#include <vulkan/vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const char* validation_layer = "VK_LAYER_KHRONOS_validation";
    const int validation = getenv("RENDERCHECK_VULKAN_VALIDATION") != NULL;

    VkApplicationInfo app = {0};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "RendererCheck Vulkan validation example";
    app.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo ici = {0};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    if (validation) {
        ici.enabledLayerCount = 1;
        ici.ppEnabledLayerNames = &validation_layer;
    }

    VkInstance instance = VK_NULL_HANDLE;
    VkResult vr = vkCreateInstance(&ici, NULL, &instance);
    if (vr != VK_SUCCESS) {
        fprintf(stderr, "vkCreateInstance failed: %d\n", (int)vr);
        return 2;
    }

    uint32_t physical_count = 0;
    vr = vkEnumeratePhysicalDevices(instance, &physical_count, NULL);
    if (vr != VK_SUCCESS || physical_count == 0) {
        fprintf(stderr, "no Vulkan physical device available\n");
        vkDestroyInstance(instance, NULL);
        return 3;
    }

    VkPhysicalDevice physical = VK_NULL_HANDLE;
    vkEnumeratePhysicalDevices(instance, &physical_count, &physical);

    uint32_t queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, NULL);
    if (queue_count == 0) {
        vkDestroyInstance(instance, NULL);
        return 4;
    }
    VkQueueFamilyProperties* queues = calloc(queue_count, sizeof(*queues));
    if (!queues) {
        vkDestroyInstance(instance, NULL);
        return 5;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, queues);
    uint32_t family = 0;
    while (family < queue_count && queues[family].queueCount == 0) ++family;
    free(queues);
    if (family == queue_count) {
        vkDestroyInstance(instance, NULL);
        return 6;
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {0};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = family;
    qci.queueCount = getenv("RENDERCHECK_VK_INVALID") ? 0u : 1u;
    qci.pQueuePriorities = &priority;

    VkDeviceCreateInfo dci = {0};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;

    VkDevice device = VK_NULL_HANDLE;
    vr = vkCreateDevice(physical, &dci, NULL, &device);
    if (device != VK_NULL_HANDLE) vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);

    return getenv("RENDERCHECK_VK_INVALID") ? 0 : (vr == VK_SUCCESS ? 0 : 7);
}
