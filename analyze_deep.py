import struct

with open('entry_1685', 'rb') as f:
    data = f.read()

print(f"File size: {len(data)}")
print(f"\nFull header (0x00-0x20):")
for i in range(0, 0x20, 16):
    hex_part = ' '.join(f'{b:02x}' for b in data[i:i+16])
    ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i+16])
    print(f'  {i:04x}: {hex_part:<48s}  {ascii_part}')

cs = struct.unpack('<I', data[0x10:0x14])[0]
ds = struct.unpack('<I', data[0x18:0x1c])[0]
fl = struct.unpack('<I', data[0x1c:0x20])[0]
cdata = data[0x20:0x20+cs]
print(f"\nHeader fields:")
print(f"  0x10 compressed_size (LE): {cs}")
print(f"  0x10 compressed_size (BE): {struct.unpack('>I', data[0x10:0x14])[0]}")
print(f"  0x14 magic: {data[0x14:0x18]}")
print(f"  0x18 decompressed_size (LE): {ds}")
print(f"  0x18 decompressed_size (BE): {struct.unpack('>I', data[0x18:0x1c])[0]}")
print(f"  0x1c flags (LE): {fl}")
print(f"  0x1c flags (BE): {struct.unpack('>I', data[0x1c:0x20])[0]}")

print(f"\nCompressed data first 64 bytes (hex):")
for i in range(0, min(64, len(cdata)), 16):
    hex_part = ' '.join(f'{b:02x}' for b in cdata[i:i+16])
    ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in cdata[i:i+16])
    print(f'  {i:04x}: {hex_part:<48s}  {ascii_part}')

# Check if compressed data looks like text vs binary
printable = sum(1 for b in cdata[:256] if 32 <= b < 127)
print(f"\nPrintable ratio in first 256 bytes: {printable}/256")

# Maybe ds is not decompressed_size - maybe it's something else
# Check if decompressed data might be text
print(f"\nFlags value: {fl} = 0x{fl:08x} = {bin(fl)}")
