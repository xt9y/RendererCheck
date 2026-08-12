CC ?= cc
TARGET = basic
CPPFLAGS += -I../../include
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CPPFLAGS) $(CFLAGS) main.c -o $@

run: $(TARGET)
	rendercheck run

clean:
	rm -f $(TARGET)
	rm -rf .rendercheck baselines

.PHONY: all run clean
