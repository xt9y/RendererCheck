#include <GLFW/glfw3.h>
#include <rendercheck/capture.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int capture_frame(uint64_t frame)
{
    if (!rendercheck_capture_due(frame)) return 0;

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(glfwGetCurrentContext(), &width, &height);
    if (width <= 0 || height <= 0) return -1;

    const size_t row_bytes = (size_t)width * 3u;
    unsigned char *pixels = (unsigned char *)malloc(row_bytes * (size_t)height);
    if (!pixels) return -1;

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels);

    for (int y = 0; y < height / 2; ++y) {
        unsigned char *top = pixels + (size_t)y * row_bytes;
        unsigned char *bottom = pixels + (size_t)(height - 1 - y) * row_bytes;
        for (size_t x = 0; x < row_bytes; ++x) {
            const unsigned char tmp = top[x];
            top[x] = bottom[x];
            bottom[x] = tmp;
        }
    }

    const int result = rendercheck_capture_rgb8(
        pixels, (uint32_t)width, (uint32_t)height, row_bytes);
    free(pixels);
    return result < 0 ? -1 : 0;
}

int main(void)
{
    if (!glfwInit()) {
        fprintf(stderr, "glfwInit failed\n");
        return 2;
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

    GLFWwindow *window = glfwCreateWindow(320, 180, "RendererCheck OpenGL", NULL, NULL);
    if (!window) {
        fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return 3;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);

    const uint64_t frame_limit = rendercheck_frame_limit();
    for (uint64_t frame = 0; frame < frame_limit; ++frame) {
        glViewport(0, 0, 320, 180);
        glClearColor(0.07f, 0.10f, 0.16f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_SCISSOR_TEST);
        glScissor(32, 24, 112, 72);
        glClearColor(0.18f, 0.55f, 0.95f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(182, 66, 88, 82);
        glClearColor(0.95f, 0.32f, 0.22f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        glFinish();

        if (capture_frame(frame) != 0) {
            fprintf(stderr, "failed to capture OpenGL framebuffer\n");
            glfwDestroyWindow(window);
            glfwTerminate();
            return 4;
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
