#include <raylib.h>
#include <rendercheck/capture.h>

#include <stdint.h>
#include <stdio.h>

int main(void) {
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(640, 360, "RendererCheck raylib example");

    const uint64_t frame_limit = rendercheck_frame_limit();
    for (uint64_t frame = 0; frame < frame_limit; ++frame) {
        BeginDrawing();
        ClearBackground((Color){18, 20, 28, 255});
        DrawRectangle(80, 70, 220, 140, (Color){64, 128, 255, 255});
        DrawCircle(430, 180, 72.0f, (Color){255, 96, 96, 255});
        DrawLine(40, 320, 600, 40, (Color){240, 220, 120, 255});
        EndDrawing();

        if (rendercheck_capture_due(frame)) {
            Image image = LoadImageFromScreen();
            ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            const int capture_result = rendercheck_capture_rgba8(
                (const uint8_t*)image.data, (uint32_t)image.width, (uint32_t)image.height, 0);
            UnloadImage(image);
            if (capture_result < 0) {
                fprintf(stderr, "failed to write RendererCheck capture\n");
                CloseWindow();
                return 2;
            }
        }
    }

    CloseWindow();
    return 0;
}
