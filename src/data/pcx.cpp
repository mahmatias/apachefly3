#include "pcx.hpp"
#include <fstream>
#include <algorithm>

namespace c3 {

std::optional<PCXImage> decode_pcx(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 128 > data.size()) return std::nullopt;

    // Check PCX marker (0x0A)
    if (data[offset] != 0x0A) return std::nullopt;

    PCXImage img;
    const uint8_t* hdr = data.data() + offset;

    // PCX header (128 bytes)
    uint8_t manufacturer = hdr[0];      // 0x0A
    uint8_t version = hdr[1];           // 5 = 256 color
    uint8_t encoding = hdr[2];          // 1 = RLE
    uint8_t bits_per_pixel = hdr[3];    // 8 for 256 color

    img.width = *reinterpret_cast<const uint16_t*>(hdr + 8) + 1 - *reinterpret_cast<const uint16_t*>(hdr + 4);
    img.height = *reinterpret_cast<const uint16_t*>(hdr + 10) + 1 - *reinterpret_cast<const uint16_t*>(hdr + 6);
    // Or use xmax/ymax directly
    uint16_t xmin = *reinterpret_cast<const uint16_t*>(hdr + 4);
    uint16_t ymin = *reinterpret_cast<const uint16_t*>(hdr + 6);
    uint16_t xmax = *reinterpret_cast<const uint16_t*>(hdr + 8);
    uint16_t ymax = *reinterpret_cast<const uint16_t*>(hdr + 10);
    img.width = xmax - xmin + 1;
    img.height = ymax - ymin + 1;

    // Horizontal/vertical DPI
    // uint16_t hdpi = *reinterpret_cast<const uint16_t*>(hdr + 12);
    // uint16_t vdpi = *reinterpret_cast<const uint16_t*>(hdr + 14);

    // Palette info (bytes 16-67) - 48 bytes for 16-color palette
    // bytes 68-69: reserved
    // uint16_t color_planes = *reinterpret_cast<const uint16_t*>(hdr + 68);
    img.num_planes = hdr[65]; // byte 65
    img.bits_per_pixel = bits_per_pixel;
    img.bytes_per_line = *reinterpret_cast<const uint16_t*>(hdr + 66);

    // Palette type (byte 68)
    // uint8_t palette_type = hdr[68];

    // Skip to image data (after 128 byte header)
    size_t img_data_offset = offset + 128;

    // For 256-color images, palette is at end of file (last 769 bytes: 1 byte marker + 768 bytes palette)
    if (version == 5 && bits_per_pixel == 8 && img.num_planes == 1) {
        // Check for palette at end of file
        if (data.size() >= 769) {
            uint8_t palette_marker = data[data.size() - 769];
            if (palette_marker == 0x0C) {
                img.palette.assign(data.data() + data.size() - 768, data.data() + data.size());
            }
        }
    }

    // Decode RLE image data
    size_t expected_pixels = img.width * img.height;
    img.pixels.resize(expected_pixels);

    size_t src = img_data_offset;
    size_t dst = 0;

    while (dst < expected_pixels && src < data.size()) {
        uint8_t byte = data[src++];
        if ((byte & 0xC0) == 0xC0) { // RLE run
            uint8_t count = byte & 0x3F;
            if (src >= data.size()) break;
            uint8_t value = data[src++];
            size_t run_end = std::min(dst + count, expected_pixels);
            std::fill(img.pixels.begin() + dst, img.pixels.begin() + run_end, value);
            dst = run_end;
        } else {
            img.pixels[dst++] = byte;
        }
    }

    // If no palette found at end, try default VGA palette
    if (img.palette.empty()) {
        img.palette.resize(768);
        // Generate default grayscale palette
        for (int i = 0; i < 256; ++i) {
            img.palette[i * 3] = i;
            img.palette[i * 3 + 1] = i;
            img.palette[i * 3 + 2] = i;
        }
    }

    return img;
}

std::vector<uint32_t> PCXImage::to_rgba() const {
    std::vector<uint32_t> rgba(width * height);
    for (size_t i = 0; i < pixels.size(); ++i) {
        uint8_t idx = pixels[i];
        if (idx * 3 + 2 < palette.size()) {
            uint8_t r = palette[idx * 3];
            uint8_t g = palette[idx * 3 + 1];
            uint8_t b = palette[idx * 3 + 2];
            rgba[i] = (0xFFu << 24) | (r << 16) | (g << 8) | b;
        } else {
            rgba[i] = 0xFF000000;
        }
    }
    return rgba;
}

// Simple PNG writer (using stb_image_write would be better, but for now write raw)
bool write_pcx_png(const PCXImage& img, const std::string& path) {
    // For now, write a simple PPM file
    std::string ppm_path = path;
    size_t dot = ppm_path.find_last_of('.');
    if (dot != std::string::npos) {
        ppm_path = ppm_path.substr(0, dot) + ".ppm";
    } else {
        ppm_path += ".ppm";
    }

    std::ofstream out(ppm_path, std::ios::binary);
    if (!out) return false;

    auto rgba = img.to_rgba();
    out << "P6\n" << img.width << " " << img.height << "\n255\n";
    for (uint32_t pixel : rgba) {
        uint8_t r = (pixel >> 16) & 0xFF;
        uint8_t g = (pixel >> 8) & 0xFF;
        uint8_t b = pixel & 0xFF;
        out.write(reinterpret_cast<const char*>(&r), 1);
        out.write(reinterpret_cast<const char*>(&g), 1);
        out.write(reinterpret_cast<const char*>(&b), 1);
    }
    return true;
}

} // namespace c3