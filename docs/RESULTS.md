# Results and exit codes

Every `renderercheck run` rewrites `.rendercheck/report.md` and `.rendercheck/results.json`, and recreates each selected test's artifact directory.

`results.json` has `schema_version: 1`. Each test includes process exit/signal/timeout state, renderer/headless classification, validation status, GPU/custom metric summaries, visual metrics, artifact paths, and a list of failure reasons.

Per-test artifacts can include:

- `stdout.log` / `stderr.log`
- `validation.log`
- `metrics.txt`
- `actual.ppm` / `actual.png`
- `baseline.png`
- `diff.ppm` / `diff.png`

Exit codes:

- `0`: selected checks passed.
- `1`: one or more selected checks failed.
- `2`: command/configuration/selection error.
- Renderer subprocess exit codes are reported inside the result schema. A RendererCheck execution timeout is recorded as subprocess exit code `124` with `timed_out: true`.
