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

define RENDERCHECK_TEST_SOURCE
#include "rendercheck/checks.h"
#include "rendercheck/run_performance.h"
#include <cassert>
#include <filesystem>
#include <string>
#include <vector>
int main() {
    namespace fs = std::filesystem;
    using rendercheck::MetricSummary;
    using namespace rendercheck;
    static_assert(RUN_PERFORMANCE_ABSOLUTE_FLOOR_MS == 0.25);
    static_assert(RUN_PERFORMANCE_P95_ABSOLUTE_FLOOR_MS == 0.50);
    static_assert(RUN_PERFORMANCE_P95_MIN_SAMPLES == 40u);
    assert(run_performance_metric_is_gating("cpu_render_ms", true));
    assert(!run_performance_metric_is_gating("process_ms", true));
    assert(!run_performance_p95_is_gating(39));
    assert(run_performance_p95_is_gating(40));
    assert(!run_performance_exceeds_regression_floor(1.076, 0.927, 15.0));
    assert(run_performance_exceeds_regression_floor(1.30, 1.00, 15.0));
    assert(!run_performance_exceeds_regression_floor(2.403, 1.993, 15.0, RUN_PERFORMANCE_P95_ABSOLUTE_FLOOR_MS));
    assert(run_performance_exceeds_regression_floor(2.60, 1.99, 15.0, RUN_PERFORMANCE_P95_ABSOLUTE_FLOOR_MS));
    assert(!run_performance_exceeds_regression_floor(11.4, 10.0, 15.0));
    assert(run_performance_exceeds_regression_floor(11.6, 10.0, 15.0));
    const fs::path old = fs::current_path();
    const fs::path root = fs::temp_directory_path() / "renderercheck-contract";
    std::error_code ec; fs::remove_all(root, ec); fs::create_directories(root); fs::current_path(root);
    MetricSummary cpu; cpu.name="cpu_render_ms"; cpu.samples=100; cpu.median=0.020; cpu.p95=0.030;
    MetricSummary calls; calls.name="draw_calls"; calls.samples=100; calls.median=7; calls.p95=7;
    std::vector<MetricSummary> metrics={cpu,calls}; std::string error;
    assert(save_run_performance_latest("FinalScene",1000,metrics,true,error));
    auto missing=evaluate_run_performance("FinalScene",1000,metrics,15); assert(!missing.passed && missing.baseline_missing && missing.comparisons.size()==2);
    std::size_t approved=0; assert(approve_run_performance("FinalScene",approved,error) && approved==2);
    assert(evaluate_run_performance("FinalScene",1000,metrics,15).passed);
    auto absent=evaluate_run_performance("FinalScene",1000,{},15); assert(!absent.passed);
    assert(evaluate_run_performance("FinalScene",1400,metrics,15).passed);
    MetricSummary noisy=cpu; noisy.median=0.200; noisy.p95=0.450; assert(evaluate_run_performance("FinalScene",1000,{noisy},15).passed);
    MetricSummary short_tail=noisy; short_tail.samples=24; short_tail.p95=0.600; assert(evaluate_run_performance("FinalScene",1000,{short_tail},15).passed);
    MetricSummary stable_tail=short_tail; stable_tail.samples=40; assert(!evaluate_run_performance("FinalScene",1000,{stable_tail},15).passed);
    auto process=evaluate_run_performance("ProcessOnly",1000,{},15); assert(process.passed && process.comparisons.size()==1 && !process.comparisons.front().gating);
    assert(save_run_performance_latest("FinalScene",0,{},false,error)); approved=0; assert(!approve_run_performance("FinalScene",approved,error));
    fs::current_path(old); fs::remove_all(root,ec); return 0;
}
endef

define RENDERCHECK_TEST_SCRIPT
set -eu
BIN=$${BIN:?}; ROOT=$${ROOT:?}; CC=$${CC:-cc}
"$$BIN" version | grep -q '^RendererCheck 0.3.0$$'
"$$BIN" help | grep -q 'renderercheck doctor'
"$$BIN" help | grep -q 'renderercheck approve'
"$$BIN" help | grep -q 'renderercheck perf'
printf '%s\n' '#include <rendercheck/capture.h>' '#include <rendercheck/metrics.h>' 'int main(void){return 0;}' | "$$CC" -I"$$ROOT/include" -std=c11 -Wall -Wextra -Wpedantic -x c - -fsyntax-only

tmp=$$(mktemp -d); (cd "$$tmp"; "$$BIN" init >/dev/null; grep -q '^vulkan = false$$' rendercheck.toml; grep -q '^timeout_ms = 30000.0$$' rendercheck.toml; grep -q '^headless = "auto"$$' rendercheck.toml); rm -rf "$$tmp"
tmp=$$(mktemp -d); (cd "$$tmp"; cat >rendercheck.toml <<'TOML'
[project]
name="perf-config"
command="true"
headless="none"
renderer="hardware"
[performance]
warmup_ms=250
sample_ms=750
regression_percent=12.5
min_samples=4
[[perf]]
name="steady"
command="true"
env="DEMO_MODE=steady"
warmup_ms=100
sample_ms=500
regression_percent=8
min_samples=2
timeout_ms=2000
TOML
RENDERCHECK_HEADLESS_AUTO=0 "$$BIN" run >/dev/null); rm -rf "$$tmp"
tmp=$$(mktemp -d); (cd "$$tmp"; printf '[project]\ncommand="true"\n[[perf]]\nname="same"\n[[perf]]\nname="same"\n' >rendercheck.toml; if "$$BIN" run >/dev/null 2>&1; then exit 1; fi); rm -rf "$$tmp"
for kind in unknown-key unknown-section duplicate-name duplicate-key; do tmp=$$(mktemp -d); case "$$kind" in unknown-key) printf '[project]\nname="x"\ncommand="true"\ncaputre=true\n' >"$$tmp/rendercheck.toml";; unknown-section) printf '[project]\nname="x"\ncommand="true"\n[performnce]\nmax_process_ms=1\n' >"$$tmp/rendercheck.toml";; duplicate-name) printf '[project]\nname="x"\ncommand="true"\n[[test]]\nname="same"\n[[test]]\nname="same"\n' >"$$tmp/rendercheck.toml";; duplicate-key) printf '[project]\nname="x"\ncommand="true"\ncommand="false"\n' >"$$tmp/rendercheck.toml";; esac; (cd "$$tmp"; if "$$BIN" run >/dev/null 2>&1; then exit 1; fi); rm -rf "$$tmp"; done

tmp=$$(mktemp -d); (cd "$$tmp"; cat >renderer <<'SH'
#!/bin/sh
trap '' TERM
sleep 5
SH
chmod +x renderer; printf '[project]\nname="timeout"\ncommand="./renderer"\ntimeout_ms=100\n' >rendercheck.toml; if "$$BIN" run >/dev/null 2>&1; then exit 1; fi; grep -q '"timed_out": true' .rendercheck/results.json; grep -q '"exit_code": 124' .rendercheck/results.json); rm -rf "$$tmp"

tmp=$$(mktemp -d); (cd "$$tmp"; cat >renderer <<'SH'
#!/bin/sh
printf 'P6\n1 1\n255\n\377\000\000' > "$$RENDERCHECK_CAPTURE_PATH"
printf 'gpu_ms=2.000\ndraw_calls=7.000\n' >> "$$RENDERCHECK_METRICS_PATH"
SH
chmod +x renderer; cat >rendercheck.toml <<'TOML'
[project]
name="inherit"
command="./renderer"
baseline_dir="baselines"
headless="none"
renderer="hardware"
timeout_ms=1000
capture=true
pixel_threshold=0
max_changed_percent=0
[performance]
max_gpu_ms=4
[[test]]
name="pixel"
TOML
if RENDERCHECK_HEADLESS_AUTO=0 "$$BIN" run pixel >/dev/null; then exit 1; fi; test -s .rendercheck/pixel/actual.ppm; test -s .rendercheck/pixel/actual.png; "$$BIN" approve pixel >/dev/null; RENDERCHECK_HEADLESS_AUTO=0 "$$BIN" run pixel >/dev/null; grep -q '"name":"draw_calls"' .rendercheck/results.json; cat >renderer <<'SH'
#!/bin/sh
printf 'P6\n1 1\n255\n\000\000\377' > "$$RENDERCHECK_CAPTURE_PATH"
SH
chmod +x renderer; if RENDERCHECK_HEADLESS_AUTO=0 "$$BIN" run pixel >/dev/null; then exit 1; fi; test -s .rendercheck/pixel/diff.ppm; test -s .rendercheck/pixel/diff.png); rm -rf "$$tmp"

tmp=$$(mktemp -d); (cd "$$tmp"; mkdir baselines; printf 'P6\n1 1\n255\n\377\000\000' >baselines/pixel.ppm; cat >renderer <<'SH'
#!/bin/sh
printf 'P6\n2 1\n255\n\377\000\000\377\000\000' > "$$RENDERCHECK_CAPTURE_PATH"
SH
chmod +x renderer; printf '[project]\ncommand="./renderer"\nbaseline_dir="baselines"\ncapture=true\n[[test]]\nname="pixel"\n' >rendercheck.toml; if RENDERCHECK_HEADLESS_AUTO=0 "$$BIN" run pixel >/dev/null; then exit 1; fi; test -s .rendercheck/pixel/diff.png); rm -rf "$$tmp"

tmp=$$(mktemp -d); (cd "$$tmp"; cat >renderer <<'SH'
#!/bin/sh
printf 'gpu_ms=4.000\ngpu_ms=1.000\ngpu_ms=3.000\ngpu_ms=2.000\n' >> "$$RENDERCHECK_METRICS_PATH"
echo 'Validation Warning: [ VUID-Smoke-00001 ] synthetic' >&2
SH
chmod +x renderer; printf '[project]\ncommand="./renderer"\nheadless="none"\nrenderer="hardware"\n[performance]\nmax_gpu_ms=5\nmax_process_ms=10000\n' >rendercheck.toml; RENDERCHECK_HEADLESS_AUTO=0 "$$BIN" run >/dev/null 2>/dev/null; grep -q '"gpu_max_ms": 4.000' .rendercheck/results.json; grep -q '"name":"gpu_ms".*"minimum":1.000.*"median":2.500.*"p95":4.000.*"maximum":4.000' .rendercheck/results.json); rm -rf "$$tmp"

if [ "$$(uname -s)" = Linux ]; then tmp=$$(mktemp -d); (mkdir -p "$$tmp/bin"; cat >"$$tmp/bin/xvfb-run" <<'SH'
#!/bin/sh
[ "$$1" = "-a" ] && shift
export DISPLAY=:99
exec "$$@"
SH
printf '#!/bin/sh\nexit 0\n' >"$$tmp/bin/Xvfb"; chmod +x "$$tmp/bin/xvfb-run" "$$tmp/bin/Xvfb"; cd "$$tmp"; cat >renderer <<'SH'
#!/bin/sh
[ "$$LIBGL_ALWAYS_SOFTWARE" = 1 ]
[ "$$RENDERCHECK_HEADLESS_BACKEND" = xvfb ]
printf 'gpu_ms=999.000\n' >> "$$RENDERCHECK_METRICS_PATH"
SH
chmod +x renderer; printf '[project]\ncommand="./renderer"\n[performance]\nmax_gpu_ms=1\n' >rendercheck.toml; DISPLAY= WAYLAND_DISPLAY= PATH="$$tmp/bin:$$PATH" "$$BIN" run >/dev/null; grep -q '"renderer_mode": "software"' .rendercheck/results.json; grep -q '"timing_kind": "software_render"' .rendercheck/results.json); rm -rf "$$tmp"; fi

tmp=$$(mktemp -d); (cd "$$tmp"; printf '[project]\ncommand="true"\n[[test]]\nname="a/b"\n[[test]]\nname="a?b"\n' >rendercheck.toml; RENDERCHECK_HEADLESS_AUTO=0 "$$BIN" run >/dev/null; test "$$(find .rendercheck -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')" -eq 2); rm -rf "$$tmp"

tmp=$$(mktemp -d); (cd "$$tmp"; cat >renderer <<'SH'
#!/bin/sh
printf 'P6\n1 1\n255\n\377\000\000' > "$$RENDERCHECK_CAPTURE_PATH"
value=$$(cat metric-value)
printf 'cpu_frame_ms=%s\n' "$$value" >> "$$RENDERCHECK_METRICS_PATH"
SH
chmod +x renderer; cat >rendercheck.toml <<'TOML'
[project]
command="./renderer"
baseline_dir="baselines"
headless="none"
renderer="hardware"
timeout_ms=2000
capture=true
pixel_threshold=0
max_changed_percent=0
[performance]
regression_percent=1000
[[test]]
name="pixel"
TOML
printf '1.000\n' >metric-value; if RENDERCHECK_HEADLESS_AUTO=0 "$$BIN" run pixel >/dev/null; then exit 1; fi; grep -q 'run-performance baseline missing' .rendercheck/results.json; grep -q MISSING .rendercheck/report.md; "$$BIN" approve pixel >/dev/null; test -s baselines/pixel.ppm; test -s .rendercheck/run-performance/baseline.tsv; RENDERCHECK_HEADLESS_AUTO=0 "$$BIN" run pixel >/dev/null; grep -q '"run_performance": {"checked": true' .rendercheck/results.json; printf '20.000\n' >metric-value; if RENDERCHECK_HEADLESS_AUTO=0 "$$BIN" run pixel >/dev/null; then exit 1; fi; grep -q '"name":"cpu_frame_ms".*"regressed":true' .rendercheck/results.json; grep -q REGRESSION .rendercheck/report.md); rm -rf "$$tmp"

tmp=$$(mktemp -d); (cd "$$tmp"; mkdir .rendercheck; printf 'old\n' >.rendercheck/report.md; printf '{}\n' >.rendercheck/results.json; printf '[project]\ncommand="true"\nunknown_key=true\n' >rendercheck.toml; if "$$BIN" run >/dev/null 2>&1; then exit 1; fi; test ! -e .rendercheck/report.md; test ! -e .rendercheck/results.json); rm -rf "$$tmp"

tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' EXIT HUP INT TERM; cd "$$tmp"; cat >renderer <<'SH'
#!/bin/sh
set -eu
[ "$${RENDERCHECK:-}" = 1 ]; [ "$${RENDERCHECK_PERF:-}" = 1 ]; [ -n "$${RENDERCHECK_PERF_CASE:-}" ]; [ -n "$${RENDERCHECK_PERF_WARMUP_MS:-}" ]; [ -n "$${RENDERCHECK_PERF_DURATION_MS:-}" ]; [ -n "$${RENDERCHECK_METRICS_PATH:-}" ]
echo 'synthetic perf stdout'
echo 'synthetic perf stderr' >&2
case "$$RENDERCHECK_PERF_CASE" in BVH4-linear) d=.100;l=.050;; BVH4-bvh) d=.140;l=.060;; BVH16-linear) d=.220;l=.090;; BVH16-bvh) d=.160;l=.070;; BVH64-linear) d=.700;l=.240;; BVH64-bvh) d=.280;l=.190;; *) case "$${PERF_PROFILE:-base}" in slow) d=2.000;l=.800;g=2.800;; fast) d=.800;l=.320;g=1.200;; *) d=1.000;l=.420;g=1.550;; esac;; esac
printf 'direct_ms=%s\ndirect_ms=%s\ndirect_ms=%s\n' "$$d" "$$d" "$$d" >>"$$RENDERCHECK_METRICS_PATH"; printf 'lumen_trace_ms=%s\nlumen_trace_ms=%s\nlumen_trace_ms=%s\n' "$$l" "$$l" "$$l" >>"$$RENDERCHECK_METRICS_PATH"; [ -z "$${g:-}" ] || printf 'gpu_pipeline_ms=%s\ngpu_pipeline_ms=%s\ngpu_pipeline_ms=%s\n' "$$g" "$$g" "$$g" >>"$$RENDERCHECK_METRICS_PATH"
SH
chmod +x renderer; cat >rendercheck.toml <<'TOML'
[project]
command="./renderer"
headless="none"
renderer="hardware"
timeout_ms=2000
[performance]
warmup_ms=0
sample_ms=100
regression_percent=15
min_samples=3
[[perf]]
name="steady"
[[perf]]
name="BVH4-linear"
[[perf]]
name="BVH4-bvh"
[[perf]]
name="BVH16-linear"
[[perf]]
name="BVH16-bvh"
[[perf]]
name="BVH64-linear"
[[perf]]
name="BVH64-bvh"
TOML
RENDERCHECK_HEADLESS_AUTO=0 "$$BIN" perf steady >/dev/null; test -s .rendercheck/performance/runs/steady/metrics.txt; test -s .rendercheck/performance/runs/steady/stdout.log; test -s .rendercheck/performance/runs/steady/stderr.log; grep -q '"median": 1.000' .rendercheck/performance/results.json; RENDERCHECK_HEADLESS_AUTO=0 "$$BIN" perf --approve steady >/dev/null; test -s .rendercheck/performance/baseline.tsv; if PERF_PROFILE=slow RENDERCHECK_HEADLESS_AUTO=0 "$$BIN" perf steady >/dev/null 2>&1; then exit 1; fi; grep -q regression .rendercheck/performance/report.md; PERF_PROFILE=fast RENDERCHECK_HEADLESS_AUTO=0 "$$BIN" perf steady >/dev/null; grep -q '"baseline_median"' .rendercheck/performance/results.json; RENDERCHECK_HEADLESS_AUTO=0 "$$BIN" perf >perf-all.txt; grep -q 'Recommended crossover: 16' perf-all.txt; grep -q 'Recommended crossover: 16' .rendercheck/performance/report.md
endef

export RENDERCHECK_EMBEDDED_SOURCE
export RENDERCHECK_EMBEDDED_SCRIPT
$(BUILD_DIR)/test-contract: RENDERCHECK_EMBEDDED_SOURCE = $(RENDERCHECK_TEST_SOURCE)
$(BUILD_DIR)/test-contract: src/run_performance.cpp include/rendercheck/run_performance.h include/rendercheck/checks.h
	@mkdir -p $(BUILD_DIR)
	@printf '%s\n' "$$RENDERCHECK_EMBEDDED_SOURCE" | $(CXX) $(CPPFLAGS) $(CXXFLAGS) -Werror -x c++ - src/run_performance.cpp -o $@

test-contract: $(BUILD_DIR)/test-contract
	@$(BUILD_DIR)/test-contract

test-cli: RENDERCHECK_EMBEDDED_SCRIPT = $(RENDERCHECK_TEST_SCRIPT)
test-cli: $(PRODUCT)
	@BIN="$(abspath $(PRODUCT))" ROOT="$(CURDIR)" CC="$(CC)" sh -c "$$RENDERCHECK_EMBEDDED_SCRIPT"

test-install: $(PRODUCT)
	@tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' EXIT HUP INT TERM; $(MAKE) install DESTDIR=$$tmp >/dev/null; test -x "$$tmp$(BINDIR)/renderercheck"; test -f "$$tmp$(INCLUDEDIR)/rendercheck/capture.h"; test -f "$$tmp$(INCLUDEDIR)/rendercheck/metrics.h"; "$$tmp$(BINDIR)/renderercheck" version >/dev/null; $(MAKE) uninstall DESTDIR=$$tmp >/dev/null; test ! -e "$$tmp$(BINDIR)/renderercheck"; test ! -e "$$tmp$(INCLUDEDIR)/rendercheck/capture.h"; test ! -e "$$tmp$(INCLUDEDIR)/rendercheck/metrics.h"

test: test-contract test-cli test-install
	@echo "All RendererCheck tests passed"

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

.PHONY: all test test-contract test-cli test-install install uninstall clean