#!/bin/sh
set -eu

BIN=${BIN:?set BIN}

"$BIN" help | grep -q 'renderercheck perf'

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cd "$tmp"
cat > renderer <<'SH'
#!/bin/sh
set -eu
[ "${RENDERCHECK:-}" = 1 ]
[ "${RENDERCHECK_PERF:-}" = 1 ]
[ -n "${RENDERCHECK_PERF_CASE:-}" ]
[ -n "${RENDERCHECK_PERF_WARMUP_MS:-}" ]
[ -n "${RENDERCHECK_PERF_DURATION_MS:-}" ]
[ -n "${RENDERCHECK_METRICS_PATH:-}" ]
echo 'synthetic perf stdout'
echo 'synthetic perf stderr' >&2

case "${RENDERCHECK_PERF_CASE}" in
  BVH4-linear)
    printf 'direct_ms=0.100\ndirect_ms=0.102\ndirect_ms=0.098\n' >> "$RENDERCHECK_METRICS_PATH"
    printf 'lumen_trace_ms=0.050\nlumen_trace_ms=0.052\nlumen_trace_ms=0.048\n' >> "$RENDERCHECK_METRICS_PATH"
    exit 0
    ;;
  BVH4-bvh)
    printf 'direct_ms=0.140\ndirect_ms=0.142\ndirect_ms=0.138\n' >> "$RENDERCHECK_METRICS_PATH"
    printf 'lumen_trace_ms=0.060\nlumen_trace_ms=0.062\nlumen_trace_ms=0.058\n' >> "$RENDERCHECK_METRICS_PATH"
    exit 0
    ;;
  BVH16-linear)
    printf 'direct_ms=0.220\ndirect_ms=0.222\ndirect_ms=0.218\n' >> "$RENDERCHECK_METRICS_PATH"
    printf 'lumen_trace_ms=0.090\nlumen_trace_ms=0.092\nlumen_trace_ms=0.088\n' >> "$RENDERCHECK_METRICS_PATH"
    exit 0
    ;;
  BVH16-bvh)
    printf 'direct_ms=0.160\ndirect_ms=0.162\ndirect_ms=0.158\n' >> "$RENDERCHECK_METRICS_PATH"
    printf 'lumen_trace_ms=0.070\nlumen_trace_ms=0.072\nlumen_trace_ms=0.068\n' >> "$RENDERCHECK_METRICS_PATH"
    exit 0
    ;;
  BVH64-linear)
    printf 'direct_ms=0.700\ndirect_ms=0.710\ndirect_ms=0.690\n' >> "$RENDERCHECK_METRICS_PATH"
    printf 'lumen_trace_ms=0.240\nlumen_trace_ms=0.245\nlumen_trace_ms=0.235\n' >> "$RENDERCHECK_METRICS_PATH"
    exit 0
    ;;
  BVH64-bvh)
    printf 'direct_ms=0.280\ndirect_ms=0.285\ndirect_ms=0.275\n' >> "$RENDERCHECK_METRICS_PATH"
    printf 'lumen_trace_ms=0.190\nlumen_trace_ms=0.195\nlumen_trace_ms=0.185\n' >> "$RENDERCHECK_METRICS_PATH"
    exit 0
    ;;
esac

case "${PERF_PROFILE:-base}" in
  slow)
    printf 'direct_ms=1.800\ndirect_ms=2.000\ndirect_ms=2.200\n' >> "$RENDERCHECK_METRICS_PATH"
    printf 'lumen_trace_ms=0.700\nlumen_trace_ms=0.800\nlumen_trace_ms=0.900\n' >> "$RENDERCHECK_METRICS_PATH"
    printf 'gpu_pipeline_ms=2.700\ngpu_pipeline_ms=2.800\ngpu_pipeline_ms=2.900\n' >> "$RENDERCHECK_METRICS_PATH"
    ;;
  fast)
    printf 'direct_ms=0.700\ndirect_ms=0.800\ndirect_ms=0.900\n' >> "$RENDERCHECK_METRICS_PATH"
    printf 'lumen_trace_ms=0.300\nlumen_trace_ms=0.320\nlumen_trace_ms=0.340\n' >> "$RENDERCHECK_METRICS_PATH"
    printf 'gpu_pipeline_ms=1.100\ngpu_pipeline_ms=1.200\ngpu_pipeline_ms=1.300\n' >> "$RENDERCHECK_METRICS_PATH"
    ;;
  *)
    printf 'direct_ms=1.000\ndirect_ms=1.100\ndirect_ms=0.900\n' >> "$RENDERCHECK_METRICS_PATH"
    printf 'lumen_trace_ms=0.400\nlumen_trace_ms=0.450\nlumen_trace_ms=0.420\n' >> "$RENDERCHECK_METRICS_PATH"
    printf 'gpu_pipeline_ms=1.500\ngpu_pipeline_ms=1.600\ngpu_pipeline_ms=1.550\n' >> "$RENDERCHECK_METRICS_PATH"
    ;;
esac
SH
chmod +x renderer

cat > rendercheck.toml <<'TOML'
[project]
name = "perf-fixture"
command = "./renderer"
headless = "none"
renderer = "hardware"
timeout_ms = 2000

[performance]
warmup_ms = 0
sample_ms = 100
regression_percent = 15
min_samples = 3

[[perf]]
name = "steady"

[[perf]]
name = "BVH4-linear"

[[perf]]
name = "BVH4-bvh"

[[perf]]
name = "BVH16-linear"

[[perf]]
name = "BVH16-bvh"

[[perf]]
name = "BVH64-linear"

[[perf]]
name = "BVH64-bvh"
TOML

RENDERCHECK_HEADLESS_AUTO=0 "$BIN" perf steady >/dev/null

test -s .rendercheck/performance/runs/steady/metrics.txt
test -s .rendercheck/performance/runs/steady/stdout.log
test -s .rendercheck/performance/runs/steady/stderr.log
test -s .rendercheck/performance/report.md
test -s .rendercheck/performance/results.json
grep -q '"name": "steady"' .rendercheck/performance/results.json
grep -q '"name": "direct_ms"' .rendercheck/performance/results.json
grep -q '"median": 1.000' .rendercheck/performance/results.json
grep -q '"p95": 1.100' .rendercheck/performance/results.json

RENDERCHECK_HEADLESS_AUTO=0 "$BIN" perf --approve steady >/dev/null
test -s .rendercheck/performance/baseline.tsv
grep -q 'steady.*direct_ms' .rendercheck/performance/baseline.tsv

if PERF_PROFILE=slow RENDERCHECK_HEADLESS_AUTO=0 "$BIN" perf steady >/dev/null 2>&1; then
  echo 'expected slower performance run to fail against approved baseline' >&2
  exit 1
fi
grep -q 'regression' .rendercheck/performance/report.md

PERF_PROFILE=fast RENDERCHECK_HEADLESS_AUTO=0 "$BIN" perf steady >/dev/null
grep -q '"baseline_median"' .rendercheck/performance/results.json

RENDERCHECK_HEADLESS_AUTO=0 "$BIN" perf > perf-all.txt
grep -q 'BVH crossover' perf-all.txt
grep -Eq '4[[:space:]].*linear' perf-all.txt
grep -Eq '16[[:space:]].*bvh' perf-all.txt
grep -q 'Recommended crossover: 16' perf-all.txt
grep -q '## BVH crossover' .rendercheck/performance/report.md
grep -q 'Recommended crossover: 16' .rendercheck/performance/report.md

echo 'Performance CLI, baseline, and BVH crossover tests passed'
