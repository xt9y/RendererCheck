# RendererCheck — Graphics CI for native renderers

Test Vulkan and native graphics projects from the terminal before visual regressions reach a release.

RendererCheck is a small, engine-agnostic test runner for renderer correctness, Vulkan diagnostics, deterministic frame baselines, image diffs, and eventually GPU performance regressions.

## Build

```bash
make
```

The binary is written to `build/rendercheck`.

## Requirements

- **macOS** or **Linux**
- C++20 compiler (`clang++` or `g++`)
- Vulkan loader or MoltenVK for Vulkan diagnostics

No Vulkan SDK headers or third-party libraries are required.

## Setup

Create a project configuration:

```bash
rendercheck init
```

This creates `rendercheck.toml`:

```toml
[project]
name = "renderer"
command = "./build/app"
cwd = "."
baseline_dir = "rendercheck/baselines"

[[test]]
name = "triangle"
args = "--scene tests/triangle.scene --headless"
capture = true
pixel_threshold = 0
max_changed_percent = 0.0
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

## Visual regression tests

Set `capture = true` on a test. RendererCheck then exposes a deterministic capture target to the renderer process:

```text
RENDERCHECK=1
RENDERCHECK_TEST=triangle
RENDERCHECK_OUTPUT_DIR=/absolute/path/.rendercheck/triangle
RENDERCHECK_CAPTURE_PATH=/absolute/path/.rendercheck/triangle/actual.ppm
RENDERCHECK_CAPTURE_FORMAT=ppm-rgb8
```

The renderer writes one RGB8 frame to `RENDERCHECK_CAPTURE_PATH`. RendererCheck compares it against the committed baseline after the process exits successfully.

A small C/C++ helper is included:

```cpp
#include <rendercheck/capture.h>

// pixels = tightly packed RGBA8 framebuffer
rendercheck_capture_rgba8(pixels, width, height, 0);
```

The first run has no baseline and intentionally fails:

```text
triangle
  [ok] process (12 ms)
  [fail] baseline missing: rendercheck/baselines/triangle.ppm
  actual: .rendercheck/triangle/actual.ppm
  approve: rendercheck approve triangle
```

After checking the captured frame:

```bash
rendercheck approve triangle
```

Future runs compare against that baseline:

```text
triangle
  [ok] process (11 ms)
  capture: 1280x720 PPM
  changed: 0.000% (0/921600 pixels)
  rmse: 0.000
  [ok] image matches baseline
```

A regression fails CI and writes `.rendercheck/<test>/diff.ppm`.

### Tolerance

`pixel_threshold` ignores per-channel differences up to the given RGB8 value. `max_changed_percent` controls how many pixels may exceed that threshold.

```toml
[[test]]
name = "pbr"
capture = true
pixel_threshold = 2
max_changed_percent = 0.05
```

A custom baseline path can be set per test:

```toml
baseline = "tests/baselines/pbr.ppm"
```

PPM/P6 is the first capture format because it is deterministic, trivial for native engines to emit, and keeps RendererCheck dependency-free. PNG support can be added later without changing the capture contract.

## Doctor

`rendercheck doctor` checks the machine before renderer tests are started:

```text
RendererCheck doctor

[ok]   platform: Darwin ... (arm64)
[ok]   target: macOS
[ok]   Vulkan loader: libvulkan.1.dylib
[ok]   vkGetInstanceProcAddr
[ok]   Vulkan API: 1.4.x
[ok]   instance layers: 4
[ok]   validation layers: VK_LAYER_KHRONOS_validation
[ok]   instance extensions: 17

Environment usable for RendererCheck's Vulkan bootstrap.
```

It currently checks:

- operating system and architecture
- Vulkan/MoltenVK loader availability
- Vulkan loader API version
- instance layers and extensions
- `VK_LAYER_KHRONOS_validation`
- `vulkaninfo` availability
- broken `VK_LAYER_PATH`, `VK_ICD_FILENAMES`, and `VK_DRIVER_FILES` entries

## Current status

### Works

- compact C++20 CLI with no third-party dependencies
- macOS and Linux build targets
- `rendercheck init`
- `rendercheck run [test]`
- `rendercheck diff [test]`
- `rendercheck approve [test]`
- small TOML subset for project/test configuration
- renderer exit-code propagation for CI
- deterministic RGB8 PPM capture contract
- header-only C/C++ RGB8 and RGBA8 capture helper
- committed per-test baselines
- per-channel tolerance and changed-pixel percentage threshold
- RMSE and maximum channel-delta reporting
- generated visual diff images
- runtime Vulkan/MoltenVK loader discovery
- validation-layer detection
- macOS + Linux GitHub Actions smoke build

### Not implemented yet

- PNG capture/baselines
- Vulkan validation message collection from child renderers
- GPU timing regression tests
- GitHub PR reports and image artifacts
- Metal backend
- remote GPU test workers

## Roadmap

1. validation-layer capture
2. GPU timing and performance thresholds
3. GitHub Actions reports and image artifacts
4. PNG support
5. Metal diagnostics
6. multi-GPU test matrix

The first supported rendering backend is Vulkan on Linux and Vulkan through MoltenVK on macOS.

## License

MIT
