#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rendercheck {

struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgb;
};

struct ImageDiff {
    std::size_t changed_pixels = 0;
    std::size_t total_pixels = 0;
    double changed_percent = 0.0;
    double rmse = 0.0;
    std::uint8_t max_channel_delta = 0;
};

bool load_ppm(const std::filesystem::path& path, Image& image, std::string& error);
bool save_ppm(const std::filesystem::path& path, const Image& image, std::string& error);
bool compare_images(const Image& baseline,
                    const Image& actual,
                    std::uint8_t pixel_threshold,
                    ImageDiff& result,
                    Image& diff,
                    std::string& error);

} // namespace rendercheck
