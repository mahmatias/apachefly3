#include "application.hpp"
#include "../data/iso9660.hpp"
#include "../data/resource2.hpp"
#include <iostream>
#include <iomanip>

namespace c3 {

Application::Application(const std::filesystem::path& iso_path) : iso_path_(iso_path) {}

Application::~Application() = default;

void list_all_files_recursive(const Iso9660& iso, const std::string& path, int depth = 0) {
  auto entries = iso.list_directory(path);
  for (const auto& entry : entries) {
    std::string indent(depth * 2, ' ');
    if (entry.is_directory) {
      std::cout << indent << path << "/" << entry.name << "/" << std::endl;
      if (depth < 5) {  // Limit depth to avoid infinite recursion
        list_all_files_recursive(iso, path + "/" + entry.name, depth + 1);
      }
    } else {
      std::cout << indent << path << "/" << entry.name << " (extent=" << entry.extent_lba << ", size=" << entry.size_bytes << ")" << std::endl;
    }
  }
}

void dump_c3_nam(const Iso9660& iso) {
  // Try to find c3.nam
  auto entry = iso.find_file("/nova/c3.nam");
  if (!entry) {
    entry = iso.find_file("/nova/c3.NAM");
  }
  if (!entry) {
    std::cout << "c3.nam not found in ISO" << std::endl;
    return;
  }

  std::cout << "Found c3.nam at extent " << entry->extent_lba << " size " << entry->size_bytes << std::endl;
  auto data = iso.read_bytes(static_cast<uint64_t>(entry->extent_lba) * iso.logical_block_size(), entry->size_bytes);

  std::cout << "c3.nam contents (first 256 bytes):" << std::endl;
  for (int row = 0; row < 16 && row * 16 < static_cast<int>(data.size()); ++row) {
    std::cout << "  " << std::hex << std::setw(4) << std::setfill('0') << (row * 16) << ": ";
    for (int col = 0; col < 16; ++col) {
      int idx = row * 16 + col;
      if (idx < static_cast<int>(data.size())) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[idx]) << " ";
      } else {
        std::cout << "   ";
      }
    }
    std::cout << "  ";
    for (int col = 0; col < 16; ++col) {
      int idx = row * 16 + col;
      if (idx < static_cast<int>(data.size())) {
        char c = static_cast<char>(data[idx]);
        std::cout << (c >= 32 && c < 127 ? c : '.');
      } else {
        std::cout << " ";
      }
    }
    std::cout << std::dec << std::endl;
  }

  // Try to parse as null-terminated strings
  std::cout << "\nParsed strings from c3.nam:" << std::endl;
  size_t pos = 0;
  int count = 0;
  while (pos < data.size() && count < 100) {
    if (data[pos] == 0) {
      pos++;
      continue;
    }
    size_t start = pos;
    while (pos < data.size() && data[pos] != 0) pos++;
    if (pos > start) {
      std::string name(reinterpret_cast<const char*>(&data[start]), pos - start);
      if (name.size() >= 2) {
        std::cout << "  " << name << std::endl;
        count++;
      }
    }
    if (pos < data.size()) pos++;
  }
}

int Application::run() {
  std::cout << "Loading ISO: " << iso_path_ << std::endl;

  try {
    c3::Iso9660 iso(iso_path_);

    std::cout << "Volume: " << iso.volume_id() << std::endl;
    std::cout << "Logical block size: " << iso.logical_block_size() << std::endl;
    std::cout << "Root dir extent: " << iso.root_dir_extent() << " (size: " << iso.root_dir_size() << ")" << std::endl;

    // Dump c3.nam to see name table
    std::cout << "\n=== C3.NAM NAME TABLE ===" << std::endl;
    dump_c3_nam(iso);

    // List all files in ISO
    std::cout << "\n=== ALL FILES IN ISO ===" << std::endl;
    list_all_files_recursive(iso, "");

    // Build resource index from ISO (finds shards recursively)
    auto resource_index = c3::ResourceIndex::build(iso);

    std::cout << "\nShards found: " << resource_index.shard_paths().size() << std::endl;
    std::cout << "Total entries indexed: " << resource_index.entries().size() << std::endl;

    // List some entries
    size_t count = 0;
    for (const auto& [name, entry] : resource_index.entries()) {
      if (count < 20) {
        std::cout << "  " << name << " (shard " << entry.shard << ", offset " << entry.offset << ", size " << entry.size << ")" << std::endl;
      }
      ++count;
    }
    if (count > 20) {
      std::cout << "  ... and " << (count - 20) << " more" << std::endl;
    }

    std::cout << "\nISO read successful!" << std::endl;
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}

} // namespace c3