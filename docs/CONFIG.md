# Configuration

RendererCheck intentionally accepts a strict TOML subset. Unknown sections, unknown keys, duplicate keys, and duplicate test names are errors so a typo cannot silently disable a CI check.

## Project

```toml
[project]
name = "renderer"
command = "./build/app"
cwd = "."
baseline_dir = "rendercheck/baselines"
headless = "auto"        # auto | xvfb | none
renderer = "auto"        # auto | hardware | software
timeout_ms = 30000.0      # execution timeout; 0 disables it
capture = false
pixel_threshold = 0
max_changed_percent = 0.0
warmup_frames = 0
```

`timeout_ms` stops hung processes. `max_process_ms` is only a performance budget and is evaluated after a normal process run.

`warmup_frames = N` asks integrated renderers to render N frames before the captured frame. RendererCheck exports `RENDERCHECK_FRAME_LIMIT` and `RENDERCHECK_CAPTURE_FRAME`; `<rendercheck/capture.h>` exposes helpers for both.

## Validation

```toml
[validation]
vulkan = false
fail_on_error = true
fail_on_warning = false
```

When Vulkan validation is enabled, RendererCheck preflights `VK_LAYER_KHRONOS_validation`. Missing validation is a failure, not a clean result.

## Performance

```toml
[performance]
max_gpu_ms = 16.67
max_process_ms = 1000.0
```

Hardware GPU budgets require `gpu_ms` samples. They are skipped when RendererCheck knows the run is using a software renderer.

## Tests

Project visual settings and performance settings are defaults. Tests override only fields they explicitly set.

```toml
[[test]]
name = "triangle"
args = "--scene triangle"
capture = true
warmup_frames = 30
pixel_threshold = 1
max_changed_percent = 0.01
max_gpu_ms = 16.67
timeout_ms = 10000.0
```
