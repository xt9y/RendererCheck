# Notes

- Test the rendered result, not just the build.
- A pass must never hide a missing check.
- Keep setup small and failures readable in a terminal.
- Keep every run's artifacts fresh and inspectable.
- Treat process timeout and process performance as different things.
- Do not pretend CPU or software-renderer timing is hardware GPU time.
- Do not try to be a GPU profiler or a game engine.
- Prefer deterministic capture points over huge visual tolerances.
- Test changes on real renderers. BGE is one of the integration projects.
