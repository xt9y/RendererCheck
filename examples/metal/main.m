#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <rendercheck/capture.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define WIDTH 320u
#define HEIGHT 180u

int main(void)
{
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            fprintf(stderr, "no Metal device available\n");
            return 2;
        }

        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue) return 3;

        MTLTextureDescriptor *descriptor =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                               width:WIDTH
                                                              height:HEIGHT
                                                           mipmapped:NO];
        descriptor.usage = MTLTextureUsageRenderTarget;
        descriptor.storageMode = MTLStorageModeShared;
        id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
        if (!texture) {
            fprintf(stderr, "could not create Metal render texture\n");
            return 4;
        }

        const uint64_t frame_limit = rendercheck_frame_limit();
        for (uint64_t frame = 0; frame < frame_limit; ++frame) {
            MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = texture;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0.08, 0.15, 0.32, 1.0);

            id<MTLCommandBuffer> command = [queue commandBuffer];
            id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
            [encoder endEncoding];
            [command commit];
            [command waitUntilCompleted];

            if (command.status == MTLCommandBufferStatusError) {
                fprintf(stderr, "Metal command failed: %s\n", command.error.localizedDescription.UTF8String);
                return 5;
            }

            if (rendercheck_capture_due(frame)) {
                const size_t row_bytes = (size_t)WIDTH * 4u;
                uint8_t *pixels = (uint8_t *)malloc(row_bytes * HEIGHT);
                if (!pixels) return 6;

                [texture getBytes:pixels
                      bytesPerRow:row_bytes
                       fromRegion:MTLRegionMake2D(0, 0, WIDTH, HEIGHT)
                      mipmapLevel:0];

                const int result = rendercheck_capture_rgba8(
                    pixels, WIDTH, HEIGHT, row_bytes);
                free(pixels);
                if (result < 0) {
                    fprintf(stderr, "failed to capture Metal texture\n");
                    return 7;
                }
            }
        }
    }
    return 0;
}
