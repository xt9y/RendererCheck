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
        if (c == '#') { in.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); continue; }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') continue;
        break;
    }
    for (;;) {
        token.push_back(c);
        if (!in.get(c)) return true;
        if (c == '#') { in.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); return true; }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\v') return true;
        if (c == '\r') { if (in.peek() == '\n') in.get(); return true; }
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

void append_u32_be(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

std::uint32_t adler32(const std::uint8_t* data, std::size_t size) {
    constexpr std::uint32_t mod = 65521U;
    std::uint32_t a = 1U;
    std::uint32_t b = 0U;
    for (std::size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % mod;
        b = (b + a) % mod;
    }
    return (b << 16U) | a;
}

void append_chunk(std::vector<std::uint8_t>& png, const char type[4], const std::vector<std::uint8_t>& data) {
    append_u32_be(png, static_cast<std::uint32_t>(data.size()));
    const std::size_t crc_start = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    append_u32_be(png, crc32(png.data() + crc_start, png.size() - crc_start));
}

bool valid_image(const Image& image) {
    if (image.width == 0 || image.height == 0) return false;
    const std::uint64_t bytes = static_cast<std::uint64_t>(image.width) * image.height * 3U;
    return bytes <= std::numeric_limits<std::size_t>::max() && image.rgb.size() == static_cast<std::size_t>(bytes);
}

} // namespace

bool load_ppm(const fs::path& path, Image& image, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "could not open " + path.string(); return false; }
    std::string token;
    if (!read_token(in, token) || token != "P6") { error = path.string() + ": expected binary PPM (P6)"; return false; }
    std::uint32_t width = 0, height = 0, max_value = 0;
    if (!read_token(in, token) || !parse_u32(token, width) || width == 0) { error = path.string() + ": invalid width"; return false; }
    if (!read_token(in, token) || !parse_u32(token, height) || height == 0) { error = path.string() + ": invalid height"; return false; }
    if (!read_token(in, token) || !parse_u32(token, max_value) || max_value != 255) { error = path.string() + ": RendererCheck requires max value 255"; return false; }
    const std::uint64_t byte_count = static_cast<std::uint64_t>(width) * height * 3U;
    if (byte_count > std::numeric_limits<std::size_t>::max() || byte_count > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        error = path.string() + ": image is too large"; return false;
    }
    image.width = width;
    image.height = height;
    image.rgb.resize(static_cast<std::size_t>(byte_count));
    in.read(reinterpret_cast<char*>(image.rgb.data()), static_cast<std::streamsize>(image.rgb.size()));
    if (in.gcount() != static_cast<std::streamsize>(image.rgb.size())) { error = path.string() + ": truncated pixel data"; return false; }
    char extra = 0;
    if (in.get(extra)) { error = path.string() + ": trailing pixel data"; return false; }
    return true;
}

bool save_ppm(const fs::path& path, const Image& image, std::string& error) {
    if (!valid_image(image)) { error = "invalid image data"; return false; }
    std::error_code ec;
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path(), ec);
    if (ec) { error = "could not create " + path.parent_path().string(); return false; }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) { error = "could not write " + path.string(); return false; }
    out << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    out.write(reinterpret_cast<const char*>(image.rgb.data()), static_cast<std::streamsize>(image.rgb.size()));
    if (!out) { error = "failed while writing " + path.string(); return false; }
    return true;
}

bool save_png(const fs::path& path, const Image& image, std::string& error) {
    if (!valid_image(image)) { error = "invalid image data"; return false; }
    const std::uint64_t row_bytes64 = 1U + static_cast<std::uint64_t>(image.width) * 3U;
    const std::uint64_t raw_size64 = row_bytes64 * image.height;
    if (row_bytes64 > std::numeric_limits<std::size_t>::max() || raw_size64 > std::numeric_limits<std::size_t>::max()) {
        error = "image is too large"; return false;
    }

    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(raw_size64));
    const std::size_t row_rgb = static_cast<std::size_t>(image.width) * 3U;
    for (std::uint32_t y = 0; y < image.height; ++y) {
        raw.push_back(0);
        const auto begin = image.rgb.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * row_rgb);
        raw.insert(raw.end(), begin, begin + static_cast<std::ptrdiff_t>(row_rgb));
    }

    std::vector<std::uint8_t> zlib;
    zlib.reserve(raw.size() + raw.size() / 65535U * 5U + 16U);
    zlib.push_back(0x78);
    zlib.push_back(0x01);
    std::size_t offset = 0;
    while (offset < raw.size()) {
        const std::size_t remaining = raw.size() - offset;
        const std::uint16_t block = static_cast<std::uint16_t>(std::min<std::size_t>(remaining, 65535U));
        const bool final = offset + block == raw.size();
        zlib.push_back(final ? 0x01 : 0x00);
        zlib.push_back(static_cast<std::uint8_t>(block & 0xffU));
        zlib.push_back(static_cast<std::uint8_t>((block >> 8U) & 0xffU));
        const std::uint16_t nlen = static_cast<std::uint16_t>(~block);
        zlib.push_back(static_cast<std::uint8_t>(nlen & 0xffU));
        zlib.push_back(static_cast<std::uint8_t>((nlen >> 8U) & 0xffU));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + block));
        offset += block;
    }
    append_u32_be(zlib, adler32(raw.data(), raw.size()));

    std::vector<std::uint8_t> png = {137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<std::uint8_t> ihdr;
    append_u32_be(ihdr, image.width);
    append_u32_be(ihdr, image.height);
    ihdr.push_back(8);
    ihdr.push_back(2);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    append_chunk(png, "IHDR", ihdr);
    append_chunk(png, "IDAT", zlib);
    append_chunk(png, "IEND", {});

    std::error_code ec;
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path(), ec);
    if (ec) { error = "could not create " + path.parent_path().string(); return false; }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) { error = "could not write " + path.string(); return false; }
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!out) { error = "failed while writing " + path.string(); return false; }
    return true;
}

bool compare_images(const Image& baseline,
                    const Image& actual,
                    std::uint8_t pixel_threshold,
                    ImageDiff& result,
                    Image& diff,
                    std::string& error) {
    if (baseline.width != actual.width || baseline.height != actual.height) {
        error = "image dimensions differ: baseline " + std::to_string(baseline.width) + "x" + std::to_string(baseline.height) +
                ", actual " + std::to_string(actual.width) + "x" + std::to_string(actual.height);
        return false;
    }
    if (baseline.rgb.size() != actual.rgb.size()) { error = "image data size differs"; return false; }
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
            const int delta = std::abs(static_cast<int>(actual.rgb[base + channel]) - static_cast<int>(baseline.rgb[base + channel]));
            result.max_channel_delta = std::max(result.max_channel_delta, static_cast<std::uint8_t>(delta));
            squared_error += static_cast<long double>(delta) * delta;
            if (delta > pixel_threshold) changed = true;
            diff.rgb[base + channel] = static_cast<std::uint8_t>(std::min(255, delta * 4));
        }
        if (changed) ++result.changed_pixels;
    }
    if (result.total_pixels != 0) {
        result.changed_percent = 100.0 * static_cast<double>(result.changed_pixels) / static_cast<double>(result.total_pixels);
        result.rmse = std::sqrt(static_cast<double>(squared_error / static_cast<long double>(result.total_pixels * 3U)));
    }
    return true;
}

Image side_by_side(const Image& left, const Image& right) {
    constexpr std::uint32_t gap = 8;
    Image out;
    out.width = left.width + gap + right.width;
    out.height = std::max(left.height, right.height);
    out.rgb.assign(static_cast<std::size_t>(out.width) * out.height * 3U, 32);
    auto copy = [&](const Image& src, std::uint32_t xoff) {
        for (std::uint32_t y = 0; y < src.height; ++y) {
            for (std::uint32_t x = 0; x < src.width; ++x) {
                const std::size_t si = (static_cast<std::size_t>(y) * src.width + x) * 3U;
                const std::size_t di = (static_cast<std::size_t>(y) * out.width + xoff + x) * 3U;
                out.rgb[di + 0] = src.rgb[si + 0];
                out.rgb[di + 1] = src.rgb[si + 1];
                out.rgb[di + 2] = src.rgb[si + 2];
            }
        }
    };
    copy(left, 0);
    copy(right, left.width + gap);
    return out;
}

} // namespace rendercheck
