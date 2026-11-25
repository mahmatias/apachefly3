#include "data/resource2.hpp"
#include "data/iso9660.hpp"
#include "data/pcx.hpp"
#include "data/model3do.hpp"
#include "data/midi.hpp"
#include "data/wav.hpp"
#include "data/lzp1.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <iomanip>

namespace fs = std::filesystem;

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " <iso_path|resource_dir> [options]\n";
    std::cout << "Options:\n";
    std::cout << "  --list              List all resources\n";
    std::cout << "  --extract <name>    Extract specific resource by name\n";
    std::cout << "  --extract-all <dir> Extract all resources to directory\n";
    std::cout << "  --identify          Identify resource types by magic bytes\n";
    std::cout << "  --from-disk         Load from installed game directory (RESOURCE.RES + shards)\n";
    std::cout << "  --decode-pcx <name> Decode PCX image to PPM (use --pcx-out <dir>)\n";
    std::cout << "  --pcx-out <dir>     Output directory for decoded PCX images\n";
    std::cout << "  --decode-3do1 <name> Decode 3DO1 model to text\n";
    std::cout << "  --decode-midi <name> Decode MIDI file to text\n";
    std::cout << "  --decode-wav <name> Decode WAV file to text\n";
    std::cout << "  --decode-lzp1 <name> Decompress LZP1 resource and detect inner type\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string input_path = argv[1];
    bool do_list = false;
    bool do_identify = false;
    bool from_disk = false;
    std::string extract_name;
    std::string extract_dir;
    std::string decode_pcx_name;
    std::string pcx_out_dir;
    std::string decode_3do1_name;
    std::string decode_midi_name;
    std::string decode_wav_name;
    std::string decode_lzp1_name;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--list") {
            do_list = true;
        } else if (arg == "--identify") {
            do_identify = true;
        } else if (arg == "--from-disk") {
            from_disk = true;
        } else if (arg == "--extract" && i + 1 < argc) {
            extract_name = argv[++i];
        } else if (arg == "--extract-all" && i + 1 < argc) {
            extract_dir = argv[++i];
        } else if (arg == "--decode-pcx" && i + 1 < argc) {
            decode_pcx_name = argv[++i];
        } else if (arg == "--pcx-out" && i + 1 < argc) {
            pcx_out_dir = argv[++i];
        } else if (arg == "--decode-3do1" && i + 1 < argc) {
            decode_3do1_name = argv[++i];
        } else if (arg == "--decode-midi" && i + 1 < argc) {
            decode_midi_name = argv[++i];
        } else if (arg == "--decode-wav" && i + 1 < argc) {
            decode_wav_name = argv[++i];
        } else if (arg == "--decode-lzp1" && i + 1 < argc) {
            decode_lzp1_name = argv[++i];
        }
    }

    // Load resource index
    c3::ResourceIndex index = (from_disk || input_path.find("RESOURCE.RES") != std::string::npos)
        ? c3::ResourceIndex::load_from_disk([&]() {
            fs::path base_path = input_path;
            if (base_path.filename() == "RESOURCE.RES") {
                base_path = base_path.parent_path();
            }
            std::cout << "Loading from disk: " << base_path << std::endl;
            return base_path;
        }())
        : c3::ResourceIndex::build([&]() {
            c3::Iso9660 iso(input_path);
            std::cout << "ISO: " << iso.volume_id() << std::endl;
            std::cout << "Block size: " << iso.logical_block_size() << std::endl;
            return iso;
        }());

    std::cout << "Shards found: " << index.shard_paths().size() << std::endl;
    std::cout << "Total entries: " << index.entries().size() << std::endl;

    if (do_list) {
        std::cout << "\n=== Resources ===\n";
        for (const auto& [name, entry] : index.entries()) {
            std::cout << name << " (shard " << entry.shard
                      << ", offset " << entry.offset
                      << ", size " << entry.size << ")\n";
        }
    }

    // Helper to scan for magic bytes at various offsets
auto identify_format = [](const std::vector<uint8_t>& data) -> std::string {
    if (data.size() < 4) return "UNKNOWN";

    // Check at offset 0
    uint32_t magic0 = *reinterpret_cast<const uint32_t*>(data.data());
    if (magic0 == 0x314F4433) return "3DO1"; // "3DO1"
    if (data[0] == 0x0A) return "PCX"; // PCX marker
    if (data.size() >= 4 && data[0] == 'M' && data[1] == 'T' && data[2] == 'h' && data[3] == 'd') return "MIDI";
    if (data.size() >= 4 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F') return "RIFF/WAV";
    if (data.size() >= 0x18 && data[0x14] == 'L' && data[0x15] == 'Z' && data[0x16] == 'P' && data[0x17] == '1') return "LZP1";

    // Scan deeper for magic bytes (up to 256 bytes)
    const size_t max_scan = std::min<size_t>(data.size(), 256);
    for (size_t off = 4; off + 4 <= max_scan; ++off) {
        uint32_t magic = *reinterpret_cast<const uint32_t*>(data.data() + off);
        if (magic == 0x314F4433) return "3DO1@" + std::to_string(off);
        if (data[off] == 0x0A && off + 128 < data.size()) return "PCX@" + std::to_string(off);
        if (off + 3 < max_scan && data[off] == 'M' && data[off+1] == 'T' && data[off+2] == 'h' && data[off+3] == 'd') return "MIDI@" + std::to_string(off);
        if (off + 3 < max_scan && data[off] == 'R' && data[off+1] == 'I' && data[off+2] == 'F' && data[off+3] == 'F') return "RIFF/WAV@" + std::to_string(off);
        if (off + 3 < max_scan && data[off] == 'L' && data[off+1] == 'Z' && data[off+2] == 'P' && data[off+3] == '1') return "LZP1@" + std::to_string(off);
    }
    return "UNKNOWN";
};

if (do_identify) {
    std::cout << "\n=== Resource Types ===\n";
    for (const auto& [name, entry] : index.entries()) {
        auto data = index.read_resource(entry);
        if (!data.empty()) {
            std::string type = identify_format(data);
            std::cout << std::setw(12) << name << " : " << type
                      << " (size=" << data.size() << ")\n";
        }
    }
}

    if (!extract_name.empty()) {
        auto opt = index.find(extract_name);
        if (!opt) {
            std::cerr << "Resource not found: " << extract_name << std::endl;
            return 1;
        }
        auto data = index.read_resource(*opt);
        if (data.empty()) {
            std::cerr << "Failed to read resource: " << extract_name << std::endl;
            return 1;
        }
        std::string out_name = extract_name;
        // If --output was provided, we'd use it, but for now just use the extract_name
        std::ofstream out(out_name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), data.size());
        std::cout << "Extracted " << extract_name << " (" << data.size() << " bytes)\n";
    }

    if (!extract_dir.empty()) {
        fs::create_directories(extract_dir);
        std::cout << "\nExtracting all resources to " << extract_dir << "...\n";
        size_t count = 0;
        for (const auto& [name, entry] : index.entries()) {
            auto data = index.read_resource(entry);
            if (!data.empty()) {
                fs::path out_path = fs::path(extract_dir) / name;
                fs::create_directories(out_path.parent_path());
                std::ofstream out(out_path, std::ios::binary);
                out.write(reinterpret_cast<const char*>(data.data()), data.size());
                ++count;
            }
        }
        std::cout << "Extracted " << count << " resources\n";
    }

    if (!decode_pcx_name.empty()) {
        auto opt = index.find(decode_pcx_name);
        if (!opt) {
            std::cerr << "Resource not found: " << decode_pcx_name << std::endl;
            return 1;
        }
        auto data = index.read_resource(*opt);
        if (data.empty()) {
            std::cerr << "Failed to read resource: " << decode_pcx_name << std::endl;
            return 1;
        }

        // Scan for PCX magic (0x0A) at plausible header offsets
        size_t pcx_offset = std::string::npos;
        for (size_t off = 0; off + 128 < data.size(); ++off) {
            if (data[off] == 0x0A) {
                pcx_offset = off;
                break;
            }
        }

        std::optional<c3::PCXImage> img;
        if (pcx_offset != std::string::npos) {
            img = c3::decode_pcx(data, pcx_offset);
        } else {
            img = c3::decode_pcx(data);
        }

        if (!img) {
            std::cerr << "Failed to decode PCX: " << decode_pcx_name << std::endl;
            return 1;
        }
        fs::create_directories(pcx_out_dir);
        fs::path out_path = fs::path(pcx_out_dir) / (decode_pcx_name + ".ppm");
        if (!c3::write_pcx_png(*img, out_path.string())) {
            std::cerr << "Failed to write PPM: " << out_path << std::endl;
            return 1;
        }
        std::cout << "Decoded PCX: " << decode_pcx_name << " -> " << out_path
                  << " (" << img->width << "x" << img->height << ")\n";
    }

    if (!decode_3do1_name.empty()) {
        auto opt = index.find(decode_3do1_name);
        if (!opt) {
            std::cerr << "Resource not found: " << decode_3do1_name << std::endl;
            return 1;
        }
        auto data = index.read_resource(*opt);
        if (data.empty()) {
            std::cerr << "Failed to read resource: " << decode_3do1_name << std::endl;
            return 1;
        }

        auto model = c3::decode_3do1(data);
        if (!model) {
            std::cerr << "Failed to decode 3DO1: " << decode_3do1_name << std::endl;
            return 1;
        }

        std::cout << "Decoded 3DO1: " << decode_3do1_name << "\n";
        std::cout << "  name: " << model->name << "\n";
        std::cout << "  field_0x10: " << model->field_0x10 << "\n";
        std::cout << "  version: " << (int)model->version_major << "."
                  << (int)model->version_minor << "\n";
        std::cout << "  textures: " << model->textures.size() << "\n";
        for (const auto& t : model->textures) {
            std::cout << "    - " << t.name << " (" << t.width << "x" << t.height << ")\n";
        }
        std::cout << "  vertices: " << model->vertices.size() << "\n";
        std::cout << "  faces: " << model->faces.size() << "\n";
    }

    if (!decode_midi_name.empty()) {
        auto opt = index.find(decode_midi_name);
        if (!opt) {
            std::cerr << "Resource not found: " << decode_midi_name << std::endl;
            return 1;
        }
        auto data = index.read_resource(*opt);
        if (data.empty()) {
            std::cerr << "Failed to read resource: " << decode_midi_name << std::endl;
            return 1;
        }

        // Scan for MThd magic
        size_t midi_offset = std::string::npos;
        for (size_t off = 0; off + 4 <= data.size(); ++off) {
            if (data[off] == 'M' && data[off+1] == 'T' && data[off+2] == 'h' &&
                data[off+3] == 'd') {
                midi_offset = off;
                break;
            }
        }
        std::vector<uint8_t> midi_data;
        if (midi_offset != std::string::npos) {
            midi_data.assign(data.begin() + midi_offset, data.end());
        } else {
            midi_data = data;
        }

        auto midi = c3::decode_midi(midi_data);
        if (!midi) {
            std::cerr << "Failed to decode MIDI: " << decode_midi_name << std::endl;
            return 1;
        }

        std::cout << "Decoded MIDI: " << decode_midi_name << "\n";
        std::cout << "  format: " << midi->format << "\n";
        std::cout << "  tracks: " << midi->ntracks << "\n";
        std::cout << "  division: " << midi->division << "\n";
        for (size_t t = 0; t < midi->tracks.size(); ++t) {
            std::cout << "  track " << t << ": " << midi->tracks[t].events.size()
                      << " events\n";
        }
    }

    if (!decode_wav_name.empty()) {
        auto opt = index.find(decode_wav_name);
        if (!opt) {
            std::cerr << "Resource not found: " << decode_wav_name << std::endl;
            return 1;
        }
        auto data = index.read_resource(*opt);
        if (data.empty()) {
            std::cerr << "Failed to read resource: " << decode_wav_name << std::endl;
            return 1;
        }

        // Scan for RIFF magic
        size_t wav_offset = std::string::npos;
        for (size_t off = 0; off + 4 <= data.size(); ++off) {
            if (data[off] == 'R' && data[off+1] == 'I' && data[off+2] == 'F' &&
                data[off+3] == 'F') {
                wav_offset = off;
                break;
            }
        }
        std::vector<uint8_t> wav_data;
        if (wav_offset != std::string::npos) {
            wav_data.assign(data.begin() + wav_offset, data.end());
        } else {
            wav_data = data;
        }

        auto wav = c3::decode_wav(wav_data);
        if (!wav) {
            std::cerr << "Failed to decode WAV: " << decode_wav_name << std::endl;
            return 1;
        }

        std::cout << "Decoded WAV: " << decode_wav_name << "\n";
        std::cout << "  audio_format: " << wav->audio_format << "\n";
        std::cout << "  channels: " << wav->num_channels << "\n";
        std::cout << "  sample_rate: " << wav->sample_rate << "\n";
        std::cout << "  byte_rate: " << wav->byte_rate << "\n";
        std::cout << "  block_align: " << wav->block_align << "\n";
        std::cout << "  bits_per_sample: " << wav->bits_per_sample << "\n";
        std::cout << "  data bytes: " << wav->data.size() << "\n";
    }

    if (!decode_lzp1_name.empty()) {
        auto opt = index.find(decode_lzp1_name);
        if (!opt) {
            std::cerr << "Resource not found: " << decode_lzp1_name << std::endl;
            return 1;
        }
        auto data = index.read_resource(*opt);
        if (data.empty()) {
            std::cerr << "Failed to read resource: " << decode_lzp1_name << std::endl;
            return 1;
        }

        auto result = c3::data::lzp1_decompress(data.data(), data.size());
        if (!result) {
            std::cerr << "Failed to decompress LZP1: " << decode_lzp1_name << std::endl;
            return 1;
        }

        const std::vector<uint8_t>& decoded = *result;
        std::cout << "Decoded LZP1: " << decode_lzp1_name << "\n";
        std::cout << "  compressed size: " << data.size() << "\n";
        std::cout << "  decompressed size: " << decoded.size() << "\n";

        // Detect inner magic bytes
        std::string inner = "UNKNOWN";
        if (decoded.size() >= 4) {
            if (decoded[0] == 'M' && decoded[1] == 'T' && decoded[2] == 'h' &&
                decoded[3] == 'd') inner = "MIDI";
            else if (decoded[0] == 'R' && decoded[1] == 'I' && decoded[2] == 'F' &&
                     decoded[3] == 'F') inner = "RIFF/WAV";
            else if (decoded[0] == 0x0A) inner = "PCX";
        } else if (decoded.size() >= 0x18 &&
                   decoded[0x14] == 'L' && decoded[0x15] == 'Z' &&
                   decoded[0x16] == 'P' && decoded[0x17] == '1') {
            inner = "LZP1";
        }
        std::cout << "  inner type: " << inner << "\n";
    }

    return 0;
}