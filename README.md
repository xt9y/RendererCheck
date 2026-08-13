# RendererCheck

Your renderer compiling does not mean the frame is right.

- Run native graphics projects in CI.
- Headless Linux runs.
- Capture rendered frames.
- Compare them against approved baselines.
- Catch validation errors.
- Catch performance regressions.
- Keep a report from every run.

## Why

- Normal CI can tell me that my renderer compiled.
- It cannot tell me that I broke the image.
- I wanted that check too.
- So I built RendererCheck.
- I use it on my own graphics projects.

## Real use

- [BGE](https://github.com/xt9y/BGE) uses RendererCheck for runtime and visual checks.
- `examples/basic-c` shows the smallest setup.
- `examples/raylib` shows a real framebuffer capture.

## Install

```bash
git clone https://github.com/xt9y/RendererCheck.git
cd RendererCheck
make
sudo make install
```

Then:

```bash
rendercheck init
rendercheck run
```

Docs (Thanks to AI): https://xt9y.de/rendercheck.html

## Notes

- This is a young project.
- I am still finding graphics edge cases.
- If it behaves strangely on your renderer, open an issue.

## License

MIT
