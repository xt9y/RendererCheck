#include "rendercheck/image.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <string>

namespace fs = std::filesystem;

namespace rendercheck {
namespace {

bool read_token(std::istream& in, std::string& token) {
    token.clear();
    char c = 0;

    for (;;) {
        if (!in.get(c)) return false;
        if (c == '#') {
            in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') continue;
        break;
    }

    for (;;) {
        token.push_back(c);
        if (!in.get(c)) return true;

        if (c == '#') {
            in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            return true;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\v') return true;
        if (c == '\r') {
            if (in.peek() == '\n') in.get();
            return true;
        }
    }
}

bool parse_u32(const std::string& token, std::uint32_t& value) {
    if (token.empty()) return false;
    std::uint64_t parsed = 0;
    for (const char c : token) {
        if (c < '0' || c > '9') return false;
        parsed = parsed * 10U + static_cast<unsigned>(c - '0');
        if (parsed > std::numeric_limits<std::uint32_t>::max()) return false;
    }
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

} // namespace

bool load_ppm(const std::filesystem::path& path, Image& image, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "could not open " + path.string();
        return false;
    }

    std::string token;
    if (!read_token(in, token) || token != "P6") {
        error = path.string() + ": expected binary PPM (P6)";
        return false;
    }

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t max_value = 0;

    if (!read_token(in, token) || !parse_u32(token, width) || width == 0) {
        error = path.string() + ": invalid width";
        return false;
    }
    if (!read_token(in, token) || !parse_u32(token, height) || height == 0) {
        error = path.string() + ": invalid height";
        return false;
    }
    if (!read_token(in, token) || !parse_u32(token, max_value) || max_value != 255) {
        error = path.string() + ": RendererCheck requires max value 255";
        return false;
    }

    const std::uint64_t byte_count = static_cast<std::uint64_t>(width) * height * 3U;
    if (byte_count > std::numeric_limits<std::size_t>::max()) {
        error = path.string() + ": image is too large";
        return false;
    }

    image.width = width;
    image.height = height;
    image.rgb.resize(static_cast<std::size_t>(byte_count));
    in.read(reinterpret_cast<char*>(image.rgb.data()), static_cast<std::streamsize>(image.rgb.size()));

    if (in.gcount() != static_cast<std::streamsize>(image.rgb.size())) {
        error = path.string() + ": truncated pixel data";
        return false;
    }

    return true;
}

bool save_ppm(const std::filesystem::path& path, const Image& image, std::string& error) {
    if (image.width == 0 || image.height == 0 ||
        image.rgb.size() != static_cast<std::size_t>(image.width) * image.height * 3U) {
        error = "invalid image data";
        return false;
    }

    std::error_code ec;
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "could not create " + path.parent_path().string();
        return false;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "could not write " + path.string();
        return false;
    }

    out << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    out.write(reinterpret_cast<const char*>(image.rgb.data()), static_cast<std::streamsize>(image.rgb.size()));
    if (!out) {
        error = "failed while writing " + path.string();
        return false;
    }
    return true;
}

bool compare_images(const Image& baseline,
                    const Image& actual,
                    std::uint8_t pixel_threshold,
                    ImageDiff& result,
                    Image& diff,
                    std::string& error) {
    if (baseline.width != actual.width || baseline.height != actual.height) {
        error = "image dimensions differ: baseline " + std::to_string(baseline.width) + "x" +
                std::to_string(baseline.height) + ", actual " + std::to_string(actual.width) + "x" +
                std::to_string(actual.height);
        return false;
    }

    if (baseline.rgb.size() != actual.rgb.size()) {
        error = "image data size differs";
        return false;
    }

    result = {};
    result.total_pixels = static_cast<std::size_t>(actual.width) * actual.height;
    diff.width = actual.width;
    diff.height = actual.height;
    diff.rgb.resize(actual.rgb.size());

    long double squared_error = 0.0;

    for (std::size_t pixel = 0; pixel < result.total_pixels; ++pixel) {
        bool changed = false;
        const std::size_t base = pixel * 3U;

        for (std::size_t channel = 0; channel < 3; ++channel) {
            const int delta = std::abs(static_cast<int>(actual.rgb[base + channel]) -
                                       static_cast<int>(baseline.rgb[base + channel]));
            result.max_channel_delta = std::max(result.max_channel_delta, static_cast<std::uint8_t>(delta));
            squared_error += static_cast<long double>(delta) * delta;
            if (delta > pixel_threshold) changed = true;
            diff.rgb[base + channel] = static_cast<std::uint8_t>(std::min(255, delta * 4));
        }

        if (changed) ++result.changed_pixels;
    }

    if (result.total_pixels != 0) {
        result.changed_percent = 100.0 * static_cast<double>(result.changed_pixels) /
                                 static_cast<double>(result.total_pixels);
        result.rmse = std::sqrt(static_cast<double>(squared_error /
                  static_cast<long double>(result.total_pixels * 3U)));
    }

    return true;
}

} // namespace rendercheck
