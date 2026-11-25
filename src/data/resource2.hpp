#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <string_view>
#include <filesystem>
#include <memory>
#include <fstream>
#include "iso9660.hpp"

namespace c3 {

struct ResourceEntry {
  uint32_t offset = 0;
  uint32_t size = 0;
  uint32_t shard = 0; // 0-3 for RESOURCE.RES, ._01, ._02, ._03
  std::string name;
};

class ResourceIndex {
public:
  // Build index from ISO (finds RESOURCE.RES and shards)
  static ResourceIndex build(const Iso9660& iso);

  // Load from pre-extracted files on disk
  static ResourceIndex load_from_disk(const std::filesystem::path& base_path);

  // Get entry by name (case-insensitive)
  std::optional<ResourceEntry> find(std::string_view name) const;

  // Get all entries
  const std::unordered_map<std::string, ResourceEntry>& entries() const { return entries_; }

  // Read raw resource data (handles shard boundaries)
  std::vector<uint8_t> read_resource(const ResourceEntry& entry) const;

  // Shard file paths
  const std::vector<std::filesystem::path>& shard_paths() const { return shard_paths_; }

private:
  ResourceIndex() = default;

  std::unordered_map<std::string, ResourceEntry> entries_;
  std::vector<std::filesystem::path> shard_paths_; // [0]=RESOURCE.RES, [1]=._01, etc.

  // Parse RESOURCE2 header
  void parse_header(std::istream& stream, uint32_t shard_idx);
};

} // namespace c3