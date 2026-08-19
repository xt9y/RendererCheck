# basic-c

Dependency-free deterministic capture fixture. It is the repository's committed-baseline regression test.

```bash
make
renderercheck run gradient
```

`RENDERCHECK_EXAMPLE_MUTATE=1 renderercheck run gradient` intentionally changes one pixel and must fail.
