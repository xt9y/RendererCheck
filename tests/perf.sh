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
[ "${RENDERCHECK_PERF_CASE:-}" = steady ]
[ -n "${RENDERCHECK_PERF_WARMUP_MS:-}" ]
[ -n "${RENDERCHECK_PERF_DURATION_MS:-}" ]
[ -n "${RENDERCHECK_METRICS_PATH:-}" ]
echo 'synthetic perf stdout'
echo 'synthetic perf stderr' >&2
printf 'direct_ms=1.000\ndirect_ms=1.100\ndirect_ms=0.900\n' >> "$RENDERCHECK_METRICS_PATH"
printf 'lumen_trace_ms=0.400\nlumen_trace_ms=0.450\nlumen_trace_ms=0.420\n' >> "$RENDERCHECK_METRICS_PATH"
printf 'gpu_pipeline_ms=1.500\ngpu_pipeline_ms=1.600\ngpu_pipeline_ms=1.550\n' >> "$RENDERCHECK_METRICS_PATH"
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

echo 'Performance CLI test passed'
