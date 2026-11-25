import struct

with open("entry_0000", "rb") as f:
    data = f.read()

print(f"File size: {len(data)}")
print(f"Magic at 0x14: {data[0x14:0x18]}")

# Print hex dump of first 256 bytes
for i in range(0, min(256, len(data)), 16):
    hex_part = " ".join(f"{b:02x}" for b in data[i:i+16])
    ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in data[i:i+16])
    print(f"{i:04x}: {hex_part:<48s} {ascii_part}")

print("\n--- Parsing header fields ---")
print(f"Bytes 0x00-0x0F: {data[0:16].hex()}")
print(f"Bytes 0x10-0x13 (uint32 LE): {struct.unpack_from('<I', data, 0x10)[0]}")
print(f"Bytes 0x14-0x17: {data[0x14:0x18]} (magic)")
print(f"Bytes 0x18-0x19: {data[0x18:0x1a].hex()}")
print(f"Bytes 0x1a-0x1b: {data[0x1a:0x1c].hex()}")
print(f"Bytes 0x1c-0x23 (name): {data[0x1c:0x24]}")
print(f"Bytes 0x24-0x27 (uint32 LE): {struct.unpack_from('<I', data, 0x24)[0]}")
print(f"Bytes 0x28-0x2b (int32 LE): {struct.unpack_from('<i', data, 0x28)[0]}")
print(f"Bytes 0x2c-0x2f (uint32 LE): {struct.unpack_from('<I', data, 0x2c)[0]}")
print(f"Bytes 0x30-0x33 (int32 LE): {struct.unpack_from('<i', data, 0x30)[0]}")
print(f"Bytes 0x34-0x37 (uint32 LE): {struct.unpack_from('<I', data, 0x34)[0]}")
print(f"Bytes 0x38-0x3b (int32 LE): {struct.unpack_from('<i', data, 0x38)[0]}")
print(f"Bytes 0x3c-0x3f (uint32 LE): {struct.unpack_from('<I', data, 0x3c)[0]}")
print(f"Bytes 0x40-0x43 (uint32 LE): {struct.unpack_from('<I', data, 0x40)[0]}  <- num textures?")
print(f"Bytes 0x44-0x47 (uint32 LE): {struct.unpack_from('<I', data, 0x44)[0]}")
print(f"Bytes 0x48-0x4b (uint32 LE): {struct.unpack_from('<I', data, 0x48)[0]}")
print(f"Bytes 0x4c-0x4f (uint32 LE): {struct.unpack_from('<I', data, 0x4c)[0]}")
