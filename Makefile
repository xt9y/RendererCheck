CC ?= cc
CXX ?= c++
BUILD_DIR = build
PRODUCT = $(BUILD_DIR)/rendercheck
SOURCES = src/main.cpp src/doctor.cpp src/config.cpp src/run.cpp src/checks.cpp src/image.cpp src/visual.cpp
HEADERS = include/rendercheck/doctor.h include/rendercheck/config.h include/rendercheck/run.h include/rendercheck/checks.h include/rendercheck/image.h include/rendercheck/visual.h include/rendercheck/capture.h include/rendercheck/metrics.h include/rendercheck/version.h include/rendercheck/vulkan_min.h
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

test: export RENDERCHECK_HEADLESS_AUTO=0
test: $(PRODUCT)
	./$(PRODUCT) version
	./$(PRODUCT) help | grep -q doctor
	./$(PRODUCT) help | grep -q run
	./$(PRODUCT) help | grep -q approve
	@printf '%s\n' '#include <rendercheck/capture.h>' '#include <rendercheck/metrics.h>' 'int main(void) { return 0; }' | $(CC) $(CPPFLAGS) -std=c11 -Wall -Wextra -Wpedantic -x c - -fsyntax-only
	@tmp=$$(mktemp -d); \
	  cd $$tmp; \
	  $(CURDIR)/$(PRODUCT) init >/dev/null; \
	  test -f rendercheck.toml; \
	  printf '#!/bin/sh\nexit 0\n' > renderer; \
	  chmod +x renderer; \
	  sed -i.bak 's#./build/app#./renderer#' rendercheck.toml 2>/dev/null || sed -i '' 's#./build/app#./renderer#' rendercheck.toml; \
	  $(CURDIR)/$(PRODUCT) run >/dev/null 2>/dev/null
	@tmp=$$(mktemp -d); \
	  cd $$tmp; \
	  printf '[project]\nname = "fail-test"\ncommand = "exit 7"\n' > rendercheck.toml; \
	  ! $(CURDIR)/$(PRODUCT) run >/dev/null
	@tmp=$$(mktemp -d); \
	  cd $$tmp; \
	  printf '[project]\nname = "engine"\ncommand = "test x$$RENDERCHECK = x1"\n\n[[test]]\nname = "triangle"\nargs = ""\n' > rendercheck.toml; \
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
	  cd $$tmp; \
	  printf '%s\n' '#!/bin/sh' 'printf "gpu_ms=2.000\ngpu_ms=3.000\n" >> "$$RENDERCHECK_METRICS_PATH"' 'echo "Validation Warning: [ VUID-Smoke-00001 ] synthetic" >&2' > renderer; \
	  chmod +x renderer; \
	  printf '[project]\nname = "engine"\ncommand = "./renderer"\n\n[validation]\nvulkan = true\nfail_on_error = true\nfail_on_warning = false\n\n[performance]\nmax_gpu_ms = 4.0\nmax_process_ms = 10000.0\n' > rendercheck.toml; \
	  GITHUB_STEP_SUMMARY=$$tmp/summary.md $(CURDIR)/$(PRODUCT) run >/dev/null 2>/dev/null; \
	  test -f .rendercheck/report.md; \
	  test -f .rendercheck/results.json; \
	  test -f .rendercheck/engine/metrics.txt; \
	  test -f .rendercheck/engine/validation.log; \
	  grep -q 'RendererCheck report' $$tmp/summary.md; \
	  grep -q 'gpu_max_ms.*3.000' .rendercheck/results.json; \
	  printf '[project]\nname = "engine"\ncommand = "./renderer"\n\n[validation]\nvulkan = true\nfail_on_error = true\nfail_on_warning = false\n\n[performance]\nmax_gpu_ms = 2.5\n' > rendercheck.toml; \
	  ! $(CURDIR)/$(PRODUCT) run >/dev/null 2>/dev/null; \
	  printf '[project]\nname = "engine"\ncommand = "./renderer"\n\n[validation]\nvulkan = true\nfail_on_error = true\nfail_on_warning = true\n\n[performance]\nmax_gpu_ms = 4.0\n' > rendercheck.toml; \
	  ! $(CURDIR)/$(PRODUCT) run >/dev/null 2>/dev/null
	@if [ "$$(uname -s)" = "Linux" ]; then \
	  tmp=$$(mktemp -d); \
	  mkdir -p $$tmp/bin; \
	  printf '%s\n' '#!/bin/sh' 'test "$$1" = "-a" && shift' 'export DISPLAY=:99' 'exec "$$@"' > $$tmp/bin/xvfb-run; \
	  printf '%s\n' '#!/bin/sh' 'exit 0' > $$tmp/bin/Xvfb; \
	  chmod +x $$tmp/bin/xvfb-run $$tmp/bin/Xvfb; \
	  cd $$tmp; \
	  printf '%s\n' '#!/bin/sh' 'test "$$LIBGL_ALWAYS_SOFTWARE" = "1"' 'test "$$RENDERCHECK_HEADLESS_BACKEND" = "xvfb"' 'printf "gpu_ms=999.000\n" >> "$$RENDERCHECK_METRICS_PATH"' > renderer; \
	  chmod +x renderer; \
	  printf '[project]\nname = "headless"\ncommand = "./renderer"\n\n[validation]\nvulkan = false\n\n[performance]\nmax_gpu_ms = 1.0\n' > rendercheck.toml; \
	  RENDERCHECK_HEADLESS_AUTO=1 DISPLAY= WAYLAND_DISPLAY= PATH="$$tmp/bin:$$PATH" $(CURDIR)/$(PRODUCT) run >/dev/null; \
	  test -f .rendercheck/headless/software-renderer; \
	  grep -q '"timing_kind": "software_render"' .rendercheck/results.json; \
	  grep -q 'software max 999.00 ms' .rendercheck/report.md; \
	fi
	@tmp=$$(mktemp -d); \
	  cd $$tmp; \
	  printf '[project]\nname = "engine"\ncommand = "true"\n\n[[test]]\nname = "a/b"\n\n[[test]]\nname = "a?b"\n' > rendercheck.toml; \
	  $(CURDIR)/$(PRODUCT) run >/dev/null; \
	  test "$$(find .rendercheck -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')" -eq 2
	@tmp=$$(mktemp -d); \
	  $(MAKE) install DESTDIR=$$tmp >/dev/null; \
	  test -x "$$tmp$(BINDIR)/rendercheck"; \
	  test -f "$$tmp$(INCLUDEDIR)/rendercheck/capture.h"; \
	  test -f "$$tmp$(INCLUDEDIR)/rendercheck/metrics.h"; \
	  "$$tmp$(BINDIR)/rendercheck" version >/dev/null; \
	  $(MAKE) uninstall DESTDIR=$$tmp >/dev/null; \
	  test ! -e "$$tmp$(BINDIR)/rendercheck"; \
	  test ! -e "$$tmp$(INCLUDEDIR)/rendercheck/capture.h"; \
	  test ! -e "$$tmp$(INCLUDEDIR)/rendercheck/metrics.h"
	@echo "All smoke tests passed"

install: $(PRODUCT)
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)"
	$(INSTALL_PROGRAM) $(PRODUCT) "$(DESTDIR)$(BINDIR)/rendercheck"
	$(INSTALL) -d "$(DESTDIR)$(INCLUDEDIR)/rendercheck"
	$(INSTALL_DATA) include/rendercheck/capture.h "$(DESTDIR)$(INCLUDEDIR)/rendercheck/capture.h"
	$(INSTALL_DATA) include/rendercheck/metrics.h "$(DESTDIR)$(INCLUDEDIR)/rendercheck/metrics.h"
	@echo "Installed rendercheck to $(DESTDIR)$(BINDIR)/rendercheck"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/rendercheck"
	rm -f "$(DESTDIR)$(INCLUDEDIR)/rendercheck/capture.h"
	rm -f "$(DESTDIR)$(INCLUDEDIR)/rendercheck/metrics.h"
	-rmdir "$(DESTDIR)$(INCLUDEDIR)/rendercheck" 2>/dev/null
	@echo "Uninstalled rendercheck from $(DESTDIR)$(PREFIX)"

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all test install uninstall clean
