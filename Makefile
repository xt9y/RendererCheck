CC ?= cc
CXX ?= c++
BUILD_DIR = build
PRODUCT = $(BUILD_DIR)/renderercheck
SOURCES = src/main.cpp src/doctor.cpp src/config.cpp src/run.cpp src/run_performance.cpp src/perf.cpp src/perf_compare.cpp src/checks.cpp src/image.cpp src/visual.cpp
HEADERS = include/rendercheck/doctor.h include/rendercheck/config.h include/rendercheck/run.h include/rendercheck/run_performance.h include/rendercheck/perf.h include/rendercheck/perf_compare.h include/rendercheck/checks.h include/rendercheck/image.h include/rendercheck/visual.h include/rendercheck/capture.h include/rendercheck/metrics.h include/rendercheck/version.h include/rendercheck/vulkan_min.h
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
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@
	@echo "Build complete: $(PRODUCT)"

$(BUILD_DIR)/run-performance-contract: tests/run_performance_contract.cpp src/run_performance.cpp include/rendercheck/run_performance.h include/rendercheck/checks.h
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Werror tests/run_performance_contract.cpp src/run_performance.cpp -o $@

test: $(PRODUCT) $(BUILD_DIR)/run-performance-contract
	$(BUILD_DIR)/run-performance-contract
	BIN="$(abspath $(PRODUCT))" sh ./tests/run_performance.sh
	BIN="$(abspath $(PRODUCT))" ROOT="$(CURDIR)" CC="$(CC)" ./tests/smoke.sh
	BIN="$(abspath $(PRODUCT))" sh ./tests/perf.sh
	BIN="$(abspath $(PRODUCT))" ./tests/stale-report.sh
	@tmp=$$(mktemp -d); \
	  $(MAKE) install DESTDIR=$$tmp >/dev/null; \
	  test -x "$$tmp$(BINDIR)/renderercheck"; \
	  test -f "$$tmp$(INCLUDEDIR)/rendercheck/capture.h"; \
	  test -f "$$tmp$(INCLUDEDIR)/rendercheck/metrics.h"; \
	  "$$tmp$(BINDIR)/renderercheck" version >/dev/null; \
	  $(MAKE) uninstall DESTDIR=$$tmp >/dev/null; \
	  test ! -e "$$tmp$(BINDIR)/renderercheck"; \
	  test ! -e "$$tmp$(INCLUDEDIR)/rendercheck/capture.h"; \
	  test ! -e "$$tmp$(INCLUDEDIR)/rendercheck/metrics.h"

install: $(PRODUCT)
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)"
	$(INSTALL_PROGRAM) $(PRODUCT) "$(DESTDIR)$(BINDIR)/renderercheck"
	$(INSTALL) -d "$(DESTDIR)$(INCLUDEDIR)/rendercheck"
	$(INSTALL_DATA) include/rendercheck/capture.h "$(DESTDIR)$(INCLUDEDIR)/rendercheck/capture.h"
	$(INSTALL_DATA) include/rendercheck/metrics.h "$(DESTDIR)$(INCLUDEDIR)/rendercheck/metrics.h"
	@echo "Installed renderercheck to $(DESTDIR)$(BINDIR)/renderercheck"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/renderercheck"
	rm -f "$(DESTDIR)$(INCLUDEDIR)/rendercheck/capture.h"
	rm -f "$(DESTDIR)$(INCLUDEDIR)/rendercheck/metrics.h"
	-rmdir "$(DESTDIR)$(INCLUDEDIR)/rendercheck" 2>/dev/null
	@echo "Uninstalled RendererCheck from $(DESTDIR)$(PREFIX)"

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all test install uninstall clean
