# RendererCheck

Your renderer compiling does not mean the frame is right.

- Run native graphics projects in CI.
- Headless Linux runs.
- Capture and compare rendered frames.
- Catch Vulkan validation errors.
- Catch performance regressions without pretending CPU time is GPU time.
- Keep machine-readable and human-readable reports from every run.

## Install

```bash
git clone https://github.com/xt9y/RendererCheck.git
cd RendererCheck
make
sudo make install
```

Then:

```bash
renderercheck init
renderercheck run
```

Docs: https://xt9y.de/rendercheck.html

Backend examples and regression fixtures are in [`examples/`](examples/): C, surfaceless EGL/OpenGL ES, GLFW/OpenGL, raylib, SDL3, Vulkan validation, and Metal.

MIT
