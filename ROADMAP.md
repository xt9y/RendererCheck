# Roadmap

## 0.3

- [x] Real process timeout with process-group termination.
- [x] Strict configuration parsing and duplicate detection.
- [x] Project visual defaults inherited by tests.
- [x] Explicit headless/renderer policies.
- [x] Deterministic warm-up frame capture helpers.
- [x] PNG actual/baseline/diff diagnostics.
- [x] Versioned JSON results schema and per-test logs.
- [x] Committed-baseline regression fixture.
- [x] Surfaceless EGL/OpenGL ES integration fixture.
- [x] Raw GLFW/OpenGL integration fixture.
- [x] Real raylib capture integration in CI.
- [x] SDL3 renderer integration fixture.
- [x] Real Vulkan validation integration in CI.
- [x] macOS Metal offscreen integration fixture.
- [x] Sanitizer and warnings-as-errors CI coverage.
- [x] Descendant cleanup regression coverage for timeouts.

## Next

- Keep testing BGE and other real projects.
- Add a Wayland-specific fixture when it exposes behavior distinct from surfaceless EGL/Xvfb.
- Add more headless edge-case fixtures as real failures are found.
- Keep improving reports from real regression cases rather than adding profiler features.
