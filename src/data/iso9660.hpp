#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <string_view>
#include <filesystem>

namespace c3 {

struct IsoDirectoryRecord {
  uint32_t extent_lba = 0;
  uint32_t size_bytes = 0;
  uint8_t flags = 0;
  std::string name;
  bool is_directory = false;
};

class Iso9660 {
public:
  explicit Iso9660(const std::filesystem::path& path);
  ~Iso9660();

  // Non-copyable, movable
  Iso9660(const Iso9660&) = delete;
  Iso9660& operator=(const Iso9660&) = delete;
  Iso9660(Iso9660&&) noexcept;
  Iso9660& operator=(Iso9660&&) noexcept;

  // Read raw bytes from ISO (handles sector alignment)
  std::vector<uint8_t> read_bytes(uint64_t offset, uint64_t size) const;

  // Find file by path (case-insensitive, handles ;1 version)
  std::optional<IsoDirectoryRecord> find_file(std::string_view path) const;

  // List directory contents
  std::vector<IsoDirectoryRecord> list_directory(std::string_view path) const;

  // Get volume info
  std::string volume_id() const { return volume_id_; }
  uint32_t volume_space_size() const { return volume_space_size_; }
  uint32_t logical_block_size() const { return logical_block_size_; }
  uint32_t root_dir_extent() const { return root_dir_extent_; }
  uint32_t root_dir_size() const { return root_dir_size_; }
  const std::filesystem::path& path() const;

private:
  void parse_volume_descriptors();
  void parse_root_directory();
  std::optional<IsoDirectoryRecord> parse_dir_record(const uint8_t* data, size_t max_len) const;
  void read_dir_records(uint32_t extent_lba, uint32_t size_bytes, std::vector<IsoDirectoryRecord>& out) const;

  struct Impl;
  std::unique_ptr<Impl> pimpl_;
  std::string volume_id_;
  uint32_t volume_space_size_ = 0;
  uint32_t logical_block_size_ = 2048;
  uint32_t root_dir_extent_ = 0;
  uint32_t root_dir_size_ = 0;
};

} // namespace c3