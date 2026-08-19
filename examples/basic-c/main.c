#include <rendercheck/capture.h>
#include <rendercheck/metrics.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define WIDTH 64
#define HEIGHT 64

int main(void) {
    static uint8_t pixels[WIDTH * HEIGHT * 3];
    for (uint32_t y = 0; y < HEIGHT; ++y) {
        for (uint32_t x = 0; x < WIDTH; ++x) {
            const size_t i = ((size_t)y * WIDTH + x) * 3u;
            pixels[i + 0] = (uint8_t)(x * 4u);
            pixels[i + 1] = (uint8_t)(y * 4u);
            pixels[i + 2] = (uint8_t)((x ^ y) * 4u);
        }
    }
    if (getenv("RENDERCHECK_EXAMPLE_MUTATE")) pixels[0] ^= 0xffu;
    if (rendercheck_capture_rgb8(pixels, WIDTH, HEIGHT, 0) < 0) {
        fprintf(stderr, "failed to write RendererCheck capture\n");
        return 2;
    }
    if (rendercheck_gpu_ms(2.25) < 0 || rendercheck_metric("draw_calls", 3.0) < 0) {
        fprintf(stderr, "failed to write RendererCheck metric\n");
        return 3;
    }
    return 0;
}
