import struct

with open('entry_1685', 'rb') as f:
    data = f.read()

cs = struct.unpack('<I', data[0x10:0x14])[0]
cdata = data[0x20:0x20+cs]

def read_bits_msb(d, pos, n):
    val = 0
    for b in range(n):
        byte = (pos + b) // 8
        bit = 7 - ((pos + b) % 8)
        if byte >= len(d):
            break
        val |= ((d[byte] >> bit) & 1) << (n - 1 - b)
    return val

def read_bits_lsb(d, pos, n):
    val = 0
    for b in range(n):
        byte = (pos + b) // 8
        bit = (pos + b) % 8
        if byte >= len(d):
            break
        val |= ((d[byte] >> bit) & 1) << b
    return val

def try_lzw(cdata, msb, start_width, start_bit, max_out=200000, require_clear=False):
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
    seen_clear = False
    while pos < len(cdata) * 8 and len(output) < max_out:
        code = rfn(cdata, pos, bit_width)
        pos += bit_width
        if code == END:
            return bytes(output), True, pos
        if code == CLEAR:
            seen_clear = True
            dict_size = FIRST_FREE
            bit_width = 9
            max_code = 511
            prev_code = 0xFFFF
            continue
        if require_clear and not seen_clear:
            return bytes(output), False, pos
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
                stack.append(ch[cur]); cur = prefix[cur]
            stack.append(cur)
            first = cur
            for c in reversed(stack): output.append(c)
            output.append(first)
            if dict_size < 65536:
                prefix[dict_size] = prev_code; ch[dict_size] = first; dict_size += 1
        else:
            cur = code
            stack = []
            while cur >= 256:
                stack.append(ch[cur]); cur = prefix[cur]
            stack.append(cur)
            for c in reversed(stack): output.append(c)
            if dict_size < 65536:
                prefix[dict_size] = prev_code; ch[dict_size] = ch[cur]; dict_size += 1
        if dict_size > max_code and bit_width < 16:
            bit_width += 1; max_code = (1 << bit_width) - 1
        prev_code = code
    return bytes(output), False, pos

best = []
for xor_key in range(256):
    xdata = bytes(b ^ xor_key for b in cdata)
    for msb in [True, False]:
        for sw in range(9, 14):
            for sb in [0]:
                result, ok, endpos = try_lzw(xdata, msb, sw, sb, max_out=50000, require_clear=False)
                if ok and len(result) > 500:
                    best.append((xor_key, msb, sw, sb, len(result), endpos, result))
                    print(f"XOR=0x{xor_key:02x} msb={msb} width={sw} start={sb}: OK output={len(result)} endpos={endpos}")
                    print(f"  First 48 bytes: {result[:48].hex()}")

if not best:
    print("No full matches found. Checking partial decodes > 100 bytes:")
    for xor_key in range(256):
        xdata = bytes(b ^ xor_key for b in cdata)
        for msb in [True, False]:
            for sw in range(9, 14):
                result, ok, endpos = try_lzw(xdata, msb, sw, 0, max_out=50000, require_clear=False)
                if len(result) > 100:
                    print(f"XOR=0x{xor_key:02x} msb={msb} width={sw}: partial output={len(result)} ok={ok}")
                    print(f"  First 48 bytes: {result[:48].hex()}")
