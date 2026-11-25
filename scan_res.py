import struct
from collections import Counter

data = open('D:/Games/C3/RESOURCE.RES','rb').read()
data_section_start = struct.unpack_from('<I', data, 52)[0]  # 35674

idx = 76
count = 0
magics = Counter()
lzp1 = []
samples = {}
while idx + 20 <= data_section_start:
    entry_off = struct.unpack_from('<I', data, idx)[0]
    entry_size = struct.unpack_from('<I', data, idx+4)[0]
    count += 1
    valid = (entry_off >= data_section_start and entry_off + entry_size <= len(data)
             and entry_size > 0 and entry_size < 10*1024*1024)
    if valid:
        res = data[entry_off:entry_off+entry_size]
        magic = res[0x14:0x18] if len(res) >= 0x18 else b'??'
        magics[magic] += 1
        if magic == b'LZP1':
            lzp1.append((idx, entry_off, entry_size))
        if magic not in samples and len(res) >= 0x20:
            samples[magic] = (entry_off, entry_size, res[:0x20])
    idx += 20

print("Total entries:", count)
print("Magics:")
for m, c in magics.most_common():
    print(f"  {m!r}: {c}")
print("\nLZP1 entries:", lzp1)
print("\nSample headers:")
for m, (off, sz, hdr) in samples.items():
    print(f"  {m!r}: off={off} size={sz}")
    print(f"     {hdr.hex(' ')}")
