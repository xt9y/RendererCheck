CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -I../../include $(shell pkg-config --cflags raylib 2>/dev/null)
LDLIBS += $(shell pkg-config --libs raylib 2>/dev/null)
all: raylib-renderercheck
raylib-renderercheck: main.c
	$(CC) $(CPPFLAGS) $(CFLAGS) main.c $(LDLIBS) -o $@
clean:
	rm -f raylib-renderercheck
.PHONY: all clean
