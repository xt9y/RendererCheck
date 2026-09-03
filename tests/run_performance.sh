#!/bin/sh
set -eu

BIN=${BIN:?set BIN}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

cd "$tmp"
cat > renderer <<'SH'
#!/bin/sh
printf 'P6\n1 1\n255\n\377\000\000' > "$RENDERCHECK_CAPTURE_PATH"
value=$(cat metric-value)
printf 'cpu_frame_ms=%s\n' "$value" >> "$RENDERCHECK_METRICS_PATH"
SH
chmod +x renderer

cat > rendercheck.toml <<'TOML'
[project]
name = "run-perf"
command = "./renderer"
baseline_dir = "baselines"
headless = "none"
renderer = "hardware"
timeout_ms = 2000
capture = true
pixel_threshold = 0
max_changed_percent = 0

[performance]
regression_percent = 1000

[[test]]
name = "pixel"
TOML

printf '1.000\n' > metric-value
if RENDERCHECK_HEADLESS_AUTO=0 "$BIN" run pixel >/dev/null; then
  echo "expected first captured run to require approval" >&2
  exit 1
fi
grep -q 'run-performance baseline missing' .rendercheck/results.json
grep -q 'MISSING' .rendercheck/report.md

"$BIN" approve pixel >/dev/null
test -s baselines/pixel.ppm
test -s .rendercheck/run-performance/baseline.tsv

RENDERCHECK_HEADLESS_AUTO=0 "$BIN" run pixel >/dev/null
grep -q '"run_performance": {"checked": true' .rendercheck/results.json
grep -q 'pixel run performance' .rendercheck/report.md
grep -q 'PASS' .rendercheck/report.md

printf '20.000\n' > metric-value
if RENDERCHECK_HEADLESS_AUTO=0 "$BIN" run pixel >/dev/null; then
  echo "expected cpu_frame_ms regression to fail rendering run" >&2
  exit 1
fi
grep -q '"name":"cpu_frame_ms".*"regressed":true' .rendercheck/results.json
grep -q 'REGRESSION' .rendercheck/report.md

echo 'Run performance integration test passed'
