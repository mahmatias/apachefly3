import struct

for name in ['entry_1685', 'entry_0373']:
    with open(name, 'rb') as f:
        data = f.read()
    cs = struct.unpack('<I', data[0x10:0x14])[0]
    ds = struct.unpack('<I', data[0x18:0x1c])[0]
    fl = struct.unpack('<I', data[0x1c:0x20])[0]
    cdata = data[0x20:0x20+cs]
    print(f"{name}: size={len(data)} cs={cs} ds={ds} flags={fl}")
    print(f"  First 32 bytes of compressed data: {cdata[:32].hex()}")
    print(f"  Byte at 0x20+cs={0x20+cs}: {data[0x20+cs:0x20+cs+16].hex()}")

# Are they identical?
with open('entry_1685', 'rb') as f:
    d1 = f.read()
with open('entry_0373', 'rb') as f:
    d2 = f.read()
print(f"\nIdentical: {d1 == d2}")
print(f"entry_1685 size: {len(d1)}, entry_0373 size: {len(d2)}")
