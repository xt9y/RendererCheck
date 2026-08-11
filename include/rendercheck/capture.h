#ifndef RENDERCHECK_CAPTURE_H
#define RENDERCHECK_CAPTURE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline const char* rendercheck_capture_path(void) {
    const char* path = getenv("RENDERCHECK_CAPTURE_PATH");
    return (path && *path) ? path : NULL;
}

static inline int rendercheck_capture_requested(void) {
    return rendercheck_capture_path() != NULL;
}

static inline int rendercheck_capture_rgb8(const uint8_t* pixels,
                                            uint32_t width,
                                            uint32_t height,
                                            size_t stride_bytes) {
    const char* path = rendercheck_capture_path();
    if (!path) return 0;
    if (!pixels || width == 0 || height == 0) return -1;

    const size_t row_bytes = (size_t)width * 3u;
    if (stride_bytes == 0) stride_bytes = row_bytes;
    if (stride_bytes < row_bytes) return -1;

    FILE* file = fopen(path, "wb");
    if (!file) return -1;

    if (fprintf(file, "P6\n%u %u\n255\n", width, height) < 0) {
        fclose(file);
        return -1;
    }

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = pixels + (size_t)y * stride_bytes;
        if (fwrite(row, 1, row_bytes, file) != row_bytes) {
            fclose(file);
            return -1;
        }
    }

    if (fclose(file) != 0) return -1;
    return 1;
}

static inline int rendercheck_capture_rgba8(const uint8_t* pixels,
                                             uint32_t width,
                                             uint32_t height,
                                             size_t stride_bytes) {
    const char* path = rendercheck_capture_path();
    if (!path) return 0;
    if (!pixels || width == 0 || height == 0) return -1;

    const size_t source_row_bytes = (size_t)width * 4u;
    if (stride_bytes == 0) stride_bytes = source_row_bytes;
    if (stride_bytes < source_row_bytes) return -1;

    FILE* file = fopen(path, "wb");
    if (!file) return -1;

    if (fprintf(file, "P6\n%u %u\n255\n", width, height) < 0) {
        fclose(file);
        return -1;
    }

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = pixels + (size_t)y * stride_bytes;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* pixel = row + (size_t)x * 4u;
            if (fwrite(pixel, 1, 3, file) != 3) {
                fclose(file);
                return -1;
            }
        }
    }

    if (fclose(file) != 0) return -1;
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif
