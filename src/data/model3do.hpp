#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace c3 {

struct ModelVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct ModelFace {
    uint16_t v0 = 0;
    uint16_t v1 = 0;
    uint16_t v2 = 0;
};

struct TextureRef {
    std::string name;
    uint16_t width = 0;
    uint16_t height = 0;
};

struct Model3DO1 {
    uint32_t field_0x10 = 0;
    uint8_t version_major = 0;
    uint8_t version_minor = 0;
    std::string name;          // 8-byte name
    std::vector<TextureRef> textures;
    std::vector<ModelVertex> vertices;
    std::vector<ModelFace> faces;
    std::vector<uint32_t> unknown_dwords; // leftover raw data for debugging
};

std::optional<Model3DO1> decode_3do1(const std::vector<uint8_t>& data);

} // namespace c3
