#include <SDL3/SDL.h>
#include <rendercheck/capture.h>

#include <stdint.h>
#include <stdio.h>

static int capture_renderer(SDL_Renderer *renderer)
{
    SDL_Surface *surface = SDL_RenderReadPixels(renderer, NULL);
    if (!surface) {
        fprintf(stderr, "SDL_RenderReadPixels: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Surface *rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);
    if (!rgba) {
        fprintf(stderr, "SDL_ConvertSurface: %s\n", SDL_GetError());
        return -1;
    }

    const int result = rendercheck_capture_rgba8(
        (const uint8_t *)rgba->pixels,
        (uint32_t)rgba->w,
        (uint32_t)rgba->h,
        (size_t)rgba->pitch);
    SDL_DestroySurface(rgba);
    return result < 0 ? -1 : 0;
}

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 2;
    }

    SDL_Window *window = SDL_CreateWindow(
        "RendererCheck SDL3", 320, 180, SDL_WINDOW_HIDDEN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 3;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 4;
    }

    const uint64_t frame_limit = rendercheck_frame_limit();
    for (uint64_t frame = 0; frame < frame_limit; ++frame) {
        SDL_SetRenderDrawColor(renderer, 18, 26, 42, 255);
        SDL_RenderClear(renderer);

        SDL_FRect first = {32.0f, 26.0f, 116.0f, 70.0f};
        SDL_SetRenderDrawColor(renderer, 52, 148, 242, 255);
        SDL_RenderFillRect(renderer, &first);

        SDL_FRect second = {184.0f, 68.0f, 86.0f, 78.0f};
        SDL_SetRenderDrawColor(renderer, 242, 82, 56, 255);
        SDL_RenderFillRect(renderer, &second);

        if (rendercheck_capture_due(frame) && capture_renderer(renderer) != 0) {
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 5;
        }

        SDL_RenderPresent(renderer);
        SDL_PumpEvents();
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
