# RendererCheck — Graphics CI for native renderers

Test Vulkan and native graphics projects from the terminal before visual regressions or GPU-performance regressions reach a release.

RendererCheck is a small, engine-agnostic test runner for renderer correctness, Vulkan validation, deterministic frame baselines, image diffs, performance budgets, headless execution, and CI reports.

## Install

```bash
git clone https://github.com/xt9y/RendererCheck.git
cd RendererCheck
make
sudo make install
```

This installs:

- `rendercheck` to `/usr/local/bin`
- `rendercheck/capture.h` to `/usr/local/include`
- `rendercheck/metrics.h` to `/usr/local/include`

A custom prefix is supported:

```bash
make
sudo make install PREFIX=/opt/rendercheck
```

Uninstall with:

```bash
sudo make uninstall
```

The build binary remains available at `build/rendercheck`.

## Requirements

- **macOS** or **Linux**
- C++20 compiler (`clang++` or `g++`)
- Vulkan loader or MoltenVK when Vulkan validation is enabled
- On display-less Linux machines, `xvfb-run` + `Xvfb` for automatic window-system emulation
- Mesa software OpenGL drivers for graphics tests on machines without a usable GPU/display stack

RendererCheck itself has no third-party build dependencies and requires no Vulkan SDK headers.

On Alpine Linux, a typical headless OpenGL setup is:

```bash
apk add xvfb xvfb-run mesa-gl mesa-dri-gallium
```

## Setup

```bash
rendercheck init
```

This creates `rendercheck.toml` with project, validation, and performance sections.

```toml
[project]
name = "renderer"
command = "./build/app"
cwd = "."
baseline_dir = "rendercheck/baselines"

[validation]
vulkan = true
fail_on_error = true
fail_on_warning = false

[performance]
# max_gpu_ms = 16.67
# max_process_ms = 1000.0

[[test]]
name = "triangle"
args = "--scene tests/triangle.scene --headless"
capture = true
pixel_threshold = 0
max_changed_percent = 0.0
max_gpu_ms = 16.67
```

## Usage

```bash
rendercheck help
rendercheck version
rendercheck doctor
rendercheck doctor --verbose
rendercheck init
rendercheck run
rendercheck run triangle
rendercheck diff triangle
rendercheck approve triangle
```

## Automatic Linux headless execution

`rendercheck run` automatically handles the common CI/server case where a GLFW/SDL/OpenGL application needs a display but neither `DISPLAY` nor `WAYLAND_DISPLAY` exists.

On Linux, when no display is present and both `xvfb-run` and `Xvfb` are available, RendererCheck automatically launches the renderer through Xvfb and enables Mesa software rendering. The user still runs the normal command:

```bash
rendercheck run
```

The child process receives:

```text
RENDERCHECK_HEADLESS=1
RENDERCHECK_HEADLESS_BACKEND=xvfb
RENDERCHECK_SOFTWARE_RENDERER=1
LIBGL_ALWAYS_SOFTWARE=1
```

If a real X11 or Wayland display already exists, RendererCheck does not add Xvfb. If `LIBGL_ALWAYS_SOFTWARE=1` is already present, RendererCheck recognizes the run as software-rendered even when the display was supplied externally.

Automatic Xvfb wrapping can be disabled for troubleshooting:

```bash
RENDERCHECK_HEADLESS_AUTO=0 rendercheck run
```

If there is no display and the Xvfb tools are missing, RendererCheck prints that condition and runs the configured command directly rather than silently changing its behavior.

## Examples

Ready-to-run project integrations live in [`examples/`](examples/):

- [`examples/basic-c`](examples/basic-c) — dependency-free C renderer using framebuffer capture and GPU metrics
- [`examples/raylib`](examples/raylib) — raylib framebuffer capture with `LoadImageFromScreen()`

The basic C example is built and executed on both macOS and Linux in RendererCheck's own CI.

Typical first use:

```bash
cd examples/basic-c
make
rendercheck run
rendercheck approve gradient
rendercheck run
```

The first run intentionally fails because there is no reviewed baseline yet.

## Vulkan validation

When `[validation].vulkan = true`, RendererCheck asks the Vulkan loader to enable `VK_LAYER_KHRONOS_validation`, captures the child renderer's stderr to `.rendercheck/<test>/validation.log`, and scans validation output for errors, warnings, and VUIDs.

```toml
[validation]
vulkan = true
fail_on_error = true
fail_on_warning = false
```

Validation errors fail the test by default. Warnings are reported but only fail when `fail_on_warning = true`.

RendererCheck sets `RENDERCHECK_VULKAN_VALIDATION=1` for the child process. Existing Vulkan loader configuration is preserved where possible.

Use `rendercheck doctor` to verify that `VK_LAYER_KHRONOS_validation` is installed before relying on validation runs.

## GPU performance budgets

RendererCheck exposes:

```text
RENDERCHECK_METRICS_PATH=/absolute/path/.rendercheck/<test>/metrics.txt
```

A small C/C++ helper is installed:

```c
#include <rendercheck/metrics.h>

rendercheck_gpu_ms(gpu_elapsed_ms);
```

A renderer can submit one or many GPU timing samples. On a hardware-rendered run, RendererCheck reports the average and maximum sample and `max_gpu_ms` compares against the maximum reported sample.

```toml
[performance]
max_gpu_ms = 16.67
max_process_ms = 1000.0
```

Thresholds can be overridden per test:

```toml
[[test]]
name = "shadows"
max_gpu_ms = 8.0
max_process_ms = 250.0
```

When RendererCheck automatically falls back to Xvfb + Mesa software rendering, GPU timer samples are still preserved but are reported as **software render timing**, not hardware GPU timing. `max_gpu_ms` is skipped in that mode because comparing llvmpipe/software-rasterizer timing against a GPU budget is not meaningful. `max_process_ms` remains enforced because it is a real wall-clock process budget.

If `max_gpu_ms` is configured on a hardware run but the renderer reports no `gpu_ms` samples, the test fails instead of silently skipping the performance check.

Reports retain the existing `gpu_*` JSON fields for compatibility and additionally emit `timing_kind` as either `gpu` or `software_render`.

## Visual regression tests

Set `capture = true` on a test. RendererCheck exposes a deterministic capture target:

```text
RENDERCHECK=1
RENDERCHECK_TEST=triangle
RENDERCHECK_OUTPUT_DIR=/absolute/path/.rendercheck/triangle
RENDERCHECK_CAPTURE_PATH=/absolute/path/.rendercheck/triangle/actual.ppm
RENDERCHECK_CAPTURE_FORMAT=ppm-rgb8
```

The renderer writes one RGB8 frame to `RENDERCHECK_CAPTURE_PATH`. A small C/C++ helper is included:

```cpp
#include <rendercheck/capture.h>

rendercheck_capture_rgba8(pixels, width, height, 0);
```

The first run intentionally fails when no baseline exists. After checking the capture:

```bash
rendercheck approve triangle
```

Future runs compare the new capture with the committed baseline. `pixel_threshold` ignores per-channel RGB8 differences up to the configured value and `max_changed_percent` controls how many pixels may exceed it.

```toml
[[test]]
name = "pbr"
capture = true
pixel_threshold = 2
max_changed_percent = 0.05
```

Failed visual comparisons write `.rendercheck/<test>/diff.ppm`. Passing comparisons remove stale diff images.

Test names containing filesystem-unsafe characters receive a stable hash suffix, preventing different names such as `a/b` and `a?b` from sharing artifacts or baselines.

## CI reports

Every `rendercheck run` writes:

```text
.rendercheck/report.md
.rendercheck/results.json
.rendercheck/<test>/validation.log
.rendercheck/<test>/metrics.txt
.rendercheck/<test>/actual.ppm
.rendercheck/<test>/diff.ppm
```

Files that do not apply to a test are simply absent. Software-rendered runs also contain `.rendercheck/<test>/software-renderer` so timing semantics remain explicit when reports are generated.

On GitHub Actions, RendererCheck detects `GITHUB_STEP_SUMMARY` and appends the Markdown result table automatically, so pass/fail, timing, validation counts, and visual changes appear in the workflow summary.

Upload the complete artifact bundle with:

```yaml
- name: RendererCheck
  run: rendercheck run

- name: Upload RendererCheck artifacts
  if: always()
  uses: actions/upload-artifact@v4
  with:
    name: renderercheck-${{ runner.os }}
    path: .rendercheck/
    if-no-files-found: ignore
```

## Doctor

`rendercheck doctor` checks:

- operating system and architecture
- X11/Wayland display availability on Linux
- automatic Xvfb fallback availability
- whether software rendering is forced or will be selected automatically
- Vulkan/MoltenVK loader availability
- Vulkan loader API version
- instance layers and extensions
- `VK_LAYER_KHRONOS_validation`
- `vulkaninfo` availability
- broken `VK_LAYER_PATH`, `VK_ICD_FILENAMES`, and `VK_DRIVER_FILES` entries

When the current `rendercheck.toml` has `vulkan = false`, a missing Vulkan loader is reported as an optional warning rather than making `doctor` fail an otherwise usable OpenGL/headless environment.

## Current status

### Works

- compact C++20 CLI with no third-party build dependencies
- macOS and Linux build targets
- `rendercheck init`
- `rendercheck run [test]`
- automatic Xvfb + Mesa software fallback on display-less Linux
- software-render timing detection with hardware GPU-budget suppression
- `rendercheck diff [test]`
- `rendercheck approve [test]`
- deterministic RGB8 PPM capture/baseline contract
- C/C++ RGB8 and RGBA8 capture helper
- image tolerance, RMSE, changed-pixel percentage, and diff images
- Vulkan validation-layer activation and child stderr collection
- validation error/warning/VUID reporting and CI failure policy
- C/C++ GPU timing metrics helper
- per-project and per-test GPU/process performance budgets
- Markdown and JSON run reports
- automatic GitHub Actions job summaries
- artifact-ready `.rendercheck/` output bundle
- stable collision-resistant test artifact names
- dependency-free C and raylib project examples
- runtime Vulkan/MoltenVK diagnostics
- configuration-aware headless/Vulkan doctor output
- macOS + Linux GitHub Actions smoke build

### Not implemented yet

- PNG capture/baselines
- Metal diagnostics/backend-specific validation
- multi-GPU execution matrix
- remote GPU test workers
- automated GitHub PR comments

## Roadmap

1. PNG capture and baseline support
2. Metal diagnostics
3. local multi-GPU selection/matrix
4. remote GPU test workers
5. optional GitHub PR comment publishing

RendererCheck remains engine-agnostic: the application owns frame capture and metric submission while RendererCheck owns execution, environment setup, validation policy, baseline comparison, and reporting.

## License

MIT
