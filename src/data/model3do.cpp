#include "data/model3do.hpp"

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

namespace c3 {

namespace {

uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

float read_f32(const uint8_t* p) {
    uint32_t bits = read_u32(p);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

std::string read_cstring(const uint8_t* p, size_t max_len) {
    std::string s;
    s.reserve(max_len);
    for (size_t i = 0; i < max_len && p[i] != '\0'; ++i) {
        s.push_back(static_cast<char>(p[i]));
    }
    return s;
}

} // namespace

std::optional<Model3DO1> decode_3do1(const std::vector<uint8_t>& data) {
    if (data.size() < 0x24) {
        std::cerr << "3DO1: file too small for header\n";
        return std::nullopt;
    }

    // Magic "3DO1" at offset 0x14
    if (read_u32(data.data() + 0x14) != 0x314F4433u) {
        // Check big-endian magic bytes "3DO1" = 33 44 4f 31
        if (data[0x14] != '3' || data[0x15] != 'D' || data[0x16] != 'O' ||
            data[0x17] != '1') {
            std::cerr << "3DO1: invalid magic at 0x14\n";
            return std::nullopt;
        }
    }

    Model3DO1 model;

    model.field_0x10 = read_u32(data.data() + 0x10);
    model.version_major = data[0x18];
    model.version_minor = data[0x19];

    // 8-byte name at 0x1C
    char name_buf[9] = {0};
    std::memcpy(name_buf, data.data() + 0x1C, 8);
    model.name = read_cstring(reinterpret_cast<const uint8_t*>(name_buf), 8);

    size_t pos = 0x24;

    // ---- Texture references ----
    // Each texture ref appears to be: 8-byte name ("BLDNGXX.PCX") followed by
    // 8 bytes of metadata (width/height-ish values), repeating.
    // We detect texture refs by scanning for ".PCX" strings.
    while (pos + 16 <= data.size()) {
        const uint8_t* p = data.data() + pos;
        // Look for a texture name: 8 bytes ending in ".PCX" or containing "PCX"
        bool looks_like_texture = false;
        for (int i = 0; i < 5; ++i) {
            if (p[i] == '.' && p[i+1] == 'P' && p[i+2] == 'C' && p[i+3] == 'X') {
                looks_like_texture = true;
                break;
            }
        }

        if (!looks_like_texture) {
            break;
        }

        TextureRef tex;
        tex.name = read_cstring(p, 8);
        tex.width = read_u16(p + 8);
        tex.height = read_u16(p + 10);
        model.textures.push_back(tex);
        pos += 16;
    }

    // ---- Vertex / face data ----
    // After textures, data consists of 12-byte vertex records (3 floats) and
    // 6-byte face records (3 uint16 indices), separated by 0x0000ffff sentinels.
    // We parse sequentially: a sentinel (ffff 0000) marks section boundaries.

    while (pos + 4 <= data.size()) {
        uint32_t sentinel = read_u32(data.data() + pos);
        if (sentinel == 0x0000ffffu) {
            pos += 4;
            continue;
        }

        // Try to read a vertex (3 floats = 12 bytes)
        if (pos + 12 <= data.size()) {
            float x = read_f32(data.data() + pos);
            float y = read_f32(data.data() + pos + 4);
            float z = read_f32(data.data() + pos + 8);

            // Heuristic: valid vertex coords are typically within reasonable range
            // and not all zeros/sentinels. If the first 4 bytes look like a sentinel
            // or face index pattern, stop.
            uint32_t first = read_u32(data.data() + pos);
            if (first == 0x0000ffffu || first == 0xffffffffu) {
                break;
            }

            model.vertices.push_back(ModelVertex{x, y, z});
            pos += 12;
            continue;
        }

        break;
    }

    // ---- Faces ----
    // After vertices, faces are groups of 3 uint16 indices.
    // Re-scan from after the vertex block looking for face patterns.
    // We'll attempt face parsing by reading uint16 triples until sentinel/EOF.

    size_t face_pos = pos;
    while (face_pos + 6 <= data.size()) {
        uint16_t i0 = read_u16(data.data() + face_pos);
        uint16_t i1 = read_u16(data.data() + face_pos + 2);
        uint16_t i2 = read_u16(data.data() + face_pos + 4);

        if (i0 == 0xffff || i1 == 0xffff || i2 == 0xffff) {
            face_pos += 2; // skip sentinel partial
            continue;
        }

        // Heuristic: valid face indices reference existing vertices
        if (i0 < model.vertices.size() && i1 < model.vertices.size() &&
            i2 < model.vertices.size()) {
            model.faces.push_back(ModelFace{i0, i1, i2});
            face_pos += 6;
        } else {
            break;
        }
    }

    return model;
}

} // namespace c3
