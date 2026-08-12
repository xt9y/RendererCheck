# raylib example

This example shows how a raylib application can hand its rendered framebuffer to RendererCheck.

Requirements:

- RendererCheck installed (`make && sudo make install` from the repository root)
- raylib
- `pkg-config`

Build and establish a baseline:

```bash
make
rendercheck run
rendercheck approve raylib-frame
rendercheck run
```

`rendercheck run` sets `RENDERCHECK_CAPTURE_PATH`. After drawing the frame, the example calls `LoadImageFromScreen()`, converts it to RGBA8, and passes the pixels to `rendercheck_capture_rgba8()`.

The example intentionally leaves Vulkan validation and GPU timing disabled. Standard raylib does not expose backend GPU timestamp-query results; do not report CPU frame time as `gpu_ms`. A lower-level renderer should use actual GPU timestamps and submit those through `rendercheck_gpu_ms()`.
