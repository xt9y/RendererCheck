CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDLIBS += -lvulkan
all: vulkan-validation
vulkan-validation: main.c
	$(CC) $(CFLAGS) main.c $(LDLIBS) -o $@
clean:
	rm -f vulkan-validation
.PHONY: all clean
