import struct

with open('entry_1685', 'rb') as f:
    data = f.read()

# Full hex dump of first 0x80 bytes
print("First 128 bytes:")
for i in range(0, 0x80, 16):
    hex_part = ' '.join(f'{b:02x}' for b in data[i:i+16])
    ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i+16])
    print(f'  {i:04x}: {hex_part:<48s}  {ascii_part}')

# Search for "Shadow Tools" string
idx = data.find(b'Shadow Tools')
if idx >= 0:
    print(f"\n'Shadow Tools' found at offset {idx} (0x{idx:x})")
else:
    print("\n'Shadow Tools' NOT found")

# Search for any readable strings in the header
print("\nSearching for strings in header (0x00-0x20):")
s = ''
start = 0
for i in range(0x20):
    b = data[i]
    if 32 <= b < 127:
        if not s:
            start = i
        s += chr(b)
    else:
        if len(s) >= 3:
            print(f"  offset {start} (0x{start:02x}): '{s}'")
        s = ''

# Also try interpreting header differently
cs_le = struct.unpack('<I', data[0x10:0x14])[0]
cs_be = struct.unpack('>I', data[0x10:0x14])[0]
ds_le = struct.unpack('<I', data[0x18:0x1c])[0]
ds_be = struct.unpack('>I', data[0x18:0x1c])[0]
fl_le = struct.unpack('<I', data[0x1c:0x20])[0]
fl_be = struct.unpack('>I', data[0x1c:0x20])[0]

print(f"\nHeader interpretations:")
print(f"  0x10 LE={cs_le} BE={cs_be}")
print(f"  0x14 magic={data[0x14:0x18]}")
print(f"  0x18 LE={ds_le} BE={ds_be}")
print(f"  0x1c LE={fl_le} BE={fl_be}")

# Maybe flags has subfields
print(f"\nFlags (LE=0x{fl_le:08x}):")
print(f"  Bit 0-7: {fl_le & 0xFF}")
print(f"  Bit 8-15: {(fl_le >> 8) & 0xFF}")
print(f"  Bit 16-23: {(fl_le >> 16) & 0xFF}")
print(f"  Bit 24-31: {(fl_le >> 24) & 0xFF}")
