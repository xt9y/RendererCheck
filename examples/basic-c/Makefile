CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -I../../include
all: basic
basic: main.c ../../include/rendercheck/capture.h ../../include/rendercheck/metrics.h
	$(CC) $(CPPFLAGS) $(CFLAGS) main.c -o $@
clean:
	rm -f basic
.PHONY: all clean
