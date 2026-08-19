CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -I../../include $(shell pkg-config --cflags glfw3 2>/dev/null)
LDLIBS += $(shell pkg-config --libs glfw3 2>/dev/null) -lGL

all: glfw-opengl

glfw-opengl: main.c
	$(CC) $(CPPFLAGS) $(CFLAGS) main.c $(LDLIBS) -o $@

clean:
	rm -f glfw-opengl

.PHONY: all clean
