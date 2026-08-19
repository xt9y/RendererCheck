CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -I../../include $(shell pkg-config --cflags egl glesv2 2>/dev/null)
LDLIBS += $(shell pkg-config --libs egl glesv2 2>/dev/null)

all: egl-offscreen

egl-offscreen: main.c
	$(CC) $(CPPFLAGS) $(CFLAGS) main.c $(LDLIBS) -o $@

clean:
	rm -f egl-offscreen

.PHONY: all clean
