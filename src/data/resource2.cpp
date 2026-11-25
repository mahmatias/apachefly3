#include "resource2.hpp"
#include "iso9660.hpp"
#include <fstream>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <unordered_set>

namespace c3 {

// Recursively search for a file by name starting from a given path
static std::optional<IsoDirectoryRecord> find_file_recursive(const Iso9660& iso, std::string_view start_path, std::string_view target_name, std::unordered_set<uint32_t>& visited, int depth = 0) {
  if (depth > 10) return std::nullopt;

  auto entries = iso.list_directory(start_path);
  for (const auto& entry : entries) {
    if (entry.name.empty() || entry.name == "." || entry.name == "..") continue;

    std::string lower_name = entry.name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    std::string lower_target(target_name);
    std::transform(lower_target.begin(), lower_target.end(), lower_target.begin(), ::tolower);

    if (lower_name == lower_target) {
      return entry;
    }

    if (entry.is_directory) {
      if (visited.count(entry.extent_lba)) continue;
      visited.insert(entry.extent_lba);

      std::string full_path = std::string(start_path) + (start_path == "/" ? "" : "/") + entry.name;
      auto result = find_file_recursive(iso, full_path, target_name, visited, depth + 1);
      if (result) return result;
    }
  }
  return std::nullopt;
}

ResourceIndex ResourceIndex::build(const Iso9660& iso) {
  ResourceIndex index;

  // Find RESOURCE.RES and shards in ISO (search recursively from root)
  std::vector<std::string> shard_names = {"resource.res", "resource._01", "resource._02", "resource._03"};

  // Get ISO file size for bounds checking
  uint64_t iso_file_size = std::filesystem::file_size(iso.path());

  for (size_t i = 0; i < shard_names.size(); ++i) {
    std::unordered_set<uint32_t> visited;
    visited.insert(iso.root_dir_extent());
    auto entry = find_file_recursive(iso, "/", shard_names[i], visited);
    if (!entry) {
      if (i == 0) {
        throw std::runtime_error("RESOURCE.RES not found in ISO");
      }
      break; // Optional shards
    }

    // The ISO directory entries only show 2048 bytes (one sector) but actual files are much larger.
    // Strategy: read first 64KB to parse header, find index_offset, then parse index to determine
    // actual file size, then read exactly that much.
    uint64_t start_offset = static_cast<uint64_t>(entry->extent_lba) * iso.logical_block_size();

    // Step 1: Read first 64KB to parse header
    const uint64_t initial_read_size = 64 * 1024;
    auto initial_data = iso.read_bytes(start_offset, initial_read_size);

    // Parse header from initial data to find index_offset
    // NOTE: The header field names are SWAPPED!
    // "index_offset" (at 48/72) actually points to the DATA section (3DO1 models)
    // "data_offset" (at 52/68) actually points to the INDEX region end (name table start)
    uint32_t header_data_section_start = 0;  // "index_offset" in header - points to 3DO1 model data
    uint32_t header_index_region_end = 0;    // "data_offset" in header - points to index end
    uint8_t shard_id = 0;
    {
      if (initial_data.size() >= 80) {
        const uint8_t* header = initial_data.data();

        // Check magic
        if (header[0] == 'R' && header[1] == 'E' && header[2] == 'S' && header[3] == 'O' &&
            header[4] == 'U' && header[5] == 'R' && header[6] == 'C' && header[7] == 'E' &&
            header[8] == '2') {
          shard_id = header[9];

          // Based on raw header analysis:
          // For all ISO shards, the correct offsets are:
          // - Layout 1 (shards 0,3): header_data_section_start at 72, header_index_region_end at 68
          // - Layout 2 (shards 1,2): header_data_section_start at 48, header_index_region_end at 52

          uint32_t hdr_data_start_1 = *reinterpret_cast<const uint32_t*>(&header[72]);
          uint32_t hdr_index_end_1 = *reinterpret_cast<const uint32_t*>(&header[68]);
          uint32_t hdr_data_start_2 = *reinterpret_cast<const uint32_t*>(&header[48]);
          uint32_t hdr_index_end_2 = *reinterpret_cast<const uint32_t*>(&header[52]);

          // Valid layout: header_index_region_end < header_data_section_start, both reasonable (< file_size), data_start > 1000
          uint32_t file_size = entry->size_bytes;
          if (hdr_index_end_1 < hdr_data_start_1 && hdr_data_start_1 < file_size && hdr_data_start_1 > 1000) {
            header_data_section_start = hdr_data_start_1;
            header_index_region_end = hdr_index_end_1;
          } else if (hdr_index_end_2 < hdr_data_start_2 && hdr_data_start_2 < file_size && hdr_data_start_2 > 1000) {
            header_data_section_start = hdr_data_start_2;
            header_index_region_end = hdr_index_end_2;
          }
        }
      }
    }

    if (header_data_section_start == 0) {
      continue;
    }

    // The index is in the region between header (76 bytes) and header_index_region_end
    // The data section starts at header_data_section_start
    // We need to read up to header_data_section_start to get the index + name table
    uint64_t needed_size = header_data_section_start + 5000; // cover index + some name table
    if (needed_size > initial_read_size) {
      initial_data = iso.read_bytes(start_offset, needed_size);
    }

    // Step 2: Read the index from initial data
    // The index region is [76, header_index_region_end)
    uint32_t num_entries = 0;
    uint32_t name_table_size = 0;
    {
      if (initial_data.size() > header_index_region_end) {
        const uint8_t* index_ptr = initial_data.data() + 76;  // Start after header
        uint32_t remaining = static_cast<uint32_t>(header_index_region_end - 76);

        // FIRST: Try parsing index from after header
        for (uint32_t entry_idx = 0; entry_idx * 20 + 20 <= remaining; ++entry_idx) {
          uint32_t entry_data_off = *reinterpret_cast<const uint32_t*>(index_ptr + entry_idx * 20);
          uint32_t entry_size = *reinterpret_cast<const uint32_t*>(index_ptr + entry_idx * 20 + 4);

          if (entry_data_off >= header_data_section_start && entry_size > 0 && entry_size < 100 * 1024 * 1024) {
            num_entries = entry_idx + 1;
          } else {
            break;
          }
        }

        if (num_entries == 0) {
          // FALLBACK: scan for valid index entries in [76, header_index_region_end)
          uint32_t best_num_entries = 0;
          uint32_t best_index_start = 0;

          for (uint32_t scan_offset = 76; scan_offset + 20 <= header_index_region_end; scan_offset += 4) {
            uint32_t local_entries = 0;
            for (uint32_t entry_idx = 0; scan_offset + entry_idx * 20 + 20 <= header_index_region_end; ++entry_idx) {
              uint32_t entry_data_off = *reinterpret_cast<const uint32_t*>(initial_data.data() + scan_offset + entry_idx * 20);
              uint32_t entry_size = *reinterpret_cast<const uint32_t*>(initial_data.data() + scan_offset + entry_idx * 20 + 4);

              if (entry_data_off >= header_data_section_start && entry_size > 0 && entry_size < 100 * 1024 * 1024) {
                local_entries = entry_idx + 1;
              } else {
                break;
              }
            }

            if (local_entries > best_num_entries) {
              best_num_entries = local_entries;
              best_index_start = scan_offset;
            }
          }

          if (best_num_entries > 0) {
            num_entries = best_num_entries;
            // index_offset = best_index_start; // Not needed anymore, we use 76 as base
          }
        }

        // Now find name table after the index
        uint32_t name_start = 76 + num_entries * 20;
        if (name_start < initial_data.size()) {
          const uint8_t* name_ptr = initial_data.data() + name_start;
          uint32_t name_remaining = static_cast<uint32_t>(initial_data.size() - name_start);
          for (uint32_t j = 0; j + 1 < name_remaining; ++j) {
            if (name_ptr[j] == 0 && name_ptr[j + 1] == 0) {
              name_table_size = j + 2;
              break;
            }
          }
          if (name_table_size == 0 && name_remaining > 0) {
            name_table_size = std::min<uint32_t>(num_entries * 32, 64 * 1024);
          }
        }
      }
    }

    // Step 3: Use the ISO directory entry's size_bytes (which has the correct full file size)
    // The earlier comment about "2048 bytes only" was incorrect - the directory records have actual sizes
    uint32_t actual_file_size = entry->size_bytes;

    uint64_t max_possible = iso_file_size - start_offset;
    if (actual_file_size > max_possible) {
      actual_file_size = static_cast<uint32_t>(max_possible);
    }

    // Step 4: Read exactly the file size
    auto full_data = iso.read_bytes(start_offset, actual_file_size);

    // Write to temp file
    std::filesystem::path temp_path = std::filesystem::temp_directory_path() / ("c3_shard_" + std::to_string(i) + ".tmp");
    {
      std::ofstream out(temp_path, std::ios::binary);
      out.write(reinterpret_cast<const char*>(full_data.data()), full_data.size());
    }
    index.shard_paths_.push_back(temp_path);
  }

  // Parse each shard
  for (size_t i = 0; i < index.shard_paths_.size(); ++i) {
    std::ifstream stream(index.shard_paths_[i], std::ios::binary);
    if (!stream) continue;
    // Pass header_data_section_start as "data_offset" and header_index_region_end as "index_offset"
    // to maintain API compatibility with parse_header
    // Actually parse_header reads the header again, so it will re-detect the layout
    index.parse_header(stream, static_cast<uint32_t>(i));
  }

  return index;
}

ResourceIndex ResourceIndex::load_from_disk(const std::filesystem::path& base_path) {
  ResourceIndex index;

  std::vector<std::string> shard_names = {"RESOURCE.RES", "RESOURCE._01", "RESOURCE._02", "RESOURCE._03"};

  for (size_t i = 0; i < shard_names.size(); ++i) {
    auto path = base_path / shard_names[i];
    if (!std::filesystem::exists(path)) {
      if (i == 0) {
        throw std::runtime_error("RESOURCE.RES not found at " + path.string());
      }
      break;
    }
    index.shard_paths_.push_back(path);
  }

  for (size_t i = 0; i < index.shard_paths_.size(); ++i) {
    std::ifstream stream(index.shard_paths_[i], std::ios::binary);
    if (!stream) continue;
    index.parse_header(stream, static_cast<uint32_t>(i));
  }

  return index;
}

void ResourceIndex::parse_header(std::istream& stream, uint32_t shard_idx) {
  // RESOURCE2 header (observed from actual ISO data):
  // 0-11:     "RESOURCE2" + 3 bytes padding (12 bytes)
  // 12-15:    version (uint32)
  // 16-23:    unknown1 (uint64)
  // 24-27:    zero (uint32)
  // 28-43:    first_entry data (data_offset, size, hash - 16 bytes) x2?
  // 68-71:    data_offset (uint32) - WHERE FILE DATA STARTS (shards 0,3) - CORRECT
  // 72-75:    index_offset (uint32) - WHERE THE FULL INDEX STARTS (shards 0,3) - CORRECT
  // 48-51:    index_offset (uint32) - WHERE THE FULL INDEX STARTS (shards 1,2) - CORRECT
  // 52-55:    data_offset (uint32) - WHERE FILE DATA STARTS (shards 1,2) - CORRECT
  // Total header: 64 bytes
  //
  // IMPORTANT: ISO shards do NOT have num_entries in header!
  // Data comes FIRST (at data_offset, typically small like 880), then index at END (index_offset, large like 18296)
  // Index entries are 20 bytes each: data_offset(4), size(4), hash(8), flags(4)

  // Read entire header (need at least 80 bytes to access offsets 68-75)
  std::vector<uint8_t> header(80);
  stream.read(reinterpret_cast<char*>(header.data()), 80);
  if (stream.gcount() < 64) {
    std::cerr << "Shard " << shard_idx << ": Failed to read header (got " << stream.gcount() << " bytes)" << std::endl;
    return;
  }

  std::cout << "Shard " << shard_idx << ": Header read OK, magic="
            << char(header[0]) << char(header[1]) << char(header[2]) << char(header[3])
            << char(header[4]) << char(header[5]) << char(header[6]) << char(header[7])
            << char(header[8]) << ", shard_id=" << (int)header[9] << std::endl;
  std::cout.flush();

  // Print raw header bytes at key offsets as hex
  std::cout << "Shard " << shard_idx << ": header[48..55] hex: ";
  for (int k = 48; k < 56; ++k) printf("%02x ", header[k]);
  std::cout << std::endl;
  std::cout << "Shard " << shard_idx << ": header[68..75] hex: ";
  for (int k = 68; k < 76; ++k) printf("%02x ", header[k]);
  std::cout << std::endl;
  std::cout.flush();

  std::cout << "Shard " << shard_idx << ": About to get file size..." << std::endl;
  std::cout.flush();

  // Check for "RESOURCE2" prefix (first 9 bytes)
  if (header[0] != 'R' || header[1] != 'E' || header[2] != 'S' || header[3] != 'O' ||
      header[4] != 'U' || header[5] != 'R' || header[6] != 'C' || header[7] != 'E' ||
      header[8] != '2') {
    std::cerr << "Warning: Invalid RESOURCE2 magic in shard " << shard_idx << std::endl;
    return;
  }

  // Read shard identifier (byte 9)
  uint8_t shard_id = header[9];

  // Read version at offset 12 (bytes 12-15)
  uint32_t version = *reinterpret_cast<const uint32_t*>(&header[12]);

  // Try both possible offset layouts:
  // Layout 1 (shards 0,3): index at 72, data at 68
  // Layout 2 (shards 1,2): index at 48, data at 52
  uint32_t index_offset_1 = *reinterpret_cast<const uint32_t*>(&header[72]);
  uint32_t data_offset_1 = *reinterpret_cast<const uint32_t*>(&header[68]);
  uint32_t index_offset_2 = *reinterpret_cast<const uint32_t*>(&header[48]);
  uint32_t data_offset_2 = *reinterpret_cast<const uint32_t*>(&header[52]);

  // Get file size to validate
  std::cout << "Shard " << shard_idx << ": Seeking to end..." << std::endl;
  std::cout.flush();
  stream.seekg(0, std::ios::end);
  std::cout << "Shard " << shard_idx << ": Tellg..." << std::endl;
  std::cout.flush();
  uint32_t file_size = static_cast<uint32_t>(stream.tellg());
  std::cout << "Shard " << shard_idx << ": File size = " << file_size << std::endl;
  std::cout.flush();
  stream.seekg(0, std::ios::beg);
  std::cout << "Shard " << shard_idx << ": Seek back to beg done" << std::endl;
  std::cout.flush();

  // Determine format type and interpret header fields correctly
  // ISO format (shards 0,3): Header fields are SWAPPED
  //   - header[68] (data_offset) = index_region_end (small, ~880)
  //   - header[72] (index_offset) = data_section_start (larger, ~18296)
  //   - header[52] (data_offset) = index_region_end (small, ~880)
  //   - header[48] (index_offset) = data_section_start (larger, ~93996)
  // Installed game format (all shards): Header fields are CORRECT
  //   - header[52] (data_offset) = data_section_start (moderate, ~35674)
  //   - header[48] (index_offset) = index_region_start (larger, ~89576)
  //
  // KEY DISTINCTION:
  // - ISO format: The value at "data_offset" position is VERY SMALL (< 2000) = index_region_end
  // - Installed format: The value at "data_offset" position is MODERATE (> 10000) = data_section_start
  uint32_t data_section_start = 0;
  uint32_t index_region_end = 0;
  bool is_iso_format = false;

  // Layout 1: data_offset at 68, index_offset at 72
  // Layout 2: data_offset at 52, index_offset at 48

  // For ISO format, the data_offset field (at 68 or 52) contains a SMALL value (< 2000)
  // For installed format, the data_offset field (at 52) contains a MODERATE value (> 10000)

  bool layout_1_is_iso = (data_offset_1 > 64) && (data_offset_1 < 2000) && (index_offset_1 > data_offset_1) && (index_offset_1 < file_size);
  bool layout_2_is_iso = (data_offset_2 > 64) && (data_offset_2 < 2000) && (index_offset_2 > data_offset_2) && (index_offset_2 < file_size);

  bool layout_1_is_installed = (data_offset_1 > 10000) && (data_offset_1 < index_offset_1) && (index_offset_1 < file_size);
  bool layout_2_is_installed = (data_offset_2 > 10000) && (data_offset_2 < index_offset_2) && (index_offset_2 < file_size);

  std::cout << "Shard " << shard_idx << ": Layout checks: layout_1_iso=" << layout_1_is_iso << " layout_2_iso=" << layout_2_is_iso
            << " layout_1_inst=" << layout_1_is_installed << " layout_2_inst=" << layout_2_is_installed << std::endl;
  std::cout << "  layout_1: data_off=" << data_offset_1 << " index_off=" << index_offset_1
            << " layout_2: data_off=" << data_offset_2 << " index_off=" << index_offset_2 << std::endl;
  std::cout.flush();

  if (layout_1_is_iso && !layout_2_is_iso && !layout_1_is_installed && !layout_2_is_installed) {
    // ISO format via layout 1 (shards 0,3): header fields are SWAPPED
    data_section_start = index_offset_1;   // header's "index_offset" -> data section
    index_region_end = data_offset_1;      // header's "data_offset" -> index region end
    is_iso_format = true;
  } else if (layout_2_is_iso && !layout_1_is_iso && !layout_1_is_installed && !layout_2_is_installed) {
    // ISO format via layout 2 (shards 1,2): header fields are SWAPPED
    data_section_start = index_offset_2;   // header's "index_offset" -> data section
    index_region_end = data_offset_2;      // header's "data_offset" -> index region end
    is_iso_format = true;
  } else if (layout_1_is_installed && !layout_2_is_installed && !layout_1_is_iso && !layout_2_is_iso) {
    // Installed game format via layout 1
    data_section_start = data_offset_1;    // header's "data_offset" -> data section
    index_region_end = index_offset_1;     // header's "index_offset" -> index region start
    is_iso_format = false;
  } else if (layout_2_is_installed && !layout_1_is_installed && !layout_1_is_iso && !layout_2_is_iso) {
    // Installed game format via layout 2
    data_section_start = data_offset_2;    // header's "data_offset" -> data section
    index_region_end = index_offset_2;     // header's "index_offset" -> index region start
    is_iso_format = false;
  } else if (layout_1_is_iso && layout_2_is_iso) {
    // Both layouts indicate ISO - pick the one with more sensible values
    if (index_offset_1 < index_offset_2) {
      data_section_start = index_offset_1;
      index_region_end = data_offset_1;
    } else {
      data_section_start = index_offset_2;
      index_region_end = data_offset_2;
    }
    is_iso_format = true;
  } else if (layout_1_is_installed && layout_2_is_installed) {
    // Both layouts indicate installed - pick layout 2 (standard)
    data_section_start = data_offset_2;
    index_region_end = index_offset_2;
    is_iso_format = false;
  } else {
    // Ambiguous - use the ISO format heuristic: if data_offset is tiny (< 2000), it's ISO
    if (data_offset_2 < 2000 && index_offset_2 > data_offset_2 && index_offset_2 < file_size) {
      data_section_start = index_offset_2;
      index_region_end = data_offset_2;
      is_iso_format = true;
    } else if (data_offset_1 < 2000 && index_offset_1 > data_offset_1 && index_offset_1 < file_size) {
      data_section_start = index_offset_1;
      index_region_end = data_offset_1;
      is_iso_format = true;
    } else if (data_offset_2 > 10000 && data_offset_2 < index_offset_2 && index_offset_2 < file_size) {
      data_section_start = data_offset_2;
      index_region_end = index_offset_2;
      is_iso_format = false;
    } else if (data_offset_1 > 10000 && data_offset_1 < index_offset_1 && index_offset_1 < file_size) {
      data_section_start = data_offset_1;
      index_region_end = index_offset_1;
      is_iso_format = false;
    } else {
      std::cerr << "Shard " << shard_idx << ": No valid layout found. data_offset_1=" << data_offset_1
                << " index_offset_1=" << index_offset_1 << " data_offset_2=" << data_offset_2
                << " index_offset_2=" << index_offset_2 << " file_size=" << file_size << std::endl;
      return;
    }
  }

  std::cout << "Shard " << shard_idx << ": Using " << (is_iso_format ? "ISO" : "INSTALLED") << " format"
            << " data_section_start=" << data_section_start << " index_region_end=" << index_region_end << " file_size=" << file_size << std::endl;

  std::cout << "Shard " << shard_idx << ": Using layout " << (data_section_start == index_offset_1 ? "1" : "2")
            << " data_section_start=" << data_section_start << " index_region_end=" << index_region_end << " file_size=" << file_size << std::endl;

  // KEY INSIGHT: The header interpretation depends on format:
  // ISO format: index_region_end (header's data_offset) is small, index goes from 76 to data_section_start
  // Installed format: data_section_start (header's data_offset), index goes from index_region_end to near file end
  uint32_t index_region_start = 0;
  uint32_t index_read_size = 0;
  uint32_t data_section_size = file_size - data_section_start;

  if (is_iso_format) {
    // ISO format: index region is from header (76) to data_section_start (header's index_offset field)
    // For all shards: data_section_start is the large value where 3DO1 model data begins
    // The small value (index_region_end) is where the INDEX ENDS and name table begins (for small shards)
    index_region_start = 76;
    index_read_size = data_section_start - 76;

    // For shards with SMALL data sections (shards 1,3), name table is AFTER index (at data_section_start)
    if (data_section_size < 50 * 1024 * 1024) {
      // Small data section (shards 1,3) - read extra to capture name table after index
      index_read_size = std::min<uint32_t>(file_size - 76, data_section_start - 76 + 64 * 1024);
      std::cout << "Shard " << shard_idx << ": Small data section - extending read to " << index_read_size << " bytes (up to offset " << (76 + index_read_size) << ") to capture name table" << std::endl;
    }
  } else {
    // Installed game format: The header fields are CORRECT:
    // - data_offset (at 52) = data_section_start (where 3DO1 models begin)
    // - index_offset (at 48) = index_region_end (where index region ends)
    // The index is in the region BEFORE the data section, i.e., [76, data_section_start)
    // This is the SAME as ISO format!
    index_region_start = 76;
    index_read_size = data_section_start - 76;

    // For small data sections, read extra to capture name table after index
    if (data_section_size < 50 * 1024 * 1024) {
      index_read_size = std::min<uint32_t>(file_size - 76, data_section_start - 76 + 64 * 1024);
      std::cout << "Shard " << shard_idx << ": Small data section - extending read to " << index_read_size << " bytes (up to offset " << (76 + index_read_size) << ") to capture name table" << std::endl;
    }
    std::cout << "Shard " << shard_idx << ": Installed format - index region from " << index_region_start << " to " << data_section_start << " (" << index_read_size << " bytes)" << std::endl;
  }

  std::cout << "Shard " << shard_idx << ": Reading index region (from " << index_region_start << ", size=" << index_read_size << " bytes)" << std::endl;
  stream.seekg(index_region_start, std::ios::beg);
  std::vector<uint8_t> index_data(index_read_size);
  stream.read(reinterpret_cast<char*>(index_data.data()), index_read_size);

  // For ALL shards, read the end of file for name table (might be at end for all)
  std::vector<uint8_t> end_file_data;
  uint32_t end_region_size = 0;
  {
    // Read last 256KB of file (should contain name table)
    uint32_t read_size = std::min<uint32_t>(256 * 1024, file_size);
    stream.seekg(file_size - read_size, std::ios::beg);
    if (stream.tellg() < 0) stream.seekg(0, std::ios::beg);
    end_region_size = file_size - static_cast<uint32_t>(stream.tellg());
    end_file_data.resize(end_region_size);
    stream.read(reinterpret_cast<char*>(end_file_data.data()), end_region_size);
    std::cout << "Shard " << shard_idx << ": Also reading end region (last " << end_region_size << " bytes of file, data_section_size=" << data_section_size << ")" << std::endl;
  }
  // DEBUG: Print first and last 256 bytes of end region
  std::cout << "Shard " << shard_idx << ": End region (first 256 bytes):" << std::endl;
    for (int row = 0; row < 16 && row * 16 < static_cast<int>(end_region_size); ++row) {
      std::cout << "  " << std::hex << std::setw(4) << std::setfill('0') << (row * 16) << ": ";
      for (int col = 0; col < 16; ++col) {
        int idx = row * 16 + col;
        if (idx < static_cast<int>(end_region_size)) {
          std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(end_file_data[idx]) << " ";
        } else {
          std::cout << "   ";
        }
      }
      std::cout << "  ";
      for (int col = 0; col < 16; ++col) {
        int idx = row * 16 + col;
        if (idx < static_cast<int>(end_region_size)) {
          char c = static_cast<char>(end_file_data[idx]);
          std::cout << (c >= 32 && c < 127 ? c : '.');
        } else {
          std::cout << " ";
        }
      }
      std::cout << std::dec << std::endl;
    }

    if (end_region_size > 256) {
      std::cout << "Shard " << shard_idx << ": End region (last 256 bytes):" << std::endl;
      for (int row = 0; row < 16; ++row) {
        uint32_t idx = end_region_size - 256 + row * 16;
        std::cout << "  " << std::hex << std::setw(4) << std::setfill('0') << idx << ": ";
        for (int col = 0; col < 16; ++col) {
          int idx2 = idx + col;
          if (idx2 < static_cast<int>(end_region_size)) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(end_file_data[idx2]) << " ";
          } else {
            std::cout << "   ";
          }
        }
        std::cout << "  ";
        for (int col = 0; col < 16; ++col) {
          int idx2 = idx + col;
          if (idx2 < static_cast<int>(end_region_size)) {
            char c = static_cast<char>(end_file_data[idx2]);
            std::cout << (c >= 32 && c < 127 ? c : '.');
          } else {
            std::cout << " ";
          }
        }
        std::cout << std::dec << std::endl;
      }
    }

  // Also read the data section start for reference (first few bytes after data_section_start)
  std::vector<uint8_t> data_section_sample;
  if (data_section_start + 256 <= file_size) {
    stream.seekg(data_section_start, std::ios::beg);
    data_section_sample.resize(256);
    stream.read(reinterpret_cast<char*>(data_section_sample.data()), 256);
  }

  // For debugging: print the index region
  std::cout << "Shard " << shard_idx << ": Index region (first " << std::min<uint32_t>(256, index_read_size) << " bytes):" << std::endl;
  for (int row = 0; row < 16 && row * 16 < static_cast<int>(index_read_size); ++row) {
    std::cout << "  " << std::hex << std::setw(4) << std::setfill('0') << (row * 16) << ": ";
    for (int col = 0; col < 16; ++col) {
      int idx = row * 16 + col;
      if (idx < static_cast<int>(index_read_size)) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(index_data[idx]) << " ";
      } else {
        std::cout << "   ";
      }
    }
    std::cout << "  ";
    for (int col = 0; col < 16; ++col) {
      int idx = row * 16 + col;
      if (idx < static_cast<int>(index_read_size)) {
        char c = static_cast<char>(index_data[idx]);
        std::cout << (c >= 32 && c < 127 ? c : '.');
      } else {
        std::cout << " ";
      }
    }
    std::cout << std::dec << std::endl;
  }

  // Rename file_data to index_data for the rest of the function
  std::vector<uint8_t>& file_data = index_data;
  uint32_t index_scan_size_for_scan = index_data.size();

  // Format-specific index entry sizes and layouts:
  // ISO format: 16-byte entries (data_offset, hash1, hash2, hash3)
  // Installed game format: 20-byte entries (data_offset, size, hash1, hash2, flags)

  // Hash patterns for ISO format
  static const uint32_t iso_hash_patterns[] = {
    0xFA9FBBAC, 0xFFAC9C9F, 0xACBB9FFA, 0xE99BBD9C,
    0xE999ACFF, 0xE996BE9C, 0xE997BEE4, 0xE991A69C,
    0xE991AEE7, 0xEF89AC9C, 0xEF89ACF3, 0xEC8EAC9D,
    0x9F9CACFF,
    // Shards 0 & 3 patterns (from ISO debug output)
    0xEEBF97EC, 0x82A091E2, 0xACA29A9E, 0xE3A9ED83,
    0xE2A992EF, 0x82A581EA, 0xE4AC8EEC, 0x9DA19CF2,
    0xE3A9ED83,
    // Shard 3 additional patterns
    0xEF92A9E2, 0xEA81A182, 0xEA81A082, 0xEF8BA1E0,
    0xE88AC39F, 0xE991EDAC, 0xEE93ACE4, 0xF29CA19D,
    0xF29CA2E8, 0xF298ACE1, 0xF299A39E, 0x83EDA9E3
  };

  // For installed game, the 20-byte format has hash at offsets 8,12
  // We'll use the same pattern matching but the data_offset should be in data section

  uint32_t max_data_absolute = file_size;
  std::cout << "Shard " << shard_idx << ": Data section at absolute " << data_section_start << ", max absolute offset: " << max_data_absolute << std::endl;

  // Track stride for parsing (detected during scanning)
  uint32_t best_stride = 0;

  // The index data we read starts at index_region_start
  // For ISO: index_region_start = 76, index_read_size = data_section_start - 76
  // For installed: index_region_start = index_region_end (header's index_offset), index_read_size = file_size - index_region_end
  // In both cases, index_data[0] corresponds to absolute offset index_region_start
  uint32_t scan_start = 0;
  uint32_t scan_end = index_read_size;
  uint32_t index_scan_size = index_read_size;

  std::cout << "Shard " << shard_idx << ": Index region is [" << 76 << ", " << data_section_start << ") = " << index_scan_size << " bytes" << std::endl;
  std::cout << "Shard " << shard_idx << ": Scanning for index in range [" << scan_start << ", " << scan_end << ")" << std::endl;
  std::cout << "Shard " << shard_idx << ": Name table expected at end of file (for large shards) or after index (for small shards)" << std::endl;

  uint32_t best_num_entries = 0;
  uint32_t best_index_start = 0;
  uint32_t best_entry_size = 0;

  auto has_known_hash = [&](uint32_t scan_offset, int entry_size) -> bool {
    // Check if any of the hash words match known patterns
    // For 20-byte entries (installed): hash at offsets 8, 12
    // For 16-byte entries (ISO): hash at offsets 4, 8, 12
    const uint32_t* patterns = is_iso_format ? iso_hash_patterns : iso_hash_patterns; // installed game also uses similar patterns
    size_t num_patterns = is_iso_format ? sizeof(iso_hash_patterns)/sizeof(iso_hash_patterns[0]) : sizeof(iso_hash_patterns)/sizeof(iso_hash_patterns[0]);

    if (entry_size == 20) {
      // Installed format: data_off(4), size(4), hash1(4), hash2(4), flags(4) - hash at 8, 12
      for (int hash_off : {8, 12}) {
        if (scan_offset + hash_off + 4 <= index_scan_size) {
          uint32_t hash_val = *reinterpret_cast<const uint32_t*>(file_data.data() + scan_offset + hash_off);
          for (size_t pi = 0; pi < num_patterns; ++pi) {
            if (hash_val == patterns[pi]) return true;
          }
        }
      }
    } else {
      // ISO format: data_off(4), hash1(4), hash2(4), hash3(4) - hash at 4, 8, 12
      for (int hash_off : {4, 8, 12}) {
        if (scan_offset + hash_off + 4 <= index_scan_size) {
          uint32_t hash_val = *reinterpret_cast<const uint32_t*>(file_data.data() + scan_offset + hash_off);
          for (size_t pi = 0; pi < num_patterns; ++pi) {
            if (hash_val == patterns[pi]) return true;
          }
        }
      }
    }
    return false;
  };

  // Try 20-byte entries first (matches installed game format: data_off, size, hash8, flags)
  auto try_index_at_20 = [&](uint32_t scan_offset) -> uint32_t {
    if (scan_offset + 20 > index_scan_size) return 0;
    // First entry must have known hash pattern
    if (!has_known_hash(scan_offset, 20)) return 0;

    uint32_t local_entries = 0;
    for (uint32_t entry_idx = 0; scan_offset + entry_idx * 20 + 20 <= index_scan_size; ++entry_idx) {
      uint32_t entry_data_off = *reinterpret_cast<const uint32_t*>(file_data.data() + scan_offset + entry_idx * 20);
      uint32_t entry_size_val = *reinterpret_cast<const uint32_t*>(file_data.data() + scan_offset + entry_idx * 20 + 4);
      uint32_t hash1 = *reinterpret_cast<const uint32_t*>(file_data.data() + scan_offset + entry_idx * 20 + 8);
      uint32_t hash2 = *reinterpret_cast<const uint32_t*>(file_data.data() + scan_offset + entry_idx * 20 + 12);
      uint32_t entry_flags = *reinterpret_cast<const uint32_t*>(file_data.data() + scan_offset + entry_idx * 20 + 16);

      // Validate based on format
      bool valid_hash = true;
      if (entry_idx < 5) {
        valid_hash = has_known_hash(scan_offset + entry_idx * 20, 20);
      }

      bool valid_data_off = false;
      if (is_iso_format) {
        // ISO: data_offset must be in data section (>= data_section_start, < file_size) or 0
        valid_data_off = (entry_data_off >= data_section_start && entry_data_off < max_data_absolute) ||
                         (entry_data_off == 0);
      } else {
        // Installed: data_offset must be in data section (>= data_section_start, < file_size)
        valid_data_off = (entry_data_off >= data_section_start && entry_data_off < max_data_absolute);
      }

      if (valid_data_off && entry_size_val > 0 && entry_size_val < 100 * 1024 * 1024 && valid_hash) {
        local_entries = entry_idx + 1;
      } else {
        break;
      }
    }
    return local_entries;
  };

  // Also try 16-byte entries (ISO format)
  auto try_index_at_16 = [&](uint32_t scan_offset) -> uint32_t {
    if (scan_offset + 16 > index_scan_size) return 0;
    // For ISO format, be more lenient with hash check - just check first entry has ANY non-zero hashes
    if (is_iso_format) {
      // Check first entry has non-zero hash words
      uint32_t h1 = *reinterpret_cast<const uint32_t*>(file_data.data() + scan_offset + 4);
      uint32_t h2 = *reinterpret_cast<const uint32_t*>(file_data.data() + scan_offset + 8);
      uint32_t h3 = *reinterpret_cast<const uint32_t*>(file_data.data() + scan_offset + 12);
      if (h1 == 0 && h2 == 0 && h3 == 0) return 0;
    } else {
      if (!has_known_hash(scan_offset, 16)) return 0;
    }

    uint32_t local_entries = 0;
    // Debug for scan_offset=32 and offset 112
    bool debug_this = (scan_offset == 32) || (scan_offset == 112);

    // DEBUG: Show entries for scan_offset 112
    if (scan_offset == 112) {
      std::cout << "  DEBUG: scan_offset=112" << std::endl;
    }

    for (uint32_t entry_idx = 0; scan_offset + entry_idx * 16 + 16 <= index_scan_size; ++entry_idx) {
      uint32_t entry_data_off = *reinterpret_cast<const uint32_t*>(file_data.data() + scan_offset + entry_idx * 16);

      bool valid_hash = true;
      if (entry_idx < 5) {
        if (is_iso_format) {
          // For ISO, just check hashes are non-zero
          uint32_t h1 = *reinterpret_cast<const uint32_t*>(file_data.data() + scan_offset + entry_idx * 16 + 4);
          uint32_t h2 = *reinterpret_cast<const uint32_t*>(file_data.data() + scan_offset + entry_idx * 16 + 8);
          uint32_t h3 = *reinterpret_cast<const uint32_t*>(file_data.data() + scan_offset + entry_idx * 16 + 12);
          valid_hash = (h1 != 0 || h2 != 0 || h3 != 0);
          if (debug_this) {
            std::cout << "  try_index_at_16(" << scan_offset << ") entry " << entry_idx << ": data_off=" << entry_data_off
                      << " h1=" << std::hex << h1 << " h2=" << h2 << " h3=" << h3 << std::dec
                      << " valid_hash=" << valid_hash << std::endl;
          }
        } else {
          valid_hash = has_known_hash(scan_offset + entry_idx * 16, 16);
        }
      }

      bool valid_data_off = false;
      if (is_iso_format) {
        // ISO: data_offset must be in data section (>= data_section_start, < file_size)
        valid_data_off = (entry_data_off >= data_section_start && entry_data_off < max_data_absolute);
      } else {
        // Installed: could also have 16-byte entries, but unlikely
        valid_data_off = (entry_data_off >= data_section_start && entry_data_off < max_data_absolute);
      }

      if (debug_this) {
        std::cout << "  try_index_at_16(" << scan_offset << ") entry " << entry_idx << ": data_off=" << entry_data_off
                  << " valid_data_off=" << valid_data_off << " valid_hash=" << valid_hash << std::endl;
      }

      if (valid_data_off && valid_hash) {
        local_entries = entry_idx + 1;
      } else {
        if (debug_this) {
          std::cout << "  try_index_at_16(" << scan_offset << ") BREAK at entry " << entry_idx << std::endl;
        }
        break;
      }
    }
    if (debug_this) {
      std::cout << "  try_index_at_16(" << scan_offset << ") returns " << local_entries << std::endl;
    }
    return local_entries;
  };

  // Scan for entries - use format-specific entry size
  // ISO format: 16-byte entries (data_offset, hash1, hash2, hash3)
  // Installed format: 20-byte entries (data_offset, size, hash1, hash2, flags)
  uint32_t scan_step = 4;

  if (is_iso_format) {
    // ISO format: ONLY try 16-byte entries
    // DEBUG: Show first few 16-byte chunks
    std::cout << "Shard " << shard_idx << ": DEBUG - First 256 bytes of index_data:" << std::endl;
    for (int row = 0; row < 16 && row * 16 < static_cast<int>(index_scan_size); ++row) {
      std::cout << "  " << std::hex << std::setw(4) << std::setfill('0') << (row * 16) << ": ";
      for (int col = 0; col < 16; ++col) {
        int idx = row * 16 + col;
        if (idx < static_cast<int>(index_scan_size)) {
          std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(file_data[idx]) << " ";
        } else {
          std::cout << "   ";
        }
      }
      std::cout << "  ";
      for (int col = 0; col < 16; ++col) {
        int idx = row * 16 + col;
        if (idx < static_cast<int>(index_scan_size)) {
          char c = static_cast<char>(file_data[idx]);
          std::cout << (c >= 32 && c < 127 ? c : '.');
        } else {
          std::cout << " ";
        }
      }
      std::cout << std::dec << std::endl;
    }
    // Also print 16-byte chunks with interpretation
    std::cout << "Shard " << shard_idx << ": 16-byte chunk interpretation:" << std::endl;
    for (int chunk = 0; chunk < 32 && chunk * 16 + 16 <= static_cast<int>(index_scan_size); ++chunk) {
      uint32_t data_off = *reinterpret_cast<const uint32_t*>(file_data.data() + chunk * 16);
      uint32_t h1 = *reinterpret_cast<const uint32_t*>(file_data.data() + chunk * 16 + 4);
      uint32_t h2 = *reinterpret_cast<const uint32_t*>(file_data.data() + chunk * 16 + 8);
      uint32_t h3 = *reinterpret_cast<const uint32_t*>(file_data.data() + chunk * 16 + 12);
      bool in_data = (data_off >= data_section_start && data_off < max_data_absolute);
      std::cout << "  chunk " << chunk << " (off " << chunk*16 << "): data_off=" << data_off
                << " in_data=" << in_data << " h1=" << std::hex << h1 << " h2=" << h2 << " h3=" << h3 << std::dec << std::endl;
    }
    std::cout << "Shard " << shard_idx << ": Scanning for 16-byte entries..." << std::endl;

    // For ISO format, the index entries may have a STRIDE (not packed consecutively).
    // Shard 0 shows valid entries every 80 bytes (every 5th 16-byte slot).
    // Strategy: collect ALL valid entries, then find the dominant stride pattern.

    // First, try the consecutive sequence approach (works for shards 1,3)
    uint32_t max_consecutive = 0;
    uint32_t max_consecutive_start = 0;

    for (uint32_t scan_offset = scan_start; scan_offset + 16 <= scan_end; scan_offset += scan_step) {
      uint32_t local_entries_16 = try_index_at_16(scan_offset);
      // DEBUG: Show first few valid attempts
      if (local_entries_16 > 0 && scan_offset < 100) {
        std::cout << "Shard " << shard_idx << ": try_index_at_16(" << scan_offset << ") = " << local_entries_16 << std::endl;
      }
      if (local_entries_16 > max_consecutive) {
        max_consecutive = local_entries_16;
        max_consecutive_start = scan_offset;
      }
    }
    std::cout << "Shard " << shard_idx << ": Longest consecutive sequence: " << max_consecutive << " entries at offset " << max_consecutive_start << std::endl;

    // Now check if this best sequence has a valid name table
    if (max_consecutive > 1) {
      size_t name_table_start = max_consecutive_start + max_consecutive * 16;
      if (name_table_start < index_scan_size) {
        uint32_t name_count = 0;
        size_t pos = name_table_start;
        while (pos + 1 < index_scan_size) {
          if (file_data[pos] == 0 && file_data[pos + 1] == 0) break;
          size_t start = pos;
          while (pos < index_scan_size && file_data[pos] != 0) pos++;
          if (pos > start) name_count++;
          pos++;
        }
        if (name_count > 0 && name_count >= max_consecutive * 0.5 && name_count <= max_consecutive * 1.5) {
          best_num_entries = max_consecutive;
          best_index_start = max_consecutive_start;
          best_entry_size = 16;
        }
      }
    }

    // If consecutive approach failed (best_num_entries == 0), try STRIDE-based approach
    // Collect all valid entries and find the most common stride
    if (best_num_entries == 0) {
      std::cout << "Shard " << shard_idx << ": Consecutive scan failed, trying stride-based scan..." << std::endl;

      // Collect all valid entry positions
      std::vector<uint32_t> valid_positions;
      for (uint32_t scan_offset = scan_start; scan_offset + 16 <= scan_end; scan_offset += 16) {
        uint32_t local_entries = try_index_at_16(scan_offset);
        if (local_entries > 0) {
          valid_positions.push_back(scan_offset);
        }
      }

      std::cout << "Shard " << shard_idx << ": Found " << valid_positions.size() << " valid entry positions" << std::endl;

      if (!valid_positions.empty()) {
        // Find the most common stride between consecutive valid positions
        std::unordered_map<uint32_t, int> stride_counts;
        for (size_t i = 1; i < valid_positions.size(); ++i) {
          uint32_t stride = valid_positions[i] - valid_positions[i-1];
          if (stride > 0 && stride <= 1024) {  // reasonable stride
            stride_counts[stride]++;
          }
        }

        best_stride = 16;
        int best_stride_count = 0;
        for (const auto& [stride, count] : stride_counts) {
          if (count > best_stride_count) {
            best_stride = stride;
            best_stride_count = count;
          }
        }

        std::cout << "Shard " << shard_idx << ": Best stride = " << best_stride << " (count=" << best_stride_count << ")" << std::endl;

        if (best_stride > 16 && best_stride_count >= 3) {
          // We found a regular stride pattern. Use the first valid position as start,
          // and collect entries at that stride.
          uint32_t start_offset = valid_positions[0];
          std::vector<uint32_t> stride_entries;

          for (uint32_t off = start_offset; off + 16 <= scan_end; off += best_stride) {
            // Check if this offset has a valid entry
            uint32_t local = try_index_at_16(off);
            if (local > 0) {
              stride_entries.push_back(off);
            } else {
              // Allow a few gaps
              if (stride_entries.size() > 10) break;
            }
          }

          if (stride_entries.size() > best_num_entries) {
            best_num_entries = static_cast<uint32_t>(stride_entries.size());
            best_index_start = stride_entries[0];
            best_entry_size = 16;
            std::cout << "Shard " << shard_idx << ": Stride-based scan found " << best_num_entries << " entries at stride " << best_stride << std::endl;
          }
        }

        // Fallback: if no clear stride, just use ALL valid entries (up to reasonable limit)
        if (best_num_entries == 0 && valid_positions.size() > 5) {
          // Limit to a reasonable number based on index size
          uint32_t max_reasonable = index_scan_size / 16;
          if (valid_positions.size() <= max_reasonable) {
            best_num_entries = static_cast<uint32_t>(valid_positions.size());
            best_index_start = valid_positions[0];
            best_entry_size = 16;
            std::cout << "Shard " << shard_idx << ": Fallback - using all " << best_num_entries << " valid entries" << std::endl;
          }
        }
      }
    }

    std::cout << "Shard " << shard_idx << ": Best 16-byte entries found: " << best_num_entries << " at offset " << best_index_start << std::endl;
  } else {
    // Installed format: ONLY try 20-byte entries
    // First try consecutive sequence approach
    for (uint32_t scan_offset = scan_start; scan_offset + 20 <= scan_end; scan_offset += scan_step) {
      uint32_t local_entries_20 = try_index_at_20(scan_offset);
      if (local_entries_20 > best_num_entries && local_entries_20 > 1) {
        size_t name_table_start = scan_offset + local_entries_20 * 20;
        if (name_table_start < index_scan_size) {
          uint32_t name_count = 0;
          size_t pos = name_table_start;
          while (pos + 1 < index_scan_size) {
            if (file_data[pos] == 0 && file_data[pos + 1] == 0) break;
            size_t start = pos;
            while (pos < index_scan_size && file_data[pos] != 0) pos++;
            if (pos > start) name_count++;
            pos++;
          }
          if (name_count > 0 && name_count >= local_entries_20 * 0.5 && name_count <= local_entries_20 * 1.5) {
            best_num_entries = local_entries_20;
            best_index_start = scan_offset;
            best_entry_size = 20;
          }
        }
      }
    }

    // If consecutive approach failed, try stride-based approach (like ISO format)
    if (best_num_entries == 0) {
      std::cout << "Shard " << shard_idx << ": Installed format - consecutive scan failed, trying stride-based scan..." << std::endl;

      // Collect all valid entry positions
      std::vector<uint32_t> valid_positions;
      for (uint32_t scan_offset = scan_start; scan_offset + 20 <= scan_end; scan_offset += 20) {
        uint32_t local_entries = try_index_at_20(scan_offset);
        if (local_entries > 0) {
          valid_positions.push_back(scan_offset);
        }
      }

      std::cout << "Shard " << shard_idx << ": Found " << valid_positions.size() << " valid entry positions" << std::endl;

      if (!valid_positions.empty()) {
        // Find the most common stride between consecutive valid positions
        std::unordered_map<uint32_t, int> stride_counts;
        for (size_t i = 1; i < valid_positions.size(); ++i) {
          uint32_t stride = valid_positions[i] - valid_positions[i-1];
          if (stride > 0 && stride <= 1024) {  // reasonable stride
            stride_counts[stride]++;
          }
        }

        best_stride = 20;
        int best_stride_count = 0;
        for (const auto& [stride, count] : stride_counts) {
          if (count > best_stride_count) {
            best_stride = stride;
            best_stride_count = count;
          }
        }

        std::cout << "Shard " << shard_idx << ": Best stride = " << best_stride << " (count=" << best_stride_count << ")" << std::endl;

        if (best_stride > 20 && best_stride_count >= 3) {
          // We found a regular stride pattern. Use the first valid position as start,
          // and collect entries at that stride.
          uint32_t start_offset = valid_positions[0];
          std::vector<uint32_t> stride_entries;

          for (uint32_t off = start_offset; off + 20 <= scan_end; off += best_stride) {
            // Check if this offset has a valid entry
            uint32_t local = try_index_at_20(off);
            if (local > 0) {
              stride_entries.push_back(off);
            } else {
              // Allow a few gaps
              if (stride_entries.size() > 10) break;
            }
          }

          if (stride_entries.size() > best_num_entries) {
            best_num_entries = static_cast<uint32_t>(stride_entries.size());
            best_index_start = stride_entries[0];
            best_entry_size = 20;
            std::cout << "Shard " << shard_idx << ": Stride-based scan found " << best_num_entries << " entries at stride " << best_stride << std::endl;
          }
        }

        // Fallback: if no clear stride, just use ALL valid entries (up to reasonable limit)
        if (best_num_entries == 0 && valid_positions.size() > 5) {
          // Limit to a reasonable number based on index size
          uint32_t max_reasonable = index_scan_size / 20;
          if (valid_positions.size() <= max_reasonable) {
            best_num_entries = static_cast<uint32_t>(valid_positions.size());
            best_index_start = valid_positions[0];
            best_entry_size = 20;
            std::cout << "Shard " << shard_idx << ": Fallback - using all " << best_num_entries << " valid entries" << std::endl;
          }
        }
      }
    }
  }

  // Fallback: if no valid index found with name table, try without name table validation
  // But cap entries by available space
  if (best_num_entries == 0) {
    uint32_t max_entries_by_space = (scan_end - scan_start) / (is_iso_format ? 16 : 20);
    for (uint32_t scan_offset = scan_start; scan_offset + (is_iso_format ? 16 : 20) <= scan_end; scan_offset += 4) {
      if (is_iso_format) {
        uint32_t local_entries_16 = try_index_at_16(scan_offset);
        if (local_entries_16 > best_num_entries && local_entries_16 > 5 && local_entries_16 <= max_entries_by_space) {
          best_num_entries = local_entries_16;
          best_index_start = scan_offset;
          best_entry_size = 16;
        }
      } else {
        uint32_t local_entries_20 = try_index_at_20(scan_offset);
        if (local_entries_20 > best_num_entries && local_entries_20 > 5 && local_entries_20 <= max_entries_by_space) {
          best_num_entries = local_entries_20;
          best_index_start = scan_offset;
          best_entry_size = 20;
        }
      }
    }
  }

  uint32_t num_entries = best_num_entries;
  uint32_t index_start_offset = best_index_start; // Offset from data_offset
  uint32_t entry_size = best_entry_size;
  std::cout << "Shard " << shard_idx << ": Best index found at offset " << index_start_offset << " (from data_offset), entries=" << num_entries << " entry_size=" << entry_size << std::endl;

  // Now read the index + name table section (from index_start_offset to end of file_data)
  // Actually we already have it in file_data - just use it directly
  std::vector<uint8_t> parsed_index_data(file_data.begin() + index_start_offset, file_data.end());

  // Parse all index entries
  struct RawIndexEntry {
    uint32_t data_offset;
    uint32_t size;
    uint64_t hash;
    uint32_t flags;
  };

  std::vector<RawIndexEntry> raw_entries;
  raw_entries.reserve(num_entries);

  // For ISO format with stride, we need to parse entries at stride intervals, not consecutively
  // The stride was detected during scanning (best_stride for ISO, typically 80)
  uint32_t parse_stride = entry_size;
  if (is_iso_format && best_stride > entry_size) {
    parse_stride = best_stride;
  }

  for (uint32_t i = 0; i < num_entries; ++i) {
    uint32_t entry_offset = i * parse_stride;
    if (entry_offset + entry_size > parsed_index_data.size()) break;
    RawIndexEntry e;
    if (entry_size == 20) {
      // Installed game format: data_off(4), size(4), hash1(4), hash2(4), flags(4)
      e.data_offset = *reinterpret_cast<const uint32_t*>(&parsed_index_data[entry_offset]);
      e.size = *reinterpret_cast<const uint32_t*>(&parsed_index_data[entry_offset + 4]);
      uint32_t h1 = *reinterpret_cast<const uint32_t*>(&parsed_index_data[entry_offset + 8]);
      uint32_t h2 = *reinterpret_cast<const uint32_t*>(&parsed_index_data[entry_offset + 12]);
      e.hash = (static_cast<uint64_t>(h1) << 32) | h2;
      e.flags = *reinterpret_cast<const uint32_t*>(&parsed_index_data[entry_offset + 16]);
    } else {
      // ISO format: 16-byte entries: data_off(4), hash1(4), hash2(4), hash3(4)
      e.data_offset = *reinterpret_cast<const uint32_t*>(&parsed_index_data[entry_offset]);
      e.size = 0;
      uint32_t h1 = *reinterpret_cast<const uint32_t*>(&parsed_index_data[entry_offset + 4]);
      uint32_t h2 = *reinterpret_cast<const uint32_t*>(&parsed_index_data[entry_offset + 8]);
      uint32_t h3 = *reinterpret_cast<const uint32_t*>(&parsed_index_data[entry_offset + 12]);
      e.hash = (static_cast<uint64_t>(h1) << 32) | h2;
      e.flags = 0;
    }
    raw_entries.push_back(e);
  }

  std::cout << "Shard " << shard_idx << ": Parsed " << raw_entries.size() << " index entries" << std::endl;
  // DEBUG: Show first 5 entries
  size_t debug_count = raw_entries.size() < 5 ? raw_entries.size() : 5;
  for (size_t i = 0; i < debug_count; ++i) {
    std::cout << "  Entry " << i << ": data_off=" << raw_entries[i].data_offset
              << " size=" << raw_entries[i].size
              << " hash=" << std::hex << raw_entries[i].hash << std::dec
              << " flags=" << raw_entries[i].flags << std::endl;
  }
  if (raw_entries.empty() && parsed_index_data.size() >= entry_size) {
    std::cout << "Shard " << shard_idx << ": First " << entry_size << " bytes at index: ";
    for (int k = 0; k < entry_size; ++k) std::cout << std::hex << (int)index_data[k] << " ";
    std::cout << std::dec << std::endl;
    if (entry_size == 20) {
      uint32_t d0 = *reinterpret_cast<const uint32_t*>(&index_data[0]);
      uint32_t s0 = *reinterpret_cast<const uint32_t*>(&index_data[4]);
      std::cout << "Shard " << shard_idx << ": First entry data_off=" << d0 << " size=" << s0 << std::endl;
    } else {
      uint32_t d0 = *reinterpret_cast<const uint32_t*>(&index_data[0]);
      std::cout << "Shard " << shard_idx << ": First entry data_off=" << d0 << std::endl;
    }
  }

  // Compute sizes from consecutive entries for 16-byte format
  // Use parse_stride for stride-based entries (which may not be consecutive in the parsed data)
  if (entry_size == 16) {
    for (size_t i = 0; i + 1 < raw_entries.size(); ++i) {
      raw_entries[i].size = raw_entries[i + 1].data_offset - raw_entries[i].data_offset;
    }
    if (!raw_entries.empty()) {
      raw_entries.back().size = file_size - raw_entries.back().data_offset;
    }
  }

  // Find the name table - it's at the END of the file (past the data section)
  std::vector<std::string> names;

  // The name table is at the VERY END of the file.
  // For shards 0 and 2 with huge data sections, we have end_file_data (last 32KB).
  // For shards 1 and 3, the name table should be after the index in file_data.

  // Helper to parse names from a data block starting at a given relative offset
  // Returns empty vector if the parsed names don't look like a valid name table
  auto parse_names_from = [&](const std::vector<uint8_t>& data, size_t start_rel, size_t max_len, const char* source) -> std::vector<std::string> {
    std::vector<std::string> local_names;
    size_t pos = start_rel;
    size_t end = std::min(start_rel + max_len, data.size());
    while (pos + 1 < end) {
      if (data[pos] == 0 && data[pos + 1] == 0) break;
      size_t start = pos;
      while (pos < end && data[pos] != 0) pos++;
      if (pos > start) {
        std::string name(reinterpret_cast<const char*>(&data[start]), pos - start);
        // Filter: names should be reasonable length and look like filenames
        if (name.size() >= 2 && name.size() <= 255) {
          local_names.push_back(name);
        }
      }
      if (pos < end) pos++;
    }
    if (!local_names.empty()) {
      std::cout << "Shard " << shard_idx << ": Found " << local_names.size() << " names in " << source << " (start_rel=" << start_rel << ")" << std::endl;
    }
    return local_names;
  };

  // Helper to validate if names look like a real name table
  auto validate_name_table = [&](const std::vector<std::string>& test_names) -> bool {
    if (test_names.empty()) return false;
    double ratio = static_cast<double>(test_names.size()) / num_entries;
    std::cout << "Shard " << shard_idx << ": validate_name_table: " << test_names.size() << " names, " << num_entries << " entries, ratio=" << ratio << std::endl;
    // Name count should be close to entry count (at least 15% since some entries may not have names)
    if (test_names.size() < num_entries * 0.15 || test_names.size() > num_entries * 2) {
      std::cout << "Shard " << shard_idx << ": FAIL - count ratio" << std::endl;
      return false;
    }
    // Names should look like filenames (contain alphanumeric, dots, underscores, no control chars)
    for (const auto& name : test_names) {
      for (char c : name) {
        if (c < 32 || c > 126) {
          std::cout << "Shard " << shard_idx << ": FAIL - invalid char " << (int)c << " in name: " << name << std::endl;
          return false;
        }
      }
    }
    std::cout << "Shard " << shard_idx << ": PASS" << std::endl;
    return true;
  };

  // Determine which region has the name table based on format
  // ISO format: name table is ALWAYS after the index entries (in the index region)
  // Installed game format: name table is at the END of the file (after data section)
  bool name_table_at_end = !is_iso_format;  // installed format = true, ISO format = false

  // Calculate the name table start offset in file_data (index_data) for front region
  // For ISO shards 1,3: name table is AFTER the index entries
  // best_index_start is the offset from index_data start (absolute index_region_start)
  // index_start_abs = index_region_start + best_index_start
  // name_table_start_abs = index_start_abs + num_entries * entry_size
  // name_table_start_rel = name_table_start_abs - index_region_start
  uint32_t name_table_front_start_rel = 0;
  if (!name_table_at_end) {
    uint32_t index_start_abs = index_region_start + best_index_start;
    uint32_t name_table_start_abs = index_start_abs + num_entries * entry_size;
    if (name_table_start_abs > index_region_start) {
      name_table_front_start_rel = name_table_start_abs - index_region_start;
    }
    std::cout << "Shard " << shard_idx << ": Index starts at abs " << index_start_abs
              << " (" << index_region_start << " + best_index_start=" << best_index_start << ")"
              << ", name table expected at abs " << name_table_start_abs
              << " (rel " << name_table_front_start_rel << ")" << std::endl;
  }

  // First priority: search in the CORRECT region
  if (name_table_at_end && !end_file_data.empty()) {
    // Shards 0,2: name table at END of file - search backwards from end
    for (size_t scan_start = end_file_data.size(); scan_start > 256; scan_start -= 4) {
      if (scan_start >= end_file_data.size()) continue;
      bool found_double_null = false;
      for (size_t p = scan_start; p + 1 < end_file_data.size() && p < scan_start + 8192; ++p) {
        if (end_file_data[p] == 0 && end_file_data[p + 1] == 0) {
          found_double_null = true;
          break;
        }
      }
      if (!found_double_null) continue;

      auto test_names = parse_names_from(end_file_data, scan_start, end_file_data.size() - scan_start, "end_file_data (backward scan)");
      if (validate_name_table(test_names)) {
        names = test_names;
        std::cout << "Shard " << shard_idx << ": Valid name table found at end_file_data offset " << scan_start << std::endl;
        break;
      }
    }
    // If backward scan failed, try from beginning of end_file_data
    if (names.empty()) {
      auto test_names = parse_names_from(end_file_data, 0, end_file_data.size(), "end_file_data (forward)");
      if (validate_name_table(test_names)) {
        names = test_names;
        std::cout << "Shard " << shard_idx << ": Valid name table found at end_file_data offset 0" << std::endl;
      }
    }
  } else if (!name_table_at_end) {
    // ISO format shards 1,3: name table AFTER index entries (at index_start + num_entries * entry_size)
    std::cout << "Shard " << shard_idx << ": Searching for name table after index at rel offset " << name_table_front_start_rel << std::endl;

    // DEBUG: Show bytes at the exact calculated position
    if (name_table_front_start_rel < file_data.size()) {
      size_t debug_end = std::min<size_t>(name_table_front_start_rel + 128, file_data.size());
      std::cout << "Shard " << shard_idx << ": DEBUG - bytes at exact name table position (" << name_table_front_start_rel << "):" << std::endl;
      for (size_t row = 0; row < (debug_end - name_table_front_start_rel + 15) / 16; ++row) {
        size_t offset = name_table_front_start_rel + row * 16;
        if (offset >= debug_end) break;
        std::cout << "  " << std::hex << std::setw(4) << std::setfill('0') << (offset - name_table_front_start_rel) << ": ";
        for (int col = 0; col < 16; ++col) {
          size_t idx = offset + col;
          if (idx < debug_end) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(file_data[idx]) << " ";
          }
        }
        std::cout << " | ";
        for (int col = 0; col < 16; ++col) {
          size_t idx = offset + col;
          if (idx < debug_end) {
            char c = static_cast<char>(file_data[idx]);
            std::cout << (c >= 32 && c <= 126 ? c : '.');
          }
        }
        std::cout << std::dec << std::endl;
      }
    }

    if (name_table_front_start_rel + 1 < file_data.size()) {
      size_t scan_end2 = std::min<size_t>(name_table_front_start_rel + 32768, file_data.size());
      for (size_t scan_start = name_table_front_start_rel; scan_start < scan_end2; scan_start += 4) {
        bool found_double_null = false;
        for (size_t p = scan_start; p + 1 < file_data.size() && p < scan_start + 8192; ++p) {
          if (file_data[p] == 0 && file_data[p + 1] == 0) {
            found_double_null = true;
            break;
          }
        }
        if (!found_double_null) continue;

        auto test_names = parse_names_from(file_data, scan_start, 8192, "file_data at index_region_end (front region)");
        if (validate_name_table(test_names)) {
          names = test_names;
          std::cout << "Shard " << shard_idx << ": Valid name table found at file_data offset " << scan_start << " (index_region_end)" << std::endl;
          break;
        }
      }
    }
  }

  // Fallback: try the OTHER region if first didn't work
  if (names.empty()) {
    if (name_table_at_end) {
      // Try front region as fallback (after index entries)
      if (name_table_front_start_rel + 1 < file_data.size()) {
        size_t scan_end2 = std::min<size_t>(name_table_front_start_rel + 32768, file_data.size());
        for (size_t scan_start = name_table_front_start_rel; scan_start < scan_end2; scan_start += 4) {
          bool found_double_null = false;
          for (size_t p = scan_start; p + 1 < file_data.size() && p < scan_start + 8192; ++p) {
            if (file_data[p] == 0 && file_data[p + 1] == 0) {
              found_double_null = true;
              break;
            }
          }
          if (!found_double_null) continue;

          auto test_names = parse_names_from(file_data, scan_start, 8192, "file_data at index_region_end (fallback front)");
          if (validate_name_table(test_names)) {
            names = test_names;
            std::cout << "Shard " << shard_idx << ": Valid name table found at file_data offset " << scan_start << " (fallback front at index_region_end)" << std::endl;
            break;
          }
        }
      }
    } else {
      // Try end region as fallback (read it if we haven't)
      if (end_file_data.empty() && file_size > 65536) {
        stream.seekg(file_size - 65536, std::ios::beg);
        uint32_t end_region_size = file_size - static_cast<uint32_t>(stream.tellg());
        end_file_data.resize(end_region_size);
        stream.read(reinterpret_cast<char*>(end_file_data.data()), end_region_size);
      }
      if (!end_file_data.empty()) {
        auto test_names = parse_names_from(end_file_data, 0, end_file_data.size(), "end_file_data (fallback)");
        if (validate_name_table(test_names)) {
          names = test_names;
        }
      }
    }
  }

  std::cout << "Shard " << shard_idx << ": Found " << names.size() << " names in name table" << std::endl;

  // Match entries to names (typically sequential)
  size_t match_count = std::min(raw_entries.size(), names.size());
  for (size_t i = 0; i < match_count; ++i) {
    const auto& e = raw_entries[i];
    const auto& name = names[i];

    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

    ResourceEntry entry;
    entry.offset = e.data_offset;
    entry.size = e.size;
    entry.shard = shard_idx;
    entry.name = name;

    entries_[lower_name] = entry;
  }

  // For ISO shards (no name table), create synthetic names by index
  // This allows finding resources by position even without a name table
  if (match_count == 0 && !raw_entries.empty()) {
    std::cout << "Shard " << shard_idx << ": No name table found - creating synthetic names by index" << std::endl;
    for (size_t i = 0; i < raw_entries.size(); ++i) {
      const auto& e = raw_entries[i];
      char buf[32];
      snprintf(buf, sizeof(buf), "entry_%04zu", i);
      std::string synthetic_name = buf;

      ResourceEntry entry;
      entry.offset = e.data_offset;
      entry.size = e.size;
      entry.shard = shard_idx;
      entry.name = synthetic_name;

      entries_[synthetic_name] = entry;
    }
    std::cout << "Shard " << shard_idx << ": Created " << raw_entries.size() << " synthetic entries" << std::endl;
  } else if (match_count == 0) {
    std::cout << "Shard " << shard_idx << ": No valid names matched to entries" << std::endl;
  }
}

std::optional<ResourceEntry> ResourceIndex::find(std::string_view name) const {
  std::string lower_name(name);
  std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

  auto it = entries_.find(lower_name);
  if (it != entries_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::vector<uint8_t> ResourceIndex::read_resource(const ResourceEntry& entry) const {
  if (entry.shard >= shard_paths_.size()) {
    throw std::runtime_error("Invalid shard index");
  }

  std::ifstream file(shard_paths_[entry.shard], std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open shard " + std::to_string(entry.shard));
  }

  std::vector<uint8_t> buffer(entry.size);
  file.seekg(entry.offset, std::ios::beg);
  file.read(reinterpret_cast<char*>(buffer.data()), entry.size);
  return buffer;
}

} // namespace c3