# RendererCheck Performance Runner Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a first-class `renderercheck perf` command with statistical timing reports, machine-local baselines, and CrapGame GPU-profiler integration.

**Architecture:** Extend RendererCheck's existing configuration and `name=value` metric protocol with separate `[[perf]]` cases. A dedicated performance runner owns process duration, metrics/statistics, baseline comparison, and performance reports while leaving `run`/visual capture untouched. CrapGame emits native GPU/CPU timing metrics only when `RENDERCHECK_PERF=1`.

**Tech Stack:** C++20, POSIX process control on Linux/macOS, existing RendererCheck metrics protocol, OpenGL timer queries supplied by CrapGame/lwcgl.

**Spec:** `docs/superpowers/specs/2026-09-03-performance-runner-design.md`

## Global Constraints

- `renderercheck run`, visual baselines, Vulkan validation, and existing results paths must remain compatible.
- Performance baselines must stay machine-local under ignored `.rendercheck/` state.
- Performance mode must use hardware graphics and must not silently fall back to Xvfb/software rendering.
- RendererCheck remains renderer-agnostic; CrapGame-specific benchmark variants live in CrapGame's `rendercheck.toml`.
- Performance timing uses renderer-reported metrics, never wall-clock process duration as a substitute for GPU time.

---

### Task 1: Configuration model for performance cases

**Files:**
- Modify: `include/rendercheck/config.h`
- Modify: `src/config.cpp`
- Modify: `tests/smoke.sh`

**Interfaces:**
- Produces: `PerfCaseConfig`, expanded `PerformanceConfig`, and `Config::perf_cases`.

- [ ] Add failing smoke configurations proving `[[perf]]` and performance defaults/overrides parse, and malformed/duplicate perf names fail.
- [ ] Run `make test` and confirm the new assertions fail because `[[perf]]` is unknown.
- [ ] Add `warmup_ms`, `sample_ms`, `regression_percent`, `min_samples` defaults plus `PerfCaseConfig` fields.
- [ ] Extend the parser with `[[perf]]` and strict key validation.
- [ ] Run `make test` and confirm the config tests pass.
- [ ] Commit as `RendererCheck Performance Stage 03: parse perf cases`.

### Task 2: Statistical metric summaries

**Files:**
- Modify: `include/rendercheck/checks.h`
- Modify: `src/checks.cpp`
- Modify: `tests/smoke.sh`

**Interfaces:**
- Produces: `MetricSummary::{minimum,median,p95,maximum}` and reusable `summarize_metrics(path)`.

- [ ] Add a failing test with ordered/unordered metric samples and assert median/p95 values appear in JSON/report output.
- [ ] Run tests and verify failure because median/p95 are absent.
- [ ] Implement percentile calculation using sorted copies and preserve existing average/max behavior.
- [ ] Expose a reusable metric-file summarizer for the performance runner.
- [ ] Run all tests.
- [ ] Commit as `RendererCheck Performance Stage 04: summarize timing distributions`.

### Task 3: `renderercheck perf` process runner

**Files:**
- Create: `include/rendercheck/perf.h`
- Create: `src/perf.cpp`
- Modify: `src/main.cpp`
- Modify: `Makefile`
- Modify: `tests/smoke.sh`

**Interfaces:**
- Produces: `int run_perf(std::string_view filter, bool approve)`.

- [ ] Add a failing smoke test invoking `renderercheck perf` on a synthetic child that appends several timing metrics.
- [ ] Verify the CLI fails as an unknown command.
- [ ] Implement CLI parsing for `perf [case]` and `perf --approve [case]`.
- [ ] Implement POSIX child launch, environment setup, expected-duration termination, stdout/stderr capture, and metrics collection.
- [ ] Reject explicit software-renderer performance runs.
- [ ] Run tests.
- [ ] Commit as `RendererCheck Performance Stage 05: add perf runner`.

### Task 4: Local baselines and reports

**Files:**
- Modify: `src/perf.cpp`
- Modify: `tests/smoke.sh`
- Modify: `docs/CONFIG.md`
- Modify: `docs/RESULTS.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: per-case `MetricSummary` vectors.
- Produces: `.rendercheck/performance/baseline.tsv`, `report.md`, and `results.json`.

- [ ] Add a failing test: `perf --approve` writes a baseline, a slower rerun exceeding tolerance fails, and a faster rerun passes.
- [ ] Verify failure before baseline implementation.
- [ ] Implement TSV baseline read/write keyed by case+metric.
- [ ] Compare lower-is-better `_ms` metric median and p95 against baseline tolerance.
- [ ] Generate Markdown/JSON performance reports and console summaries.
- [ ] Run all tests with `-Werror` build flags.
- [ ] Commit as `RendererCheck Performance Stage 06: add performance baselines`.

### Task 5: CrapGame native metric emission

**Files (xt9y/CrapGame):**
- Modify: `Sources/Renderer/Gpu/Profiler.hpp`
- Modify: `Sources/Renderer/Gpu/Profiler.cpp`
- Modify: `Sources/main.cpp`

**Interfaces:**
- Produces metrics: `geometry_ms`, `direct_ms`, `lumen_trace_ms`, `lumen_compose_ms`, `present_ms`, `gpu_pipeline_ms`, `cpu_frame_ms`.

- [ ] Add compile-time/logic contract coverage where feasible for perf-env parsing and metric-name mapping.
- [ ] Make profiler metric emission conditional on `RENDERCHECK_METRICS_PATH`/`RENDERCHECK_PERF`.
- [ ] Suppress emitted samples during `RENDERCHECK_PERF_WARMUP_MS` while continuing normal profiler warmup.
- [ ] Emit sampled pass timings and pipeline EWMA without changing normal console profiling cadence.
- [ ] Emit CPU frame-time samples at a shorter interval in performance mode.
- [ ] Exit cleanly after `RENDERCHECK_PERF_DURATION_MS` so GPU profiler shutdown is reached.
- [ ] Commit as `Renderer Performance Stage 28: export RendererCheck metrics`.

### Task 6: CrapGame performance suite

**Files (xt9y/CrapGame):**
- Modify: `rendercheck.toml`

**Interfaces:**
- Produces `renderercheck perf` cases for FinalScene and linear/BVH primitive sweeps.

- [ ] Add `[[perf]]` cases for FinalScene and 4/8/16/32/64/128/256 stress counts in both linear and BVH modes.
- [ ] Use `./build/release/crapgame` for every performance case.
- [ ] Keep all existing 67 `[[test]]` visual cases unchanged.
- [ ] Commit as `Renderer Performance Stage 29: define RendererCheck perf suite`.

### Task 7: Verification and crossover lock-in

**Files:**
- Modify: `xt9y/CrapGame/Sources/Renderer/Gpu/BvhBench.hpp` only after measurements establish the crossover.

- [ ] Build/install RendererCheck and run its full test suite.
- [ ] Build CrapGame release.
- [ ] Run `renderercheck perf` once and inspect `.rendercheck/performance/report.md`.
- [ ] Approve a local baseline with `renderercheck perf --approve`.
- [ ] Read linear-vs-BVH pass timings from the single report and choose the first stable primitive count where BVH wins with margin.
- [ ] Update the production auto crossover to that measured value.
- [ ] Run `renderercheck run` to verify all 67 visual references.
- [ ] Commit as `Renderer Performance Stage 30: lock measured BVH crossover`.
