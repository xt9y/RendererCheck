CC ?= cc
TARGET = raylib-rendercheck
RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib 2>/dev/null)
RAYLIB_LIBS := $(shell pkg-config --libs raylib 2>/dev/null)
CPPFLAGS += -I../../include $(RAYLIB_CFLAGS)
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic

all: $(TARGET)

$(TARGET): main.c
	@if [ -z "$(RAYLIB_LIBS)" ]; then echo "raylib pkg-config metadata not found"; exit 1; fi
	$(CC) $(CPPFLAGS) $(CFLAGS) main.c $(RAYLIB_LIBS) -o $@

run: $(TARGET)
	rendercheck run

clean:
	rm -f $(TARGET)
	rm -rf .rendercheck baselines

.PHONY: all run clean
