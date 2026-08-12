# basic C example

This example is a minimal project showing both RendererCheck integration helpers:

- `rendercheck_capture_rgb8()` for deterministic framebuffer capture
- `rendercheck_gpu_ms()` for GPU timing samples

Build and establish a baseline:

```bash
make
rendercheck run
rendercheck approve gradient
rendercheck run
```

The first run is expected to fail because `baselines/gradient.ppm` does not exist yet. Review `.rendercheck/gradient/actual.ppm`, approve it, then future runs compare against that committed baseline.

The example's `max_gpu_ms = 4.0` budget passes because `main.c` submits a 2.25 ms sample. In a real renderer, replace that sample with a value measured by GPU timestamp queries.
