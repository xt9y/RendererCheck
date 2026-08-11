# RendererCheck — Graphics CI for native renderers

Test Vulkan and native graphics projects from the terminal before visual regressions reach a release.

RendererCheck is a small, engine-agnostic test runner for renderer correctness, Vulkan validation, image baselines, performance regressions, and GPU compatibility.

## Build

```bash
make
```

The binary is written to `build/rendercheck`.

## Requirements

- **macOS** or **Linux**
- C++20 compiler (`clang++` or `g++`)
- Vulkan loader or MoltenVK for Vulkan diagnostics

No Vulkan SDK headers or third-party libraries are required to build the current version.

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

[validation]
vulkan = true
fail_on_warning = false
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
```

### Doctor

`rendercheck doctor` checks the machine before a renderer test is started:

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

### Run

With no `[[test]]` entries, `rendercheck run` executes `[project].command` once and forwards its stdout/stderr directly to the terminal.

Tests can provide their own arguments or command:

```toml
[project]
name = "engine"
command = "./build/app"

[[test]]
name = "triangle"
args = "--scene tests/triangle.scene --headless"

[[test]]
name = "shadows"
args = "--scene tests/shadows.scene --headless"
```

```bash
rendercheck run
rendercheck run shadows
```

Every child process receives:

```text
RENDERCHECK=1
RENDERCHECK_TEST=<test name>
RENDERCHECK_OUTPUT_DIR=<absolute output directory>
```

The output directory is `.rendercheck/<test>/`. This is the contract future framebuffer capture and metrics support will use.

A non-zero renderer exit code fails the test and RendererCheck itself returns non-zero, so the command works directly in CI.

## Current status

### Works

- compact C++20 CLI with no third-party dependencies
- macOS and Linux build targets
- `rendercheck init`
- `rendercheck run [test]`
- small TOML subset for project/test configuration
- renderer exit-code propagation for CI
- per-test `.rendercheck/` output directories and environment contract
- runtime Vulkan/MoltenVK loader discovery
- Vulkan version/layer/extension diagnostics without Vulkan headers
- validation-layer detection
- environment path diagnostics
- macOS + Linux GitHub Actions smoke build

### Not implemented yet

- framebuffer capture
- PNG baseline storage
- image comparison and diff output
- Vulkan validation message collection from child renderers
- GPU timing regression tests
- GitHub PR reports
- Metal backend
- remote GPU test workers

## Roadmap

1. image capture contract and baseline format
2. image diff engine
3. validation-layer capture
4. performance thresholds
5. GitHub Actions reports and artifacts
6. Metal diagnostics
7. multi-GPU test matrix

The first supported rendering backend is Vulkan on Linux and Vulkan through MoltenVK on macOS.

## Project layout

```text
RendererCheck/
├── include/rendercheck/
│   ├── config.h
│   ├── doctor.h
│   ├── run.h
│   ├── version.h
│   └── vulkan_min.h
├── src/
│   ├── config.cpp
│   ├── doctor.cpp
│   ├── main.cpp
│   └── run.cpp
├── .github/workflows/build.yml
├── Makefile
└── README.md
```

## License

MIT
