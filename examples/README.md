# RendererCheck examples

These examples show the integration points a renderer needs in order to work with RendererCheck.

## basic-c

A dependency-free C example that produces a deterministic RGB8 frame and submits a synthetic `gpu_ms` sample through the public RendererCheck helpers. It is compiled as part of `make test`.

```bash
cd examples/basic-c
make
rendercheck run
rendercheck approve gradient
rendercheck run
```

The first `rendercheck run` intentionally fails because no baseline exists yet. `rendercheck approve gradient` stores the reviewed frame, and the next run compares against it.

## raylib

A small raylib project that captures the rendered framebuffer through `rendercheck_capture_rgba8()`.

```bash
cd examples/raylib
make
rendercheck run
rendercheck approve raylib-frame
rendercheck run
```

The raylib example does not report `gpu_ms` because raylib's high-level API does not expose backend GPU timestamp queries. A Vulkan, Metal, or other backend integration should submit real GPU timestamp measurements with `rendercheck_gpu_ms()` rather than substituting CPU frame time.

Both examples include their own `rendercheck.toml` so they can be used as templates for another project.
