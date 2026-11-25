import struct

with open('entry_1685', 'rb') as f:
    data = f.read()

cs = struct.unpack('<I', data[0x10:0x14])[0]
cdata = data[0x20:0x20+cs]

def reverse_bits(b):
    r = 0
    for i in range(8):
        r = (r << 1) | ((b >> i) & 1)
    return r

def read_bits_msb(d, pos, n):
    val = 0
    for b in range(n):
        byte = (pos + b) // 8
        bit = 7 - ((pos + b) % 8)
        if byte >= len(d):
            break
        val |= ((d[byte] >> bit) & 1) << (n - 1 - b)
    return val

def try_lzw(cdata, msb, start_width, start_bit, max_out=200000, require_clear=False):
    rfn = read_bits_msb if msb else None
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

# Try XOR with every key, both with and without bit reversal
for use_reverse in [False, True]:
    for xor_key in range(256):
        xdata = bytes(b ^ xor_key for b in cdata)
        if use_reverse:
            xdata = bytes(reverse_bits(x) for x in xdata)
        for msb in [True, False]:
            for sw in range(9, 14):
                result, ok, endpos = try_lzw(xdata, msb, sw, 0, max_out=50000, require_clear=False)
                if ok and len(result) > 1000:
                    rev_str = "_rev" if use_reverse else ""
                    print(f"XOR=0x{xor_key:02x}{rev_str} msb={msb} width={sw}: OK output={len(result)} endpos={endpos}")
                    print(f"  First 64 bytes: {result[:64].hex()}")
                    print(f"  ASCII: {result[:64]}")
                    break

print("Done")
