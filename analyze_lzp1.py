import struct, os

os.chdir(os.path.dirname(os.path.abspath(__file__)))

with open('entry_1685', 'rb') as f:
    data = f.read()

cs = struct.unpack('<I', data[0x10:0x14])[0]
ds = struct.unpack('<I', data[0x18:0x1c])[0]
fl = struct.unpack('<I', data[0x1c:0x20])[0]
cdata = data[0x20:0x20+cs]
print(f"Entry size: {len(data)}, compressed_size: {cs}, decompressed_size: {ds}, flags: {fl}")
print(f"Compressed data bytes: {len(cdata)}")

def read_bits_msb(data, pos, n):
    val = 0
    for b in range(n):
        byte = (pos + b) // 8
        bit = 7 - ((pos + b) % 8)
        if byte >= len(data):
            break
        val |= ((data[byte] >> bit) & 1) << (n - 1 - b)
    return val

def read_bits_lsb(data, pos, n):
    val = 0
    for b in range(n):
        byte = (pos + b) // 8
        bit = (pos + b) % 8
        if byte >= len(data):
            break
        val |= ((data[byte] >> bit) & 1) << b
    return val

def lzw_decompress(cdata, msb, start_bit, start_width, max_out=100000):
    rfn = read_bits_msb if msb else read_bits_lsb
    output = bytearray()
    prefix = [0] * 65536
    ch = [0] * 65536
    for i in range(256):
        prefix[i] = 0xFFFF
        ch[i] = i

    CLEAR = 256; END = 257; FIRST_FREE = 258

    pos = start_bit
    bit_width = start_width
    max_code = (1 << bit_width) - 1
    prev_code = 0xFFFF
    dict_size = FIRST_FREE

    while pos < len(cdata) * 8 and len(output) < max_out:
        code = rfn(cdata, pos, bit_width)
        pos += bit_width

        if code == END:
            return bytes(output), True, pos
        if code == CLEAR:
            dict_size = FIRST_FREE
            bit_width = 9
            max_code = 511
            prev_code = 0xFFFF
            continue
        if prev_code == 0xFFFF:
            if code < 256:
                output.append(code)
                prev_code = code
            else:
                return bytes(output), False, pos
            continue
        if code > dict_size:
            return bytes(output), False, pos
        if code == dict_size:
            cur = prev_code
            stack = []
            while cur >= 256:
                stack.append(ch[cur])
                cur = prefix[cur]
            stack.append(cur)
            first = cur
            for c in reversed(stack):
                output.append(c)
            output.append(first)
            if dict_size < 65536:
                prefix[dict_size] = prev_code
                ch[dict_size] = first
                dict_size += 1
        else:
            cur = code
            stack = []
            while cur >= 256:
                stack.append(ch[cur])
                cur = prefix[cur]
            stack.append(cur)
            for c in reversed(stack):
                output.append(c)
            if dict_size < 65536:
                prefix[dict_size] = prev_code
                ch[dict_size] = ch[cur]
                dict_size += 1
        if dict_size > max_code and bit_width < 16:
            bit_width += 1
            max_code = (1 << bit_width) - 1
        prev_code = code

    return bytes(output), False, pos

for msb in [True, False]:
    for start_width in range(9, 14):
        result, ok, endpos = lzw_decompress(cdata, msb, 0, start_width, max_out=10000)
        if ok and len(result) > 0:
            print(f"  msb={msb} width={start_width} start=0: OK output={len(result)} endpos={endpos}")
            print(f"    First 32 bytes: {result[:32].hex()}")
        elif len(result) > 100:
            print(f"  msb={msb} width={start_width} start=0: partial output={len(result)} (not OK)")
            print(f"    First 32 bytes: {result[:32].hex()}")
