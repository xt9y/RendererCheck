CC ?= clang
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -fobjc-arc
CPPFLAGS += -I../../include
LDLIBS += -framework Foundation -framework Metal

all: metal-renderercheck

metal-renderercheck: main.m
	$(CC) $(CPPFLAGS) $(CFLAGS) main.m $(LDLIBS) -o $@

clean:
	rm -f metal-renderercheck

.PHONY: all clean
