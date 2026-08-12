#ifndef RENDERCHECK_METRICS_H
#define RENDERCHECK_METRICS_H

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline const char* rendercheck_metrics_path(void) {
    const char* path = getenv("RENDERCHECK_METRICS_PATH");
    return (path && *path) ? path : NULL;
}

static inline int rendercheck_metric_name_valid(const char* name) {
    if (!name || !*name) return 0;
    for (const unsigned char* p = (const unsigned char*)name; *p; ++p) {
        if (!(isalnum(*p) || *p == '_' || *p == '-' || *p == '.')) return 0;
    }
    return 1;
}

static inline int rendercheck_metric(const char* name, double value) {
    const char* path = rendercheck_metrics_path();
    if (!path) return 0;
    if (!rendercheck_metric_name_valid(name)) return -1;

    FILE* file = fopen(path, "a");
    if (!file) return -1;

    const int written = fprintf(file, "%s=%.9f\n", name, value);
    if (fclose(file) != 0) return -1;
    return written > 0 ? 1 : -1;
}

static inline int rendercheck_gpu_ms(double milliseconds) {
    return rendercheck_metric("gpu_ms", milliseconds);
}

#ifdef __cplusplus
}
#endif

#endif
