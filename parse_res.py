import struct

data = open('D:/Games/C3/RESOURCE.RES','rb').read()
print("size:", len(data))

# Installed format via layout 2:
# offset 48 (0x30): index_offset_2 = index_region_end
# offset 52 (0x34): data_offset_2 = data_section_start
# index region: [76, data_section_start)
# data section starts at data_section_start
index_offset_2 = struct.unpack_from('<I', data, 48)[0]
data_section_start = struct.unpack_from('<I', data, 52)[0]
print(f"index_offset_2(48)={index_offset_2} data_offset_2(52)={data_section_start}")

# Index entries: 20 bytes each starting at 76: data_offset(4), size(4), hash(8), flags(4)
idx = 76
count = 0
lzp1_entries = []
while idx + 20 <= data_section_start:
    entry_off = struct.unpack_from('<I', data, idx)[0]
    entry_size = struct.unpack_from('<I', data, idx+4)[0]
    entry_hash = data[idx+8:idx+16]
    entry_flags = struct.unpack_from('<I', data, idx+16)[0]
    count += 1
    # Validate: entry_off should be in data section
    valid = (entry_off >= data_section_start and entry_off + entry_size <= len(data)
             and entry_size > 0 and entry_size < 10*1024*1024)
    if valid:
        res = data[entry_off:entry_off+entry_size]
        magic = res[0x14:0x18] if len(res) >= 0x18 else b''
        cs = struct.unpack_from('<I', res, 0x10)[0] if len(res) >= 0x14 else 0
        ds = struct.unpack_from('<I', res, 0x18)[0] if len(res) >= 0x1c else 0
        fl = struct.unpack_from('<I', res, 0x1c)[0] if len(res) >= 0x20 else 0
        if magic == b'LZP1':
            lzp1_entries.append((idx, entry_off, entry_size, cs, ds, fl, entry_flags))
            print(f"LZP1 idx={idx} off={entry_off} size={entry_size} cs={cs} ds={ds} fl={fl} flags={entry_flags:#x}")
    idx += 20

print(f"\nTotal index entries: {count}")
print(f"LZP1 entries found: {len(lzp1_entries)}")
