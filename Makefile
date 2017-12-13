NAME := libloveplugin

CC := gcc
CFLAGS := -shared -fPIC

CFILES := $(wildcard src/*.c)
INCLUDE := -Iinclude

all: linux windows
linux: $(NAME)_linux64.so $(NAME)_linux32.so
windows: $(NAME)_win64.dll $(NAME)_win32.dll

clean:
	rm -f $(NAME)_*.so $(NAME)_*.dll

$(NAME)_linux64.so: $(CFILES)
	$(CC) $(CFLAGS) $(INCLUDE) -DLINUX -m64 $^ -o $@

$(NAME)_linux32.so: $(CFILES)
	$(CC) $(CFLAGS) $(INCLUDE) -DLINUX -m32 $^ -o $@

$(NAME)_win64.dll: $(CFILES)
	x86_64-w64-mingw32-$(CC) $(CFLAGS) $(INCLUDE) -DWINDOWS -m64 $^ -o $@

$(NAME)_win32.dll: $(CFILES)
	i686-w64-mingw32-$(CC) $(CFLAGS) $(INCLUDE) -DWINDOWS -m32 $^ -o $@
