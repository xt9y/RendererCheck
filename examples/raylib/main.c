#include <raylib.h>
#include <rendercheck/capture.h>

#include <stdint.h>
#include <stdio.h>

int main(void) {
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(640, 360, "RendererCheck raylib example");

    BeginDrawing();
    ClearBackground((Color){18, 20, 28, 255});
    DrawRectangle(80, 70, 220, 140, (Color){64, 128, 255, 255});
    DrawCircle(430, 180, 72.0f, (Color){255, 96, 96, 255});
    DrawLine(40, 320, 600, 40, (Color){240, 220, 120, 255});
    EndDrawing();

    Image image = LoadImageFromScreen();
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

    const int capture_result = rendercheck_capture_rgba8(
        (const uint8_t*)image.data,
        (uint32_t)image.width,
        (uint32_t)image.height,
        0);

    UnloadImage(image);
    CloseWindow();

    if (capture_result < 0) {
        fprintf(stderr, "failed to write RendererCheck capture\n");
        return 2;
    }

    return 0;
}
