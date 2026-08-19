#ifndef RENDERCHECK_CAPTURE_H
#define RENDERCHECK_CAPTURE_H

#include <errno.h>
#include <limits.h>
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

static inline uint64_t rendercheck_env_u64(const char* name, uint64_t fallback) {
    const char* raw = getenv(name);
    if (!raw || !*raw || *raw == '-') return fallback;
    char* end = NULL;
    errno = 0;
    const unsigned long long value = strtoull(raw, &end, 10);
    if (errno != 0 || !end || *end != '\0') return fallback;
    return (uint64_t)value;
}

static inline uint64_t rendercheck_capture_frame_index(void) {
    return rendercheck_env_u64("RENDERCHECK_CAPTURE_FRAME", 0);
}

static inline uint64_t rendercheck_frame_limit(void) {
    const uint64_t limit = rendercheck_env_u64("RENDERCHECK_FRAME_LIMIT", 1);
    return limit == 0 ? 1 : limit;
}

static inline int rendercheck_capture_due(uint64_t frame_index) {
    return rendercheck_capture_requested() && frame_index == rendercheck_capture_frame_index();
}

static inline int rendercheck_frame_is_last(uint64_t frame_index) {
    const uint64_t limit = rendercheck_frame_limit();
    return frame_index >= limit - 1u;
}

static inline int rendercheck_rgb_row_bytes(uint32_t width, size_t channels, size_t* out) {
    if (!out || channels == 0) return 0;
    if ((size_t)width > SIZE_MAX / channels) return 0;
    *out = (size_t)width * channels;
    return 1;
}

static inline int rendercheck_capture_rgb8(const uint8_t* pixels,
                                            uint32_t width,
                                            uint32_t height,
                                            size_t stride_bytes) {
    const char* path = rendercheck_capture_path();
    if (!path) return 0;
    if (!pixels || width == 0 || height == 0) return -1;

    size_t row_bytes = 0;
    if (!rendercheck_rgb_row_bytes(width, 3u, &row_bytes)) return -1;
    if (stride_bytes == 0) stride_bytes = row_bytes;
    if (stride_bytes < row_bytes) return -1;
    if ((size_t)height > SIZE_MAX / stride_bytes) return -1;

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

    size_t source_row_bytes = 0;
    size_t output_row_bytes = 0;
    if (!rendercheck_rgb_row_bytes(width, 4u, &source_row_bytes) ||
        !rendercheck_rgb_row_bytes(width, 3u, &output_row_bytes)) return -1;
    if (stride_bytes == 0) stride_bytes = source_row_bytes;
    if (stride_bytes < source_row_bytes) return -1;
    if ((size_t)height > SIZE_MAX / stride_bytes) return -1;

    uint8_t* output_row = (uint8_t*)malloc(output_row_bytes);
    if (!output_row) return -1;
    FILE* file = fopen(path, "wb");
    if (!file) {
        free(output_row);
        return -1;
    }
    if (fprintf(file, "P6\n%u %u\n255\n", width, height) < 0) {
        fclose(file);
        free(output_row);
        return -1;
    }
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* source = pixels + (size_t)y * stride_bytes;
        for (uint32_t x = 0; x < width; ++x) {
            output_row[(size_t)x * 3u + 0u] = source[(size_t)x * 4u + 0u];
            output_row[(size_t)x * 3u + 1u] = source[(size_t)x * 4u + 1u];
            output_row[(size_t)x * 3u + 2u] = source[(size_t)x * 4u + 2u];
        }
        if (fwrite(output_row, 1, output_row_bytes, file) != output_row_bytes) {
            fclose(file);
            free(output_row);
            return -1;
        }
    }
    const int close_result = fclose(file);
    free(output_row);
    return close_result == 0 ? 1 : -1;
}

#ifdef __cplusplus
}
#endif

#endif
