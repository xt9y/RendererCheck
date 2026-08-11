CXX ?= c++
BUILD_DIR = build
PRODUCT = $(BUILD_DIR)/rendercheck
SOURCES = src/main.cpp src/doctor.cpp src/config.cpp src/run.cpp src/image.cpp src/visual.cpp
HEADERS = include/rendercheck/doctor.h include/rendercheck/config.h include/rendercheck/run.h include/rendercheck/image.h include/rendercheck/visual.h include/rendercheck/capture.h include/rendercheck/version.h include/rendercheck/vulkan_min.h
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Iinclude

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INCLUDEDIR ?= $(PREFIX)/include
INSTALL ?= install
INSTALL_PROGRAM ?= $(INSTALL) -m 755
INSTALL_DATA ?= $(INSTALL) -m 644

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
LDLIBS += -ldl
endif

all: $(PRODUCT)

$(PRODUCT): $(SOURCES) $(HEADERS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) $(LDLIBS) -o $@
	@echo "Build complete: $(PRODUCT)"

test: $(PRODUCT)
	./$(PRODUCT) version
	./$(PRODUCT) help | grep -q doctor
	./$(PRODUCT) help | grep -q run
	./$(PRODUCT) help | grep -q approve
	@tmp=$$(mktemp -d); \
	  cd $$tmp; \
	  $(CURDIR)/$(PRODUCT) init >/dev/null; \
	  test -f rendercheck.toml; \
	  printf '#!/bin/sh\nexit 0\n' > renderer; \
	  chmod +x renderer; \
	  sed -i.bak 's#./build/app#./renderer#' rendercheck.toml 2>/dev/null || sed -i '' 's#./build/app#./renderer#' rendercheck.toml; \
	  $(CURDIR)/$(PRODUCT) run >/dev/null
	@tmp=$$(mktemp -d); \
	  cd $$tmp; \
	  printf '[project]\nname = "fail-test"\ncommand = "exit 7"\n' > rendercheck.toml; \
	  ! $(CURDIR)/$(PRODUCT) run >/dev/null
	@tmp=$$(mktemp -d); \
	  cd $$tmp; \
	  printf '[project]\nname = "engine"\ncommand = "test \"x$$RENDERCHECK\" = x1"\n\n[[test]]\nname = "triangle"\nargs = ""\n' > rendercheck.toml; \
	  $(CURDIR)/$(PRODUCT) run triangle >/dev/null
	@tmp=$$(mktemp -d); \
	  cd $$tmp; \
	  printf '%s\n' '#!/bin/sh' 'printf "P6\n1 1\n255\n\377\000\000" > "$$RENDERCHECK_CAPTURE_PATH"' > renderer; \
	  chmod +x renderer; \
	  printf '[project]\nname = "engine"\ncommand = "./renderer"\nbaseline_dir = "baselines"\n\n[[test]]\nname = "pixel"\ncapture = true\npixel_threshold = 0\nmax_changed_percent = 0.0\n' > rendercheck.toml; \
	  ! $(CURDIR)/$(PRODUCT) run pixel >/dev/null; \
	  test -f .rendercheck/pixel/actual.ppm; \
	  $(CURDIR)/$(PRODUCT) approve pixel >/dev/null; \
	  test -f baselines/pixel.ppm; \
	  $(CURDIR)/$(PRODUCT) run pixel >/dev/null; \
	  $(CURDIR)/$(PRODUCT) diff pixel >/dev/null; \
	  printf '%s\n' '#!/bin/sh' 'printf "P6\n1 1\n255\n\000\000\377" > "$$RENDERCHECK_CAPTURE_PATH"' > renderer; \
	  ! $(CURDIR)/$(PRODUCT) run pixel >/dev/null; \
	  test -f .rendercheck/pixel/diff.ppm
	@tmp=$$(mktemp -d); \
	  $(MAKE) install DESTDIR=$$tmp >/dev/null; \
	  test -x "$$tmp$(BINDIR)/rendercheck"; \
	  test -f "$$tmp$(INCLUDEDIR)/rendercheck/capture.h"; \
	  "$$tmp$(BINDIR)/rendercheck" version >/dev/null; \
	  $(MAKE) uninstall DESTDIR=$$tmp >/dev/null; \
	  test ! -e "$$tmp$(BINDIR)/rendercheck"; \
	  test ! -e "$$tmp$(INCLUDEDIR)/rendercheck/capture.h"
	@echo "All smoke tests passed"

install: $(PRODUCT)
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)"
	$(INSTALL_PROGRAM) $(PRODUCT) "$(DESTDIR)$(BINDIR)/rendercheck"
	$(INSTALL) -d "$(DESTDIR)$(INCLUDEDIR)/rendercheck"
	$(INSTALL_DATA) include/rendercheck/capture.h "$(DESTDIR)$(INCLUDEDIR)/rendercheck/capture.h"
	@echo "Installed rendercheck to $(DESTDIR)$(BINDIR)/rendercheck"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/rendercheck"
	rm -f "$(DESTDIR)$(INCLUDEDIR)/rendercheck/capture.h"
	-rmdir "$(DESTDIR)$(INCLUDEDIR)/rendercheck" 2>/dev/null
	@echo "Uninstalled rendercheck from $(DESTDIR)$(PREFIX)"

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all test install uninstall clean
