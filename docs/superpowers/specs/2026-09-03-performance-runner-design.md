# RendererCheck Performance Runner Design

## Goal

Make RendererCheck the single validation entry point for renderer correctness and performance. Existing `renderercheck run` visual/validation behavior remains unchanged. A new `renderercheck perf` path runs hardware renderer benchmarks, consumes the existing `RENDERCHECK_METRICS_PATH` protocol, produces stable statistical summaries, and compares them with machine-local approved baselines.

## CLI

- `renderercheck run [test]` — existing correctness/visual path; unchanged.
- `renderercheck perf [case]` — run enabled performance cases.
- `renderercheck perf --approve [case]` — run the same cases, then approve their current medians/p95 values as the local performance baseline.

`perf` never enables frame capture and never sets `RENDERCHECK_TEST`; GPU applications therefore remain on their normal interactive renderer unless their benchmark configuration explicitly changes behavior.

## Configuration

Keep the existing `[performance]` absolute-budget keys and extend that section with defaults:

```toml
[performance]
warmup_ms = 1000
sample_ms = 3000
regression_percent = 15
min_samples = 3
```

Add repeatable performance cases:

```toml
[[perf]]
name = "FinalScene"
command = "./build/release/crapgame"

[[perf]]
name = "BVH64Linear"
command = "./build/release/crapgame"
env = "CRAPGAME_BVH_STRESS=64 CRAPGAME_BVH_MODE=linear"
```

Per-case `command`, `args`, `cwd`, `env`, `warmup_ms`, `sample_ms`, `regression_percent`, `min_samples`, `timeout_ms`, and `enabled` override project/global defaults where applicable.

## Execution

`renderercheck perf` launches one case at a time. The child inherits the user's graphics environment and receives:

- `RENDERCHECK=1`
- `RENDERCHECK_PERF=1`
- `RENDERCHECK_PERF_CASE=<case>`
- `RENDERCHECK_PERF_WARMUP_MS=<ms>`
- `RENDERCHECK_PERF_DURATION_MS=<warmup+sample ms>`
- `RENDERCHECK_METRICS_PATH=<absolute metrics.txt>`

The runner captures stdout/stderr into `.rendercheck/performance/runs/<case>/`. If the application honors the duration variable it exits cleanly. Otherwise RendererCheck terminates it after the configured window plus a short grace period; expected termination is not treated as a timeout failure.

Performance mode must not silently benchmark a software renderer. On Linux, explicit `LIBGL_ALWAYS_SOFTWARE=1` / `RENDERCHECK_SOFTWARE_RENDERER=1` is rejected unless a future explicit opt-in is added.

## Metrics and statistics

The metric protocol remains plain append-only `name=value` lines. RendererCheck calculates, per metric:

- sample count
- minimum
- average
- median
- p95
- maximum

Metrics ending in `_ms` are lower-is-better and participate in baseline regression checks. Other metrics remain informational.

The runner requires at least `min_samples` samples for every timing metric that appears in a baseline. New metrics without a baseline are reported but do not fail the run.

## Baselines

Performance baselines are machine-local and intentionally live under ignored `.rendercheck/` state:

`.rendercheck/performance/baseline.tsv`

The file records case, metric, median, and p95. `renderercheck perf --approve` replaces approved entries for the selected cases. Normal `renderercheck perf` fails a timing metric when either median or p95 exceeds its approved value by more than the configured regression percentage.

A missing baseline is informational, not a failure, so first-time users can run benchmarks before approving.

## Reports

Performance output is separate from visual reports:

- `.rendercheck/performance/report.md`
- `.rendercheck/performance/results.json`
- per-case `metrics.txt`, `stdout.log`, `stderr.log`

The console and markdown report show timing metrics and baseline deltas. This makes BVH linear-vs-tree sweeps a single RendererCheck command rather than shell pipelines.

## CrapGame integration

CrapGame remains responsible for producing renderer-native timings. Its GPU profiler will write sampled pass timings to `RENDERCHECK_METRICS_PATH` only when performance mode is active:

- `geometry_ms`
- `direct_ms`
- `lumen_trace_ms`
- `lumen_compose_ms`
- `present_ms`
- `gpu_pipeline_ms`
- `cpu_frame_ms`

Normal gameplay has no metric-file I/O. Performance mode shortens CPU-stat intervals and exits cleanly after `RENDERCHECK_PERF_DURATION_MS`, ensuring profiler shutdown and metric flushing.

## Compatibility

- Existing visual `[[test]]` configuration and 67 CrapGame RendererCheck scenes remain untouched.
- Existing `renderercheck run`, `diff`, and `approve` output files keep their current schema/paths.
- C/C++ `capture.h` and `metrics.h` APIs remain source compatible.
- Linux/macOS remain the supported process-execution platforms.
