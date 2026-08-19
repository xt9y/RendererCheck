# Vulkan validation

A real Vulkan validation-layer integration fixture.

```bash
make
renderercheck run clean
RENDERCHECK_VK_INVALID=1 renderercheck run clean   # must fail with a real VUID
```
