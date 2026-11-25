import struct

data = open('entry_1685','rb').read()
print("file size:", len(data))

MAGIC_STARTS = {0x4d:'M(MIDI)', 0x52:'R(WAV/RIFF)', 0x0a:'(PCX)', 0x33:'3(3DO1)', 0x4c:'L(LZP1)', 0x46:'F', 0x49:'I'}

def lzw_decode_from(comp, start_byte, bit_align, bit_order, bit_width_start=9, max_width=13,
                    clear_code=256, end_code=257, first_free=258, max_dict=8192,
                    require_clear_first=False, max_output=200000):
    """comp: bytes. start decoding at byte index start_byte, bit position bit_align within that byte."""
    nbytes = len(comp) - start_byte
    if nbytes <= 0:
        return None
    sub = comp[start_byte:]
    nbits = nbytes * 8
    pos = bit_align  # global bit pos within sub
    bit_width = bit_width_start
    max_code = (1 << bit_width) - 1
    dict_size = first_free
    dict_list = [None] * max_dict
    for i in range(256):
        dict_list[i] = bytes([i])
    prev_code = None
    out = bytearray()
    saw_clear = False

    def read_bits(n):
        nonlocal pos
        if pos + n > nbits:
            return None
        val = 0
        if bit_order == 'msb':
            for b in range(n):
                bi = (pos + b) // 8
                bit = 7 - ((pos + b) % 8)
                if bi >= len(sub):
                    return None
                val = (val << 1) | ((sub[bi] >> bit) & 1)
        else:
            for b in range(n):
                bi = (pos + b) // 8
                bit = (pos + b) % 8
                if bi >= len(sub):
                    return None
                val |= ((sub[bi] >> bit) & 1) << b
        pos += n
        return val

    while pos < nbits:
        code = read_bits(bit_width)
        if code is None:
            break
        if code == end_code:
            return bytes(out), True  # clean end
        if code == clear_code:
            saw_clear = True
            dict_size = first_free
            bit_width = bit_width_start
            max_code = (1 << bit_width) - 1
            prev_code = None
            continue
        if prev_code is None:
            # first literal code
            if code < 256:
                out.append(code)
                prev_code = code
            else:
                return None  # invalid first code
            continue
        if code < dict_size:
            seq = dict_list[code]
            out.extend(seq)
            if dict_size < max_dict:
                dict_list[dict_size] = dict_list[prev_code] + seq[0:1]
                dict_size += 1
                if dict_size > max_code and bit_width < max_width:
                    bit_width += 1
                    max_code = (1 << bit_width) - 1
            prev_code = code
        elif code == dict_size:
            seq = dict_list[prev_code] + dict_list[prev_code][0:1]
            out.extend(seq)
            if dict_size < max_dict:
                dict_list[dict_size] = seq
                dict_size += 1
                if dict_size > max_code and bit_width < max_width:
                    bit_width += 1
                    max_code = (1 << bit_width) - 1
            prev_code = code
        else:
            return None  # invalid code
        if len(out) > max_output:
            return None
    # ran out of bits without END code
    if saw_clear or prev_code is not None:
        return bytes(out), False  # decoded but no clean END
    return None

comp = data[0x20:]

print("=== Brute force: start offset, bit align, bit order, no clear required ===")
print("Looking for output starting with a known magic byte or clean END code\n")
results = []
for start_off in range(0, 64):
    for align in range(8):
        for order in ['msb','lsb']:
            res = lzw_decode_from(comp, start_off, align, order,
                                  require_clear_first=False, max_output=200000)
            if res is None:
                continue
            out, clean_end = res
            first = out[0:1] if out else b''
            tag = MAGIC_STARTS.get(first[0] if first else 255, '')
            if tag or clean_end or len(out) > 100:
                print(f"  start={start_off} align={align} order={order} -> {len(out)} bytes "
                      f"clean_end={clean_end} first={first.hex()} {tag} head={out[:12].hex(' ')}")
                results.append((start_off, align, order, len(out), clean_end, first, out))

print(f"\nTotal candidate decodes: {len(results)}")

# Also try: single-byte XOR then LZW, check first byte magic
print("\n=== Single-byte XOR + LZW (no clear required), check first byte ===")
found = 0
for x in range(256):
    xored = bytes(b ^ x for b in comp)
    for align in range(8):
        for order in ['msb','lsb']:
            res = lzw_decode_from(xored, 0, align, order, require_clear_first=False, max_output=200000)
            if res is None:
                continue
            out, clean_end = res
            if out and out[0] in MAGIC_STARTS:
                print(f"  XOR=0x{x:02x} align={align} order={order} -> {len(out)} bytes clean_end={clean_end} first={MAGIC_STARTS[out[0]]} head={out[:16].hex(' ')}")
                found += 1
print(f"  total magic-start matches: {found}")
