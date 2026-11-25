import struct, os

os.chdir(os.path.dirname(os.path.abspath(__file__)))

# Scan all entries for LZP1 magic
entries = [f for f in os.listdir('.') if f.startswith('entry_') and os.path.isfile(f)]
lzp1_entries = []
for name in sorted(entries):
    with open(name, 'rb') as f:
        data = f.read()
    if len(data) < 0x20:
        continue
    magic = data[0x14:0x18]
    if magic == b'LZP1':
        cs = struct.unpack('<I', data[0x10:0x14])[0]
        ds = struct.unpack('<I', data[0x18:0x1c])[0]
        fl = struct.unpack('<I', data[0x1c:0x20])[0]
        lzp1_entries.append((name, len(data), cs, ds, fl))
        print(f"{name}: size={len(data)} cs={cs} ds={ds} flags={fl}")

print(f"\nTotal LZP1 entries: {len(lzp1_entries)}")
