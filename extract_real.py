import struct

data = open('D:/Games/C3/RESOURCE.RES','rb').read()
data_section_start = struct.unpack_from('<I', data, 52)[0]  # 35674

# Build index map: absolute data_offset -> size
idx = 76
index_map = {}
while idx + 20 <= data_section_start:
    eoff = struct.unpack_from('<I', data, idx)[0]
    esz = struct.unpack_from('<I', data, idx+4)[0]
    # Try absolute and relative
    index_map[eoff] = esz
    index_map[eoff + data_section_start] = esz
    idx += 20

# Find LZP1 occurrence; entry_start = o - 0x14
o = 955772
entry_start = o - 0x14
target_size = index_map.get(entry_start)
print(f"entry_start={entry_start} (0x{entry_start:x}) size={target_size}")

# Fallback: search index_map keys near entry_start
if target_size is None:
    for k in index_map:
        if abs(k - entry_start) < 100:
            print(f"  nearby index key {k} size={index_map[k]}")

# Just extract a window from entry_start
res = data[entry_start:entry_start+0x5000]
cs = struct.unpack_from('<I', res, 0x10)[0]
ds = struct.unpack_from('<I', res, 0x18)[0]
fl = struct.unpack_from('<I', res, 0x1c)[0]
print(f"0x10={cs} 0x18={ds} 0x1c={fl}")
print(f"0x20+cs={0x20+cs} 0x20+ds={0x20+ds}")
print(f"window len={len(res)}")
print("data at 0x20 first 96:", res[0x20:0x80].hex(' '))
print("data at 0x20 ascii:", ''.join(chr(b) if 32<=b<127 else '.' for b in res[0x20:0x80]))
