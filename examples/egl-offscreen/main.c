#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <rendercheck/capture.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define WIDTH 160
#define HEIGHT 96

static void destroy_egl(EGLDisplay display, EGLSurface surface, EGLContext context)
{
    if (display == EGL_NO_DISPLAY) return;
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
    if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
    eglTerminate(display);
}

static int capture_frame(uint64_t frame)
{
    if (!rendercheck_capture_due(frame)) return 0;

    const size_t row_bytes = (size_t)WIDTH * 4u;
    uint8_t *pixels = (uint8_t *)malloc(row_bytes * (size_t)HEIGHT);
    if (!pixels) return -1;

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    if (glGetError() != GL_NO_ERROR) {
        free(pixels);
        return -1;
    }

    for (int y = 0; y < HEIGHT / 2; ++y) {
        uint8_t *top = pixels + (size_t)y * row_bytes;
        uint8_t *bottom = pixels + (size_t)(HEIGHT - 1 - y) * row_bytes;
        for (size_t x = 0; x < row_bytes; ++x) {
            const uint8_t tmp = top[x];
            top[x] = bottom[x];
            bottom[x] = tmp;
        }
    }

    const int result = rendercheck_capture_rgba8(
        pixels, WIDTH, HEIGHT, row_bytes);
    free(pixels);
    return result < 0 ? -1 : 0;
}

int main(void)
{
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    if (display == EGL_NO_DISPLAY) {
        fprintf(stderr, "eglGetDisplay failed\n");
        return 2;
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (!eglInitialize(display, &major, &minor)) {
        fprintf(stderr, "eglInitialize failed: 0x%x\n", eglGetError());
        destroy_egl(display, surface, context);
        return 3;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "eglBindAPI failed: 0x%x\n", eglGetError());
        destroy_egl(display, surface, context);
        return 4;
    }

    const EGLint config_attributes[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig config = NULL;
    EGLint config_count = 0;
    if (!eglChooseConfig(display, config_attributes, &config, 1, &config_count) || config_count != 1) {
        fprintf(stderr, "eglChooseConfig failed: 0x%x\n", eglGetError());
        destroy_egl(display, surface, context);
        return 5;
    }

    const EGLint surface_attributes[] = {
        EGL_WIDTH, WIDTH,
        EGL_HEIGHT, HEIGHT,
        EGL_NONE
    };
    surface = eglCreatePbufferSurface(display, config, surface_attributes);
    if (surface == EGL_NO_SURFACE) {
        fprintf(stderr, "eglCreatePbufferSurface failed: 0x%x\n", eglGetError());
        destroy_egl(display, surface, context);
        return 6;
    }

    const EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
    if (context == EGL_NO_CONTEXT) {
        fprintf(stderr, "eglCreateContext failed: 0x%x\n", eglGetError());
        destroy_egl(display, surface, context);
        return 7;
    }
    if (!eglMakeCurrent(display, surface, surface, context)) {
        fprintf(stderr, "eglMakeCurrent failed: 0x%x\n", eglGetError());
        destroy_egl(display, surface, context);
        return 8;
    }

    const uint64_t frame_limit = rendercheck_frame_limit();
    for (uint64_t frame = 0; frame < frame_limit; ++frame) {
        glViewport(0, 0, WIDTH, HEIGHT);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.055f, 0.075f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_SCISSOR_TEST);
        glScissor(16, 14, 54, 52);
        glClearColor(0.15f, 0.72f, 0.42f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(91, 31, 48, 44);
        glClearColor(0.92f, 0.43f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        glFinish();

        if (capture_frame(frame) != 0) {
            fprintf(stderr, "failed to capture EGL framebuffer\n");
            destroy_egl(display, surface, context);
            return 9;
        }
    }

    destroy_egl(display, surface, context);
    return 0;
}
