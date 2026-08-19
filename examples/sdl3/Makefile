CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -I../../include $(shell pkg-config --cflags sdl3 2>/dev/null)
LDLIBS += $(shell pkg-config --libs sdl3 2>/dev/null)

all: sdl3-renderercheck

sdl3-renderercheck: main.c
	$(CC) $(CPPFLAGS) $(CFLAGS) main.c $(LDLIBS) -o $@

clean:
	rm -f sdl3-renderercheck

.PHONY: all clean
