#include "iso9660.hpp"
#include <fstream>
#include <algorithm>
#include <cstring>
#include <iostream>

namespace c3 {

struct Iso9660::Impl {
  std::ifstream file;
  std::filesystem::path path;
};

Iso9660::Iso9660(const std::filesystem::path& path) : pimpl_(std::make_unique<Impl>()) {
  pimpl_->path = path;
  pimpl_->file.open(path, std::ios::binary);
  if (!pimpl_->file.is_open()) {
    throw std::runtime_error("Failed to open ISO: " + path.string());
  }
  parse_volume_descriptors();
  parse_root_directory();
}

Iso9660::~Iso9660() = default;

Iso9660::Iso9660(Iso9660&& other) noexcept = default;
Iso9660& Iso9660::operator=(Iso9660&& other) noexcept = default;

const std::filesystem::path& Iso9660::path() const {
  return pimpl_->path;
}

std::vector<uint8_t> Iso9660::read_bytes(uint64_t offset, uint64_t size) const {
  std::vector<uint8_t> buffer(size);
  pimpl_->file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  if (!pimpl_->file) {
    throw std::runtime_error("Seek failed at offset " + std::to_string(offset));
  }
  pimpl_->file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
  std::streamsize read = pimpl_->file.gcount();
  if (read < static_cast<std::streamsize>(size)) {
    // Pad with zeros for short reads (e.g., root dir spanning partial sector)
    std::fill(buffer.begin() + read, buffer.end(), 0);
  }
  return buffer;
}

void Iso9660::parse_volume_descriptors() {
  const uint32_t sector_size = 2048;
  // Primary volume descriptor at sector 16 (LBA 16)
  // Also check for Joliet (type 2) supplementary volume descriptor
  uint32_t joliet_root_extent = 0;
  uint32_t joliet_root_size = 0;
  uint16_t joliet_block_size = 0;

  for (uint32_t lba = 16; lba < 100; ++lba) {
    auto sector = read_bytes(lba * sector_size, sector_size);
    if (sector.size() < 7) break;

    uint8_t type = sector[0];
    std::string_view id(reinterpret_cast<const char*>(sector.data() + 1), 5);

    if (id != "CD001") continue;

    if (type == 1) { // Primary Volume Descriptor
      // Volume ID at offset 40, 32 bytes
      volume_id_ = std::string(reinterpret_cast<const char*>(sector.data() + 40), 32);
      while (!volume_id_.empty() && volume_id_.back() == ' ') volume_id_.pop_back();

      volume_space_size_ = *reinterpret_cast<const uint32_t*>(sector.data() + 80);
      logical_block_size_ = *reinterpret_cast<const uint16_t*>(sector.data() + 128);

      auto root_rec = parse_dir_record(sector.data() + 156, 34);
      if (root_rec) {
        root_dir_extent_ = root_rec->extent_lba;
        root_dir_size_ = root_rec->size_bytes;
      }
    } else if (type == 2) { // Supplementary Volume Descriptor (Joliet)
      // Check for Joliet signature at offset 88: "%/@" (0x25 0x2f 0x40)
      if (sector.size() >= 91 && sector[88] == 0x25 && sector[89] == 0x2f && sector[90] == 0x40) {
        joliet_block_size = *reinterpret_cast<const uint16_t*>(sector.data() + 128);
        auto root_rec = parse_dir_record(sector.data() + 156, 34);
        if (root_rec) {
          joliet_root_extent = root_rec->extent_lba;
          joliet_root_size = root_rec->size_bytes;
        }
      }
    } else if (type == 255) { // Volume Descriptor Set Terminator
      break;
    }
  }

  // Prefer Joliet if available (supports long filenames and correct sizes)
  if (joliet_root_extent != 0 && joliet_root_size != 0) {
    root_dir_extent_ = joliet_root_extent;
    root_dir_size_ = joliet_root_size;
    logical_block_size_ = joliet_block_size;
  }
}

std::optional<IsoDirectoryRecord> Iso9660::parse_dir_record(const uint8_t* data, size_t max_len) const {
  if (max_len < 34) return std::nullopt; // Minimum 34 bytes (33 fixed + 1 name)

  uint8_t len = data[0];
  if (len == 0 || len > max_len) return std::nullopt;

  IsoDirectoryRecord rec;
  rec.extent_lba = *reinterpret_cast<const uint32_t*>(data + 2); // LE
  rec.size_bytes = *reinterpret_cast<const uint32_t*>(data + 10); // LE
  rec.flags = data[25]; // offset 25: File Flags (after 7-byte datetime at 10-16, file_unit_size at 17, interleave at 18, vol_seq 4 bytes at 19-22)
  rec.is_directory = (rec.flags & 0x02) != 0;

  uint8_t name_len = data[32]; // offset 32: Length of File Identifier (after vol_seq 4 bytes at 25-28? wait...)
  // Actually: 1(len)+1(ext_attr)+4(extent)+4(size)+7(datetime)+1(flags)+1(file_unit)+1(interleave)+4(vol_seq)+1(name_len) = 25 bytes before name_len
  // So name_len at offset 33? Let me check raw data...
  // Raw: offset 25=flags, 26=file_unit, 27=interleave, 28-31=vol_seq(4), 32=name_len, 33=name
  if (33 + name_len > len) return std::nullopt;

  std::string name(reinterpret_cast<const char*>(data + 33), name_len);
  // Remove ;1 version suffix
  if (name.size() >= 2 && name[name.size() - 2] == ';') {
    name.resize(name.size() - 2);
  }
  // Convert to lowercase for case-insensitive matching
  std::transform(name.begin(), name.end(), name.begin(), ::tolower);
  rec.name = name;

  return rec;
}

void Iso9660::read_dir_records(uint32_t extent_lba, uint32_t size_bytes, std::vector<IsoDirectoryRecord>& out) const {
  const uint32_t sector_size = logical_block_size_;
  uint64_t offset = static_cast<uint64_t>(extent_lba) * sector_size;

  // Read exactly size_bytes (the directory size from the directory record)
  // ISO9660 directories can span multiple sectors, and size_bytes should reflect the total
  auto data = read_bytes(offset, size_bytes);

  size_t pos = 0;
  int record_num = 0;
  while (pos < data.size()) {
    if (pos + 1 > data.size()) break;
    uint8_t len = data[pos];
    if (len == 0) {
      // Pad to next sector
      pos = ((pos + sector_size - 1) / sector_size) * sector_size;
      continue;
    }
    if (pos + len > data.size()) {
      break;
    }

    auto rec = parse_dir_record(data.data() + pos, len);
    if (rec) {
      out.push_back(*rec);
    }
    pos += len;
    record_num++;
  }
}

void Iso9660::parse_root_directory() {
  // Already parsed in parse_volume_descriptors
}

std::optional<IsoDirectoryRecord> Iso9660::find_file(std::string_view path) const {
  std::string lower_path(path);
  std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);

  // Split path
  std::vector<std::string> parts;
  size_t start = 0;
  while (start < lower_path.size()) {
    size_t end = lower_path.find('/', start);
    if (end == std::string::npos) end = lower_path.size();
    if (end > start) parts.push_back(lower_path.substr(start, end - start));
    start = end + 1;
  }

  // Handle root directory "/"
  if (parts.empty()) {
    IsoDirectoryRecord root_rec;
    root_rec.name = "";
    root_rec.extent_lba = root_dir_extent_;
    root_rec.size_bytes = root_dir_size_;
    root_rec.flags = 0x02;
    root_rec.is_directory = true;
    return root_rec;
  }

  uint32_t current_extent = root_dir_extent_;
  uint32_t current_size = root_dir_size_;

  for (size_t i = 0; i < parts.size(); ++i) {
    std::vector<IsoDirectoryRecord> entries;
    read_dir_records(current_extent, current_size, entries);

    bool found = false;
    for (const auto& entry : entries) {
      if (entry.name == parts[i]) {
        if (i == parts.size() - 1) {
          return entry;
        }
        if (entry.is_directory) {
          current_extent = entry.extent_lba;
          current_size = entry.size_bytes;
          found = true;
          break;
        }
      }
    }
    if (!found) return std::nullopt;
  }
  return std::nullopt;
}

std::vector<IsoDirectoryRecord> Iso9660::list_directory(std::string_view path) const {
  auto rec = find_file(path);
  if (!rec || !rec->is_directory) return {};

  std::vector<IsoDirectoryRecord> entries;
  read_dir_records(rec->extent_lba, rec->size_bytes, entries);
  return entries;
}

} // namespace c3