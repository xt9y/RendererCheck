#include "rendercheck/doctor.h"
#include "rendercheck/config.h"
#include "rendercheck/vulkan_min.h"

#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace rendercheck {
namespace {

struct SharedLibrary {
    void* handle = nullptr;
    std::string path;
    SharedLibrary() = default;
    SharedLibrary(const SharedLibrary&) = delete;
    SharedLibrary& operator=(const SharedLibrary&) = delete;
    SharedLibrary(SharedLibrary&& other) noexcept : handle(other.handle), path(std::move(other.path)) { other.handle = nullptr; }
    SharedLibrary& operator=(SharedLibrary&& other) noexcept {
        if (this != &other) {
#if defined(__APPLE__) || defined(__linux__)
            if (handle) dlclose(handle);
#endif
            handle = other.handle;
            path = std::move(other.path);
            other.handle = nullptr;
        }
        return *this;
    }
    ~SharedLibrary() {
#if defined(__APPLE__) || defined(__linux__)
        if (handle) dlclose(handle);
#endif
    }
};

void ok(std::string_view label, std::string_view detail = {}) {
    std::cout << "[ok]   " << label;
    if (!detail.empty()) std::cout << ": " << detail;
    std::cout << '\n';
}
void warn(std::string_view label, std::string_view detail = {}) {
    std::cout << "[warn] " << label;
    if (!detail.empty()) std::cout << ": " << detail;
    std::cout << '\n';
}
void fail(std::string_view label, std::string_view detail = {}) {
    std::cout << "[fail] " << label;
    if (!detail.empty()) std::cout << ": " << detail;
    std::cout << '\n';
}

std::string platform_string() {
#if defined(__APPLE__) || defined(__linux__)
    utsname u{};
    if (uname(&u) == 0) {
        std::ostringstream out;
        out << u.sysname << ' ' << u.release << " (" << u.machine << ')';
        return out.str();
    }
#endif
    return "unknown";
}

#if defined(__linux__)
bool env_has_value(const char* name) {
    const char* value = std::getenv(name);
    return value && *value;
}

bool env_truthy(const char* name) {
    const char* value = std::getenv(name);
    if (!value) return false;
    const std::string_view v(value);
    return v == "1" || v == "true" || v == "yes" || v == "on";
}
#endif

bool executable_in_path(const char* name) {
    const char* raw_path = std::getenv("PATH");
    if (!raw_path) return false;
    std::stringstream stream(raw_path);
    std::string dir;
    while (std::getline(stream, dir, ':')) {
        if (dir.empty()) dir = ".";
        const fs::path candidate = fs::path(dir) / name;
#if defined(__APPLE__) || defined(__linux__)
        if (::access(candidate.c_str(), X_OK) == 0) return true;
#else
        if (fs::exists(candidate)) return true;
#endif
    }
    return false;
}

void check_env_path_list(const char* name, bool expect_files) {
    const char* value = std::getenv(name);
    if (!value || !*value) return;
    std::stringstream stream(value);
    std::string entry;
    bool all_good = true;
    while (std::getline(stream, entry, ':')) {
        if (entry.empty()) continue;
        std::error_code ec;
        const fs::path p(entry);
        const bool good = expect_files ? fs::is_regular_file(p, ec) : fs::is_directory(p, ec);
        if (!good) { warn(name, std::string("missing path: ") + entry); all_good = false; }
    }
    if (all_good) ok(name, value);
}

SharedLibrary load_vulkan() {
    SharedLibrary lib;
#if defined(__APPLE__)
    const char* candidates[] = {
        "libvulkan.1.dylib", "libvulkan.dylib",
        "/opt/homebrew/lib/libvulkan.1.dylib", "/usr/local/lib/libvulkan.1.dylib",
        "/opt/homebrew/lib/libMoltenVK.dylib", "/usr/local/lib/libMoltenVK.dylib"
    };
#elif defined(__linux__)
    const char* candidates[] = {"libvulkan.so.1", "libvulkan.so"};
#else
    const char* candidates[] = {};
#endif
#if defined(__APPLE__) || defined(__linux__)
    for (const char* candidate : candidates) {
        void* handle = dlopen(candidate, RTLD_NOW | RTLD_LOCAL);
        if (handle) { lib.handle = handle; lib.path = candidate; return lib; }
    }
#endif
    return lib;
}

void* load_symbol(void* handle, const char* name) {
#if defined(__APPLE__) || defined(__linux__)
    return dlsym(handle, name);
#else
    (void)handle; (void)name; return nullptr;
#endif
}

std::string version_string(std::uint32_t version) {
    std::ostringstream out;
    out << vkmin::version_major(version) << '.' << vkmin::version_minor(version) << '.' << vkmin::version_patch(version);
    return out.str();
}

bool enumerate_validation_layer(SharedLibrary& vulkan, bool* found, std::uint32_t* layer_count, std::string& detail) {
    *found = false;
    *layer_count = 0;
    auto get_proc = reinterpret_cast<vkmin::PFN_vkGetInstanceProcAddr>(load_symbol(vulkan.handle, "vkGetInstanceProcAddr"));
    if (!get_proc) { detail = "vkGetInstanceProcAddr symbol missing"; return false; }
    auto enum_layers = reinterpret_cast<vkmin::PFN_vkEnumerateInstanceLayerProperties>(
        get_proc(nullptr, "vkEnumerateInstanceLayerProperties"));
    if (!enum_layers) { detail = "vkEnumerateInstanceLayerProperties unavailable"; return false; }
    std::uint32_t count = 0;
    if (enum_layers(&count, nullptr) != vkmin::VK_SUCCESS) { detail = "could not enumerate Vulkan instance layers"; return false; }
    std::vector<vkmin::VkLayerProperties> layers(count);
    if (count != 0 && enum_layers(&count, layers.data()) != vkmin::VK_SUCCESS) {
        detail = "could not read Vulkan instance layers"; return false;
    }
    *layer_count = count;
    for (const auto& layer : layers) {
        if (std::string_view(layer.layerName) == "VK_LAYER_KHRONOS_validation") *found = true;
    }
    return true;
}

} // namespace

bool vulkan_validation_available(std::string& detail) {
    SharedLibrary vulkan = load_vulkan();
    if (!vulkan.handle) { detail = "could not load Vulkan or MoltenVK"; return false; }
    bool found = false;
    std::uint32_t count = 0;
    if (!enumerate_validation_layer(vulkan, &found, &count, detail)) return false;
    if (!found) { detail = "VK_LAYER_KHRONOS_validation not found"; return false; }
    detail = "VK_LAYER_KHRONOS_validation available through " + vulkan.path;
    return true;
}

int run_doctor(bool verbose) {
    std::cout << "RendererCheck doctor\n\n";
    ok("platform", platform_string());
#if defined(__APPLE__)
    ok("target", "macOS");
#elif defined(__linux__)
    ok("target", "Linux");
#else
    warn("target", "unsupported platform");
#endif

    Config config;
    bool have_config = false;
    if (fs::is_regular_file("rendercheck.toml")) {
        std::string config_error;
        if (!load_config("rendercheck.toml", config, config_error)) {
            fail("config", config_error);
            std::cout << "\nEnvironment check failed because rendercheck.toml is invalid.\n";
            return 1;
        }
        have_config = true;
        ok("config", "rendercheck.toml");
        ok("headless policy", headless_mode_name(config.project.headless));
        ok("renderer policy", renderer_mode_name(config.project.renderer));
    }

#if defined(__linux__)
    const bool display_present = env_has_value("DISPLAY") || env_has_value("WAYLAND_DISPLAY");
    const bool xvfb_run = executable_in_path("xvfb-run");
    const bool xvfb_server = executable_in_path("Xvfb");
    const bool xvfb_ready = xvfb_run && xvfb_server;
    if (display_present) {
        if (env_has_value("WAYLAND_DISPLAY")) ok("display", std::string("Wayland ") + std::getenv("WAYLAND_DISPLAY"));
        else ok("display", std::string("X11 ") + std::getenv("DISPLAY"));
    } else if (xvfb_ready) {
        ok("headless display", "Xvfb fallback available");
    } else if (xvfb_run || xvfb_server) {
        warn("headless display", "incomplete Xvfb installation; both xvfb-run and Xvfb are required");
    } else {
        warn("headless display", "no display and Xvfb tooling not found");
    }
    if (have_config && config.project.headless == HeadlessMode::Xvfb && !xvfb_ready) {
        fail("headless policy", "xvfb requested but Xvfb tooling is unavailable");
        return 1;
    }

    const bool software_forced = env_truthy("LIBGL_ALWAYS_SOFTWARE") || env_truthy("RENDERCHECK_SOFTWARE_RENDERER");
    if (software_forced) ok("renderer mode", "software renderer forced by environment");
    if (have_config && config.project.renderer == RendererMode::Hardware && software_forced) {
        fail("renderer policy", "hardware renderer requested but software rendering is forced by the environment");
        return 1;
    }
    const bool would_use_xvfb = have_config && !display_present && xvfb_ready &&
        (config.project.headless == HeadlessMode::Auto || config.project.headless == HeadlessMode::Xvfb);
    if (would_use_xvfb && config.project.renderer == RendererMode::Hardware) {
        fail("renderer policy", "hardware renderer requested but Xvfb fallback uses Mesa software rendering");
        return 1;
    }
#endif

    const bool vulkan_required = have_config && config.validation.vulkan;
    if (vulkan_required) ok("Vulkan validation", "required by rendercheck.toml");
    else ok("Vulkan validation", "not required by rendercheck.toml");

    if (executable_in_path("vulkaninfo")) ok("vulkaninfo", "found in PATH");
    else warn("vulkaninfo", "not found in PATH (optional)");
    check_env_path_list("VK_LAYER_PATH", false);
    check_env_path_list("VK_ICD_FILENAMES", true);
    check_env_path_list("VK_DRIVER_FILES", true);

    SharedLibrary vulkan = load_vulkan();
    if (!vulkan.handle) {
        if (vulkan_required) {
            fail("Vulkan loader", "could not load Vulkan or MoltenVK");
            std::cout << "\nEnvironment is not ready for configured Vulkan tests.\n";
            return 1;
        }
        warn("Vulkan loader", "not found (not required by current config)");
        std::cout << "\nEnvironment is ready for configured non-Vulkan RendererCheck runs.\n";
        return 0;
    }
    ok("Vulkan loader", vulkan.path);

    auto get_proc = reinterpret_cast<vkmin::PFN_vkGetInstanceProcAddr>(load_symbol(vulkan.handle, "vkGetInstanceProcAddr"));
    if (!get_proc) {
        if (vulkan_required) { fail("vkGetInstanceProcAddr", "symbol missing from loader"); return 1; }
        warn("vkGetInstanceProcAddr", "symbol missing from optional loader"); return 0;
    }
    ok("vkGetInstanceProcAddr");

    auto enum_version = reinterpret_cast<vkmin::PFN_vkEnumerateInstanceVersion>(get_proc(nullptr, "vkEnumerateInstanceVersion"));
    std::uint32_t api_version = (1U << 22U);
    if (enum_version && enum_version(&api_version) == vkmin::VK_SUCCESS) ok("Vulkan API", version_string(api_version));
    else ok("Vulkan API", "1.0 (loader does not expose vkEnumerateInstanceVersion)");

    bool validation_found = false;
    std::uint32_t layer_count = 0;
    std::string layer_error;
    if (enumerate_validation_layer(vulkan, &validation_found, &layer_count, layer_error)) {
        ok("instance layers", std::to_string(layer_count));
    } else {
        if (vulkan_required) { fail("instance layers", layer_error); return 1; }
        warn("instance layers", layer_error);
    }

    auto enum_layers = reinterpret_cast<vkmin::PFN_vkEnumerateInstanceLayerProperties>(
        get_proc(nullptr, "vkEnumerateInstanceLayerProperties"));
    if (verbose && enum_layers) {
        std::uint32_t count = 0;
        if (enum_layers(&count, nullptr) == vkmin::VK_SUCCESS) {
            std::vector<vkmin::VkLayerProperties> layers(count);
            if (count == 0 || enum_layers(&count, layers.data()) == vkmin::VK_SUCCESS) {
                for (const auto& layer : layers) std::cout << "       layer: " << layer.layerName << '\n';
            }
        }
    }

    if (validation_found) ok("validation layers", "VK_LAYER_KHRONOS_validation");
    else if (vulkan_required) {
        fail("validation layers", "VK_LAYER_KHRONOS_validation not found");
        std::cout << "\nEnvironment is not ready for configured Vulkan validation tests.\n";
        return 1;
    } else warn("validation layers", "VK_LAYER_KHRONOS_validation not found (not required by current config)");

    auto enum_exts = reinterpret_cast<vkmin::PFN_vkEnumerateInstanceExtensionProperties>(
        get_proc(nullptr, "vkEnumerateInstanceExtensionProperties"));
    if (enum_exts) {
        std::uint32_t count = 0;
        if (enum_exts(nullptr, &count, nullptr) == vkmin::VK_SUCCESS) {
            std::vector<vkmin::VkExtensionProperties> exts(count);
            if (count == 0 || enum_exts(nullptr, &count, exts.data()) == vkmin::VK_SUCCESS) {
                ok("instance extensions", std::to_string(count));
                if (verbose) for (const auto& ext : exts) std::cout << "       extension: " << ext.extensionName << '\n';
            }
        }
    }

    std::cout << (vulkan_required ? "\nEnvironment is ready for configured Vulkan RendererCheck runs.\n"
                                  : "\nEnvironment is ready for configured RendererCheck runs.\n");
    return 0;
}

} // namespace rendercheck
