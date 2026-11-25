import struct

data = open('D:/Games/C3/RESOURCE.RES','rb').read()
data_section_start = struct.unpack_from('<I', data, 52)[0]  # 35674
print(f"data_section_start={data_section_start} (0x{data_section_start:x})")

# Build index: parse entries at [76, data_section_start)
idx = 76
entries = []
while idx + 20 <= data_section_start:
    eoff = struct.unpack_from('<I', data, idx)[0]
    esz = struct.unpack_from('<I', data, idx+4)[0]
    entries.append((eoff, esz))
    idx += 20

print(f"parsed {len(entries)} index entries")

# For each LZP1 magic occurrence, find matching index entry
lzp1_occ = []
off = 0
while True:
    i = data.find(b'LZP1', off)
    if i < 0: break
    lzp1_occ.append(i)
    off = i + 1

print(f"LZP1 occurrences: {len(lzp1_occ)}")

# Try both absolute and relative (eoff + data_section_start)
matches = 0
for o in lzp1_occ[:5]:
    entry_start = o - 0x14
    # search index
    found = None
    for eoff, esz in entries:
        if eoff == entry_start:
            found = (eoff, esz, 'absolute')
            break
        if eoff + data_section_start == entry_start:
            found = (eoff, esz, 'relative')
            break
    if found:
        matches += 1
        eoff, esz, mode = found
        res = data[entry_start:entry_start+esz]
        cs = struct.unpack_from('<I', res, 0x10)[0]
        ds = struct.unpack_from('<I', res, 0x18)[0]
        fl = struct.unpack_from('<I', res, 0x1c)[0]
        print(f"\nocc at 0x{o:x} entry_start=0x{entry_start:x} mode={mode} index_off=0x{eoff:x} size={esz}")
        print(f"  0x10={cs} 0x18={ds} 0x1c={fl}  filelen={len(res)}")
        print(f"  0x20..0x30: {res[0x20:0x30].hex(' ')}")
    else:
        print(f"\nocc at 0x{o:x} entry_start=0x{entry_start:x} NO INDEX MATCH")

print(f"\nindex matches: {matches}/{min(5,len(lzp1_occ))}")
