#!/bin/sh
set -eu

BIN=${BIN:?set BIN}
ROOT=${ROOT:?set ROOT}
CC=${CC:-cc}

"$BIN" version | grep -q '^RendererCheck 0.3.0$'
"$BIN" help | grep -q 'renderercheck doctor'
"$BIN" help | grep -q 'renderercheck approve'

printf '%s\n' '#include <rendercheck/capture.h>' '#include <rendercheck/metrics.h>' 'int main(void) { return 0; }' |
  "$CC" -I"$ROOT/include" -std=c11 -Wall -Wextra -Wpedantic -x c - -fsyntax-only

tmp=$(mktemp -d)
(
  cd "$tmp"
  "$BIN" init >/dev/null
  grep -q '^vulkan = false$' rendercheck.toml
  grep -q '^timeout_ms = 30000.0$' rendercheck.toml
  grep -q '^headless = "auto"$' rendercheck.toml
)
rm -rf "$tmp"

for kind in unknown-key unknown-section duplicate-name duplicate-key; do
  tmp=$(mktemp -d)
  case "$kind" in
    unknown-key) printf '[project]\nname="x"\ncommand="true"\ncaputre=true\n' > "$tmp/rendercheck.toml" ;;
    unknown-section) printf '[project]\nname="x"\ncommand="true"\n\n[performnce]\nmax_process_ms=1\n' > "$tmp/rendercheck.toml" ;;
    duplicate-name) printf '[project]\nname="x"\ncommand="true"\n\n[[test]]\nname="same"\n\n[[test]]\nname="same"\n' > "$tmp/rendercheck.toml" ;;
    duplicate-key) printf '[project]\nname="x"\ncommand="true"\ncommand="false"\n' > "$tmp/rendercheck.toml" ;;
  esac
  (cd "$tmp"; if "$BIN" run >/dev/null 2>&1; then exit 1; fi)
  rm -rf "$tmp"
done

tmp=$(mktemp -d)
(
  cd "$tmp"
  printf '[project]\nname="fresh"\ncommand="true"\nheadless="none"\n' > rendercheck.toml
  RENDERCHECK_HEADLESS_AUTO=0 "$BIN" run >/dev/null
  test -s .rendercheck/report.md
  test -s .rendercheck/results.json
  printf '[project]\nname="broken"\ncommand="true"\nunknown=true\n' > rendercheck.toml
  if RENDERCHECK_HEADLESS_AUTO=0 "$BIN" run >/dev/null 2>&1; then exit 1; fi
  test ! -e .rendercheck/report.md
  test ! -e .rendercheck/results.json
)
rm -rf "$tmp"

tmp=$(mktemp -d)
(
  cd "$tmp"
  cat > renderer <<'SH'
#!/bin/sh
trap '' TERM
sleep 5
SH
  chmod +x renderer
  printf '[project]\nname="timeout"\ncommand="./renderer"\ntimeout_ms=100\n' > rendercheck.toml
  if "$BIN" run >/dev/null 2>&1; then exit 1; fi
  grep -q '"timed_out": true' .rendercheck/results.json
  grep -q '"exit_code": 124' .rendercheck/results.json
)
rm -rf "$tmp"

tmp=$(mktemp -d)
(
  cd "$tmp"
  cat > renderer <<'SH'
#!/bin/sh
(
  trap '' TERM
  sleep 30
) &
echo $! > child.pid
trap 'exit 0' TERM
wait
SH
  chmod +x renderer
  printf '[project]\nname="timeout-tree"\ncommand="./renderer"\ntimeout_ms=1000\n' > rendercheck.toml
  if "$BIN" run >/dev/null 2>&1; then exit 1; fi
  child_pid=$(cat child.pid)
  sleep 1
  if kill -0 "$child_pid" 2>/dev/null; then
    echo "timeout left descendant $child_pid running" >&2
    exit 1
  fi
)
rm -rf "$tmp"

tmp=$(mktemp -d)
(
  cd "$tmp"
  cat > renderer <<'SH'
#!/bin/sh
printf 'P6\n1 1\n255\n\377\000\000' > "$RENDERCHECK_CAPTURE_PATH"
printf 'gpu_ms=2.000\ndraw_calls=7.000\n' >> "$RENDERCHECK_METRICS_PATH"
SH
  chmod +x renderer
  cat > rendercheck.toml <<'TOML'
[project]
name = "inherit"
command = "./renderer"
baseline_dir = "baselines"
headless = "none"
renderer = "hardware"
timeout_ms = 1000
capture = true
pixel_threshold = 0
max_changed_percent = 0

[performance]
max_gpu_ms = 4

[[test]]
name = "pixel"
TOML
  if RENDERCHECK_HEADLESS_AUTO=0 "$BIN" run pixel >/dev/null; then exit 1; fi
  test -s .rendercheck/pixel/actual.ppm
  test -s .rendercheck/pixel/actual.png
  "$BIN" approve pixel >/dev/null
  RENDERCHECK_HEADLESS_AUTO=0 "$BIN" run pixel >/dev/null
  grep -q '"name":"draw_calls"' .rendercheck/results.json
  cat > renderer <<'SH'
#!/bin/sh
printf 'P6\n1 1\n255\n\000\000\377' > "$RENDERCHECK_CAPTURE_PATH"
SH
  chmod +x renderer
  if RENDERCHECK_HEADLESS_AUTO=0 "$BIN" run pixel >/dev/null; then exit 1; fi
  test -s .rendercheck/pixel/diff.ppm
  test -s .rendercheck/pixel/diff.png
)
rm -rf "$tmp"

tmp=$(mktemp -d)
(
  cd "$tmp"
  mkdir -p baselines
  printf 'P6\n1 1\n255\n\377\000\000' > baselines/pixel.ppm
  cat > renderer <<'SH'
#!/bin/sh
printf 'P6\n2 1\n255\n\377\000\000\377\000\000' > "$RENDERCHECK_CAPTURE_PATH"
SH
  chmod +x renderer
  printf '[project]\nname="d"\ncommand="./renderer"\nbaseline_dir="baselines"\ncapture=true\n\n[[test]]\nname="pixel"\n' > rendercheck.toml
  if RENDERCHECK_HEADLESS_AUTO=0 "$BIN" run pixel >/dev/null; then exit 1; fi
  test -s .rendercheck/pixel/diff.png
)
rm -rf "$tmp"

tmp=$(mktemp -d)
(
  cd "$tmp"
  cat > renderer <<'SH'
#!/bin/sh
printf 'gpu_ms=2.000\ngpu_ms=3.000\n' >> "$RENDERCHECK_METRICS_PATH"
echo 'Validation Warning: [ VUID-Smoke-00001 ] synthetic' >&2
SH
  chmod +x renderer
  printf '[project]\nname="engine"\ncommand="./renderer"\nheadless="none"\nrenderer="hardware"\n\n[performance]\nmax_gpu_ms=4\nmax_process_ms=10000\n' > rendercheck.toml
  RENDERCHECK_HEADLESS_AUTO=0 "$BIN" run >/dev/null 2>/dev/null
  grep -q '"gpu_max_ms": 3.000' .rendercheck/results.json
)
rm -rf "$tmp"

if [ "$(uname -s)" = Linux ]; then
  tmp=$(mktemp -d)
  (
    mkdir -p "$tmp/bin"
    cat > "$tmp/bin/xvfb-run" <<'SH'
#!/bin/sh
[ "$1" = "-a" ] && shift
export DISPLAY=:99
exec "$@"
SH
    printf '#!/bin/sh\nexit 0\n' > "$tmp/bin/Xvfb"
    chmod +x "$tmp/bin/xvfb-run" "$tmp/bin/Xvfb"
    cd "$tmp"
    cat > renderer <<'SH'
#!/bin/sh
[ "$LIBGL_ALWAYS_SOFTWARE" = 1 ]
[ "$RENDERCHECK_HEADLESS_BACKEND" = xvfb ]
printf 'gpu_ms=999.000\n' >> "$RENDERCHECK_METRICS_PATH"
SH
    chmod +x renderer
    printf '[project]\nname="headless"\ncommand="./renderer"\n\n[performance]\nmax_gpu_ms=1\n' > rendercheck.toml
    DISPLAY= WAYLAND_DISPLAY= PATH="$tmp/bin:$PATH" "$BIN" run >/dev/null
    grep -q '"renderer_mode": "software"' .rendercheck/results.json
    grep -q '"timing_kind": "software_render"' .rendercheck/results.json

    printf '[project]\nname="hardware"\ncommand="true"\nrenderer="hardware"\nheadless="auto"\n' > rendercheck.toml
    if DISPLAY= WAYLAND_DISPLAY= LIBGL_ALWAYS_SOFTWARE= RENDERCHECK_SOFTWARE_RENDERER= PATH="$tmp/bin:$PATH" "$BIN" run >/dev/null 2>&1; then
      echo 'hardware mode unexpectedly downgraded to Xvfb software rendering' >&2
      exit 1
    fi
    grep -q 'hardware renderer requested but Xvfb fallback uses Mesa software rendering' .rendercheck/report.md
  )
  rm -rf "$tmp"
fi

tmp=$(mktemp -d)
(
  cd "$tmp"
  printf '[project]\nname="engine"\ncommand="true"\n\n[[test]]\nname="a/b"\n\n[[test]]\nname="a?b"\n' > rendercheck.toml
  RENDERCHECK_HEADLESS_AUTO=0 "$BIN" run >/dev/null
  test "$(find .rendercheck -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')" -eq 2
)
rm -rf "$tmp"

echo 'All smoke tests passed'
