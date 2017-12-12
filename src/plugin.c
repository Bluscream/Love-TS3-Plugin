/*
 * TeamSpeak 3 Love Plugin - modified demo plugin
 *
 * The original parts of the demo plugin are
 * Copyright (c) 2008-2017 TeamSpeak Systems GmbH
 *
 * The modified parts are
 * Copyright (C) 2011 Christian Goltz
 * Copyright (C) 2011-2014 Stefan Seering
 * Copyright (C) 2017 saibotu
 *
 * This file is part of Teamspeak 3 Love Plugin.
 */

#ifdef WINDOWS
#pragma warning (disable : 4100)  /* Disable Unreferenced parameter warning */
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <time.h>
#include "teamspeak/public_errors.h"
#include "teamspeak/public_errors_rare.h"
#include "teamspeak/public_definitions.h"
#include "teamspeak/public_rare_definitions.h"
#include "ts3_functions.h"
#include "plugin.h"

static struct TS3Functions ts3Functions;

#ifdef WINDOWS
#define _strcpy(dest, destSize, src) strcpy_s(dest, destSize, src)
#define snprintf sprintf_s
#else
#define _strcpy(dest, destSize, src) { strncpy(dest, src, destSize-1); (dest)[destSize-1] = '\0'; }
#endif

#define PLUGIN_API_VERSION 22

#define PATH_BUFSIZE 512
#define COMMAND_BUFSIZE 128
#define INFODATA_BUFSIZE 128
#define SERVERINFO_BUFSIZE 256
#define CHANNELINFO_BUFSIZE 512
#define RETURNCODE_BUFSIZE 128

static char* pluginID = NULL;

/*
 * The wcharToUtf8 defines done by teamspeak for windows won't compile with mingw.
 * They are never used, so make them compile with the next 2 defines.
 */
#ifndef WideCharToMultiByte
#define WideCharToMultiByte(a,b,c,d,e,f,g,h) ((c) != (c) ? 0 : 0)
#endif

#ifndef CP_UTF8
#define CP_UTF8 0
#endif

#ifdef _WIN32
/* Helper function to convert wchar_T to Utf-8 encoded strings on Windows */
static int wcharToUtf8(const wchar_t* str, char** result) {
	int outlen = WideCharToMultiByte(CP_UTF8, 0, str, -1, 0, 0, 0, 0);
	*result = (char*)malloc(outlen);
	if(WideCharToMultiByte(CP_UTF8, 0, str, -1, *result, outlen, 0, 0) == 0) {
		*result = NULL;
		return -1;
	}
	return 0;
}
#endif

// return on teamspeak error
#ifndef rote
#define rote(x) if ((x) != ERROR_ok) return
#endif

#define MAXIMAL_LOVE 100
// #define UNIQUE_ID_MAX_LEN 50
static int count_loved_users = 0; // The highest number of entries in loved_users so far for this session. The actual number of followed users at the moment may be smaller than this.
static int count_unlovely_channels = 0; // The highes number of entries in unlovely_channels so far for this session. The actual number of lovely channels at the moment may be smaller than this.
#define NO_USER  ((uint64)-1)
static const uint64 NO_SERVER = (uint64)-1;
static const uint64 NO_CHANNEL = (uint64)-1;

#ifdef EVIL_FEATURES
	#define MAX_CHANNELS_FOR_OWNAGE 100
	uint64 client_to_own = NO_USER;
	uint64 channels_used_for_ownage[MAX_CHANNELS_FOR_OWNAGE];

	void handle_client_ownage(uint64 schid /* serverConnectionHandlerID */, uint64 clientID) {

		uint64 * all_channels;
		anyID * channel_users;
		rote(ts3Functions.getChannelList(schid, &all_channels));

		int count_empty_channels = 0;
		int all_channel_index = 0;

		while (all_channels[all_channel_index] != 0 && count_empty_channels < MAX_CHANNELS_FOR_OWNAGE) {
			rote(ts3Functions.getChannelClientList(schid, all_channels[all_channel_index], &channel_users));

			if (channel_users[0] == 0) {
				channels_used_for_ownage[count_empty_channels] = all_channels[all_channel_index];
				count_empty_channels++;
			}

			ts3Functions.freeMemory(channel_users);

			all_channel_index++;
		}

		ts3Functions.freeMemory(all_channels);

		srand(time(NULL));

		for (int i=0; i<2*count_empty_channels; i++) {
			int a = rand() % count_empty_channels;
			int b = rand() % count_empty_channels;

			uint64 tmp = channels_used_for_ownage[a];
			channels_used_for_ownage[a] = channels_used_for_ownage[b];
			channels_used_for_ownage[a] = tmp;
		}

		client_to_own = NO_USER;
		if (count_empty_channels > 0) {
			for (int i=count_empty_channels-1; i>=0; i--) {
				ts3Functions.requestClientMove(schid, clientID, channels_used_for_ownage[i], "", NULL);
				//sleep(500000);
			}
			client_to_own = clientID;
		}
	}
#endif

struct channel_follow_data {
	uint64 server;  // serverConnectionHandlerID (uint64) or NO_SERVER,  valid for the current session, needed for method calls
	uint64 channel; // channelID                 (uint64) or NO_CHANNEL, valid for the current session, needed for method calls
};

struct client_follow_data {
	uint64 client;                            // clientID                        (uint64)                          or NO_USER,              valid for the current session, needed for method calls
	uint64 server;                            // serverConnectionHandlerID       (uint64)                          or NO_SERVER,            valid for the current session, needed for method calls
//	char permanent_client[UNIQUE_ID_MAX_LEN]; // CLIENT_UNIQUE_IDENTIFIER        (string with base64 encoded data) or string with length 0, valid across several sessios,  needed to save follow data across sessions
//	char permanent_server[UNIQUE_ID_MAX_LEN]; // VIRTUALSERVER_UNIQUE_IDENTIFIER (string with base64 encoded data) or string with length 0, valid across several sessios,  needed to save follow data across sessions
};

static struct client_follow_data loved_users[MAXIMAL_LOVE];
static struct channel_follow_data unlovely_channels[MAXIMAL_LOVE]; // Channels not to follow into.

void unfollow_on_server(uint64 server) {
	for (int i=0; i<count_loved_users; i++) {
		if (loved_users[i].server == server) {
			loved_users[i].client = NO_USER;
			loved_users[i].server = NO_SERVER;
//			loved_users[i].premanent_client[0] = '\0';
//			loved_users[i].premanent_server[0] = '\0';
		}
	}
}

//// clientID 2 VIRTUALSERVER_UNIQUE_IDENTIFIER
//void cid2cui(uint64 in, char * out) {
//}
//
//// serverConnectionHandlerID 2 VIRTUALSERVER_UNIQUE_IDENTIFIER
//void schid2vsui(uint64 in, char * out) {
//}

void set_loved_user(uint64 server, uint64 client) {
	for (int i=0; i<count_loved_users; i++) {
		if (loved_users[i].server == server) {
			loved_users[i].client = client; 
//			schid2vsui(client, loved_users[i].permanent_server);
//			cid2cui(client, loved_users[i].permanent_client);
			return;
		}
	}

	if (count_loved_users < MAXIMAL_LOVE) {
		loved_users[count_loved_users].server = server;
		loved_users[count_loved_users].client = client; 
//		schid2vsui(client, loved_users[count_loved_users].permanent_server);
//		cid2cui(client, loved_users[count_loved_users].permanent_client);
		count_loved_users++;
	}
}

uint64 get_loved_user(uint64 server) {
	for (int i=0; i<count_loved_users; i++) {
		if (loved_users[i].server == server) return loved_users[i].client;
	}

	return NO_USER;
}

void disallow_channel_autofollow(uint64 server, uint64 channelID) {
	for (int i=0; i<count_unlovely_channels; i++) {
		if (unlovely_channels[i].channel == NO_CHANNEL 
				&& (unlovely_channels[i].server == server || unlovely_channels[i].server == NO_SERVER)) {
			unlovely_channels[i].channel = channelID;
			unlovely_channels[i].server = server;
			return;
		}
	}

	if (count_unlovely_channels < MAXIMAL_LOVE) {
		unlovely_channels[count_unlovely_channels].channel = channelID;
		unlovely_channels[count_unlovely_channels].server = server;
		count_unlovely_channels++;
	}
}

void allow_channel_autofollow(uint64 server, uint64 channelID) {
	for (int i=0; i<count_unlovely_channels; i++) {
		if (unlovely_channels[i].channel == channelID && unlovely_channels[i].server == server) {
			unlovely_channels[i].channel = NO_CHANNEL;
			unlovely_channels[i].channel = NO_SERVER;
		}
	}
}

bool is_channel_lovely(uint64 server, uint64 channel) { // Should I autofollow into this channel?
	for (int i=0; i<count_unlovely_channels; i++) {
		if (unlovely_channels[i].channel == channel && unlovely_channels[i].server == server) {
			return false;
		}
	}

	return true;
}

void handle_client_movement(uint64 schid) {
	uint64 toFollow = get_loved_user(schid);

	if (toFollow != NO_USER) {
		anyID myself;
		rote(ts3Functions.getClientID(schid, &myself));

		uint64 toFollowChannel, myChannel;
		rote(ts3Functions.getChannelOfClient(schid, toFollow, &toFollowChannel));
		rote(ts3Functions.getChannelOfClient(schid, myself, &myChannel));

		if (myChannel != toFollowChannel && is_channel_lovely(schid, toFollowChannel)) {
			ts3Functions.requestClientMove(schid, myself, toFollowChannel, "", NULL);
		}
	}

	#ifdef EVIL_FEATURES
		if (client_to_own == NO_USER) return;
		uint64 ownee_channel;
		rote(ts3Functions.getChannelOfClient(schid, client_to_own, &ownee_channel));
		if (channels_used_for_ownage[0] !=  ownee_channel) {
			ts3Functions.requestClientMove(schid, client_to_own, channels_used_for_ownage[0], "", NULL);
		}
	#endif
}

void handle_client_unfollow(uint64 schid) {
	unfollow_on_server(schid);
}

void handle_client_follow(uint64 schid, uint64 clientID) {
	anyID myself;
	rote(ts3Functions.getClientID(schid, &myself));
	if (clientID == myself) return;

	set_loved_user(schid, clientID);

	handle_client_movement(schid);
}

/*********************************** Required functions ************************************/
/*
 * If any of these required functions is not implemented, TS3 will refuse to load the plugin
 */

/* Unique name identifying this plugin */
const char* ts3plugin_name() {
    return "Love Plugin";
}

/* Plugin version */
const char* ts3plugin_version() {
    return "1.3";
}

/* Plugin API version. Must be the same as the clients API major version, else the plugin fails to load. */
int ts3plugin_apiVersion() {
	return PLUGIN_API_VERSION;
}

/* Plugin author */
const char* ts3plugin_author() {
    return "Christian Goltz, Stefan Seering, Bluscream, saibotu";
}

/* Plugin description */
const char* ts3plugin_description() {
    return "This plugin allows you to follow a user while he switches through channels.\nFor the love menu just right click any name in the server view.\n\n\nhttp://addons.teamspeak.com/directory/plugins/miscellaneous/Love.html\n\nhttp://forum.teamspeak.com/showthread.php/59939-BETA-love-autofollower\n\nhttps://sourceforge.net/projects/ts3love/";
}

/* Set TeamSpeak 3 callback functions */
void ts3plugin_setFunctionPointers(const struct TS3Functions funcs) {
    ts3Functions = funcs;
}

/*
 * Custom code called right after loading the plugin. Returns 0 on success, 1 on failure.
 * If the function returns 1 on failure, the plugin will be unloaded again.
 */
int ts3plugin_init() {
    return 0;  /* 0 = success, 1 = failure, -2 = failure but client will not show a "failed to load" warning */
	/* -2 is a very special case and should only be used if a plugin displays a dialog (e.g. overlay) asking the user to disable
	 * the plugin again, avoiding the show another dialog by the client telling the user the plugin failed to load.
	 * For normal case, if a plugin really failed to load because of an error, the correct return value is 1. */
}

/* Custom code called right before the plugin is unloaded */
void ts3plugin_shutdown() {
	/* Free pluginID if we registered it */
	if (pluginID) {
		free(pluginID);
		pluginID = NULL;
	}
}

/****************************** Optional functions ********************************/
/*
 * Following functions are optional, if not needed you don't need to implement them.
 */

/* Tell client if plugin offers a configuration window. If this function is not implemented, it's an assumed "does not offer" (PLUGIN_OFFERS_NO_CONFIGURE). */
int ts3plugin_offersConfigure() {
	printf("PLUGIN: offersConfigure\n");
	/*
	 * Return values:
	 * PLUGIN_OFFERS_NO_CONFIGURE         - Plugin does not implement ts3plugin_configure
	 * PLUGIN_OFFERS_CONFIGURE_NEW_THREAD - Plugin does implement ts3plugin_configure and requests to run this function in an own thread
	 * PLUGIN_OFFERS_CONFIGURE_QT_THREAD  - Plugin does implement ts3plugin_configure and requests to run this function in the Qt GUI thread
	 */
	return PLUGIN_OFFERS_NO_CONFIGURE;  /* In this case ts3plugin_configure does not need to be implemented */
}

/* Plugin might offer a configuration window. If ts3plugin_offersConfigure returns 0, this function does not need to be implemented. */
void ts3plugin_configure(void* handle, void* qParentWidget) {
//    printf("PLUGIN: configure\n");
}

/*
 * If the plugin wants to use error return codes, plugin commands, hotkeys or menu items, it needs to register a command ID. This function will be
 * automatically called after the plugin was initialized. This function is optional. If you don't use these features, this function can be omitted.
 * Note the passed pluginID parameter is no longer valid after calling this function, so you must copy it and store it in the plugin.
 */
void ts3plugin_registerPluginID(const char* id) {
	const size_t sz = strlen(id) + 1;
	pluginID = (char*)malloc(sz * sizeof(char));
	_strcpy(pluginID, sz, id);  /* The id buffer will invalidate after exiting this function */
	printf("PLUGIN: registerPluginID: %s\n", pluginID);
}

/* Plugin command keyword. Return NULL or "" if not used. */
const char* ts3plugin_commandKeyword() {
	return NULL;
}

/*
 * Implement the following three functions when the plugin should display a line in the server/channel/client info.
 * If any of ts3plugin_infoTitle, ts3plugin_infoData or ts3plugin_freeMemory is missing, the info text will not be displayed.
 */

/* Static title shown in the left column in the info frame */
const char* ts3plugin_infoTitle() {
	return "Love Plugin";
}

/*
 * Dynamic content shown in the right column in the info frame. Memory for the data string needs to be allocated in this
 * function. The client will call ts3plugin_freeMemory once done with the string to release the allocated memory again.
 * Check the parameter "type" if you want to implement this feature only for specific item types. Set the parameter
 * "data" to NULL to have the client ignore the info data.
 */
void ts3plugin_infoData(uint64 serverConnectionHandlerID, uint64 id, enum PluginItemType type, char** data) {
	*data = (char*)malloc(INFODATA_BUFSIZE * sizeof(char));  /* Must be allocated in the plugin! */
	bool love;

	switch(type) {
		case PLUGIN_SERVER:
			data = NULL;  /* Ignore */
			return;
		case PLUGIN_CHANNEL:
			love = is_channel_lovely(serverConnectionHandlerID, id);
			snprintf(*data, INFODATA_BUFSIZE, "%sovely Channel (You %s automatically follow followed users into this Channel.)", love ? "L" : "Unl", love ? "will" : "won't");
			break;
		case PLUGIN_CLIENT:
			love = get_loved_user(serverConnectionHandlerID) == id;
			snprintf(*data, INFODATA_BUFSIZE, "%s (You %s automatically follow this User.)", love ? "Loved one" : "Neutral", love ? "will" : "won't");
			break;
		default:
			printf("Invalid item type: %d\n", type);
			data = NULL;  /* Ignore */
			return;
	}
}

/* Required to release the memory for parameter "data" allocated in ts3plugin_infoData and ts3plugin_initMenus */
void ts3plugin_freeMemory(void* data) {
	free(data);
}

/*
 * Plugin requests to be always automatically loaded by the TeamSpeak 3 client unless
 * the user manually disabled it in the plugin dialog.
 * This function is optional. If missing, no autoload is assumed.
 */
int ts3plugin_requestAutoload() {
	return 1;  /* 1 = request autoloaded, 0 = do not request autoload */
}

/* Helper function to create a menu item */
static struct PluginMenuItem* createMenuItem(enum PluginMenuType type, int id, const char* text, const char* icon) {
	struct PluginMenuItem* menuItem = (struct PluginMenuItem*)malloc(sizeof(struct PluginMenuItem));
	menuItem->type = type;
	menuItem->id = id;
	_strcpy(menuItem->text, PLUGIN_MENU_BUFSZ, text);
	_strcpy(menuItem->icon, PLUGIN_MENU_BUFSZ, icon);
	return menuItem;
}

/* Some makros to make the code to create menu items a bit more readable */
#define BEGIN_CREATE_MENUS(x) const size_t sz = x + 1; size_t n = 0; *menuItems = (struct PluginMenuItem**)malloc(sizeof(struct PluginMenuItem*) * sz);
#define CREATE_MENU_ITEM(a, b, c, d) (*menuItems)[n++] = createMenuItem(a, b, c, d);
#define END_CREATE_MENUS (*menuItems)[n++] = NULL; assert(n == sz);

/*
 * Menu IDs for this plugin. Pass these IDs when creating a menuitem to the TS3 client. When the menu item is triggered,
 * ts3plugin_onMenuItemEvent will be called passing the menu ID of the triggered menu item.
 * These IDs are freely choosable by the plugin author. It's not really needed to use an enum, it just looks prettier.
 */
enum {
	MENU_ID_LOVE_CLIENT = 1,
	MENU_ID_UNFOLLOW,
	MENU_ID_FOLLOW_INTO_CHANNEL,
	MENU_ID_DONT_FOLLOW_INTO_CHANNEL,
	#ifdef EVIL_FEATURES
		MENU_ID_OWN,
		MENU_ID_FORGIVE,
	#endif
};

/*
 * Initialize plugin menus.
 * This function is called after ts3plugin_init and ts3plugin_registerPluginID. A pluginID is required for plugin menus to work.
 * Both ts3plugin_registerPluginID and ts3plugin_freeMemory must be implemented to use menus.
 * If plugin menus are not used by a plugin, do not implement this function or return NULL.
 */
void ts3plugin_initMenus(struct PluginMenuItem*** menuItems, char** menuIcon) {
	/*
	 * Create the menus
	 * There are three types of menu items:
	 * - PLUGIN_MENU_TYPE_CLIENT:  Client context menu
	 * - PLUGIN_MENU_TYPE_CHANNEL: Channel context menu
	 * - PLUGIN_MENU_TYPE_GLOBAL:  "Plugins" menu in menu bar of main window
	 *
	 * Menu IDs are used to identify the menu item when ts3plugin_onMenuItemEvent is called
	 *
	 * The menu text is required, max length is 128 characters
	 *
	 * The icon is optional, max length is 128 characters. When not using icons, just pass an empty string.
	 * Icons are loaded from a subdirectory in the TeamSpeak client plugins folder. The subdirectory must be named like the
	 * plugin filename, without dll/so/dylib suffix
	 * e.g. for "test_plugin.dll", icon "1.png" is loaded from <TeamSpeak 3 Client install dir>\plugins\test_plugin\1.png
	 */

	int count_menus = 4;
	#ifdef EVIL_FEATURES
		count_menus = 6;
	#endif

	BEGIN_CREATE_MENUS(count_menus);  /* IMPORTANT: Number of menu items must be correct! */
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_CLIENT,  MENU_ID_LOVE_CLIENT,              "Love this user",  "love.png");
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_CLIENT,  MENU_ID_UNFOLLOW,                 "Unfollow this user",  "unlove.png");
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_CHANNEL, MENU_ID_FOLLOW_INTO_CHANNEL,      "Follow into this Channel",  "follow.png");
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_CHANNEL, MENU_ID_DONT_FOLLOW_INTO_CHANNEL, "Don't follow into this Channel",  "unfollow.png");
	#ifdef EVIL_FEATURES
		CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_CLIENT, MENU_ID_OWN,     "Troll this user",  "troll.png");
		CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_CLIENT, MENU_ID_FORGIVE, "Forgive this user",  "forgive.png");
	#endif
	END_CREATE_MENUS;  /* Includes an assert checking if the number of menu items matched */

	/*
	 * Specify an optional icon for the plugin. This icon is used for the plugins submenu within context and main menus
	 * If unused, set menuIcon to NULL
	 */
	*menuIcon = (char*)malloc(PLUGIN_MENU_BUFSZ * sizeof(char));
	_strcpy(*menuIcon, PLUGIN_MENU_BUFSZ, "love.png");

	/*
	 * Menus can be enabled or disabled with: ts3Functions.setPluginMenuEnabled(pluginID, menuID, 0|1);
	 * Test it with plugin command: /test enablemenu <menuID> <0|1>
	 * Menus are enabled by default. Please note that shown menus will not automatically enable or disable when calling this function to
	 * ensure Qt menus are not modified by any thread other the UI thread. The enabled or disable state will change the next time a
	 * menu is displayed.
	 */
	/* For example, this would disable MENU_ID_GLOBAL_2: */
	/* ts3Functions.setPluginMenuEnabled(pluginID, MENU_ID_GLOBAL_2, 0); */

	/* All memory allocated in this function will be automatically released by the TeamSpeak client later by calling ts3plugin_freeMemory */
}

/************************** TeamSpeak callbacks ***************************/
/*
 * Following functions are optional, feel free to remove unused callbacks.
 * See the clientlib documentation for details on each function.
 */

/* Clientlib */

void ts3plugin_onConnectStatusChangeEvent(uint64 serverConnectionHandlerID, int newStatus, unsigned int errorNumber) {
    /* Some example code following to show how to use the information query functions. */
	if (newStatus == STATUS_CONNECTION_ESTABLISHED) {
		handle_client_movement(serverConnectionHandlerID);
	}
}

void ts3plugin_onClientMoveEvent(uint64 serverConnectionHandlerID, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, const char* moveMessage) {
	handle_client_movement(serverConnectionHandlerID);
}

void ts3plugin_onClientMoveMovedEvent(uint64 serverConnectionHandlerID, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, anyID moverID, const char* moverName, const char* moverUniqueIdentifier, const char* moveMessage) {
	handle_client_movement(serverConnectionHandlerID);
}

/*
 * Called when a plugin menu item (see ts3plugin_initMenus) is triggered. Optional function, when not using plugin menus, do not implement this.
 * 
 * Parameters:
 * - serverConnectionHandlerID: ID of the current server tab
 * - type: Type of the menu (PLUGIN_MENU_TYPE_CHANNEL, PLUGIN_MENU_TYPE_CLIENT or PLUGIN_MENU_TYPE_GLOBAL)
 * - menuItemID: Id used when creating the menu item
 * - selectedItemID: Channel or Client ID in the case of PLUGIN_MENU_TYPE_CHANNEL and PLUGIN_MENU_TYPE_CLIENT. 0 for PLUGIN_MENU_TYPE_GLOBAL.
 */
void ts3plugin_onMenuItemEvent(uint64 serverConnectionHandlerID, enum PluginMenuType type, int menuItemID, uint64 selectedItemID) {
	switch(type) {
		case PLUGIN_MENU_TYPE_CLIENT:
			switch(menuItemID) {
				case MENU_ID_LOVE_CLIENT:
					handle_client_follow(serverConnectionHandlerID, selectedItemID);
					break;
				case MENU_ID_UNFOLLOW:
					handle_client_unfollow(serverConnectionHandlerID);
					break;
				#ifdef EVIL_FEATURES
					case MENU_ID_OWN:
						handle_client_ownage(serverConnectionHandlerID, selectedItemID);
						break;
					case MENU_ID_FORGIVE:
						client_to_own = NO_USER;
						break;
				#endif
				default:
					break;
			}
			break;
		case PLUGIN_MENU_TYPE_CHANNEL:
			switch(menuItemID) {
				case MENU_ID_FOLLOW_INTO_CHANNEL:
					allow_channel_autofollow(serverConnectionHandlerID, selectedItemID);
					break;
				case MENU_ID_DONT_FOLLOW_INTO_CHANNEL:
					disallow_channel_autofollow(serverConnectionHandlerID, selectedItemID);
					break;
				default:
					break;
			}
			break;
		default:
			break;
	}
}
