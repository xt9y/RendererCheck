#pragma once

#include <cstdint>

namespace rendercheck::vkmin {
using VkResult = std::int32_t;
using VkInstance = void*;
using PFN_vkVoidFunction = void (*)();
constexpr VkResult VK_SUCCESS = 0;
constexpr std::uint32_t VK_MAX_EXTENSION_NAME_SIZE = 256;
constexpr std::uint32_t VK_MAX_DESCRIPTION_SIZE = 256;
struct VkExtensionProperties { char extensionName[VK_MAX_EXTENSION_NAME_SIZE]; std::uint32_t specVersion; };
struct VkLayerProperties {
    char layerName[VK_MAX_EXTENSION_NAME_SIZE];
    std::uint32_t specVersion;
    std::uint32_t implementationVersion;
    char description[VK_MAX_DESCRIPTION_SIZE];
};
using PFN_vkGetInstanceProcAddr = PFN_vkVoidFunction (*)(VkInstance, const char*);
using PFN_vkEnumerateInstanceVersion = VkResult (*)(std::uint32_t*);
using PFN_vkEnumerateInstanceLayerProperties = VkResult (*)(std::uint32_t*, VkLayerProperties*);
using PFN_vkEnumerateInstanceExtensionProperties = VkResult (*)(const char*, std::uint32_t*, VkExtensionProperties*);
constexpr std::uint32_t version_major(std::uint32_t version) { return version >> 22U; }
constexpr std::uint32_t version_minor(std::uint32_t version) { return (version >> 12U) & 0x3ffU; }
constexpr std::uint32_t version_patch(std::uint32_t version) { return version & 0xfffU; }
} // namespace rendercheck::vkmin
