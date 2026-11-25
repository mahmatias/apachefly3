import struct

data = open('entry_1685','rb').read()
comp = data[0x20:]  # all bytes after header
print("comp region size:", len(comp))

# LZW decoder (general)
def lzw_decode(comp, start_off=0, align=0, order='msb', bw_start=9, max_w=13,
               clear_code=256, end_code=257, first_free=258, max_dict=8192,
               require_clear_first=False, max_output=200000):
    sub = comp[start_off:]
    nbits = len(sub) * 8
    pos = align
    bw = bw_start
    max_code = (1 << bw) - 1
    dsize = first_free
    d = [None]*max_dict
    for i in range(256): d[i] = bytes([i])
    prev = None
    out = bytearray()
    saw_clear = False

    def read_bits(n):
        nonlocal pos
        if pos + n > nbits: return None
        v = 0
        if order == 'msb':
            for b in range(n):
                bi = (pos+b)//8; bit = 7-((pos+b)%8)
                if bi >= len(sub): return None
                v = (v<<1) | ((sub[bi]>>bit)&1)
        else:
            for b in range(n):
                bi = (pos+b)//8; bit = (pos+b)%8
                if bi >= len(sub): return None
                v |= ((sub[bi]>>bit)&1) << b
        pos += n
        return v

    while pos < nbits:
        code = read_bits(bw)
        if code is None: break
        if code == end_code:
            return bytes(out), True, saw_clear
        if code == clear_code:
            saw_clear = True
            dsize = first_free; bw = bw_start; max_code = (1<<bw)-1; prev = None
            continue
        if prev is None:
            if code < 256:
                out.append(code); prev = code
            else:
                return None
            continue
        if code < dsize:
            seq = d[code]
            out.extend(seq)
            if dsize < max_dict:
                d[dsize] = d[prev] + seq[0:1]; dsize += 1
                if dsize > max_code and bw < max_w:
                    bw += 1; max_code = (1<<bw)-1
            prev = code
        elif code == dsize:
            seq = d[prev] + d[prev][0:1]
            out.extend(seq)
            if dsize < max_dict:
                d[dsize] = seq; dsize += 1
                if dsize > max_code and bw < max_w:
                    bw += 1; max_code = (1<<bw)-1
            prev = code
        else:
            return None
        if len(out) > max_output: return None
    return bytes(out), False, saw_clear

def xor1(buf, x): return bytes(b ^ x for b in buf)
def xor_repeat(buf, key): return bytes(b ^ key[i%len(key)] for i,b in enumerate(buf))

key16 = data[0:16]
TARGET_SIZES = {128, 7438}

print("=== Raw comp region LZW, check output size ===")
hits = 0
for bw in range(8,15):
    for order in ['msb','lsb']:
        for cf in [False, True]:
            for off in range(0,4):
                for al in range(8):
                    r = lzw_decode(comp, off, al, order, bw, require_clear_first=cf)
                    if r is None: continue
                    out, clean, sc = r
                    if len(out) in TARGET_SIZES or clean:
                        print(f"  bw={bw} order={order} cf={cf} off={off} al={al} -> {len(out)} bytes clean={clean} saw_clear={sc} head={out[:16].hex(' ')}")
                        hits += 1
print("hits:", hits)

print("\n=== Single byte XOR + LZW, check output size ===")
hits = 0
for x in range(256):
    xc = xor1(comp, x)
    for bw in [9]:
        for order in ['msb','lsb']:
            for cf in [False, True]:
                r = lzw_decode(xc, 0, 0, order, bw, require_clear_first=cf)
                if r is None: continue
                out, clean, sc = r
                if len(out) in TARGET_SIZES or (clean and len(out)>4):
                    print(f"  XOR=0x{x:02x} bw={bw} order={order} cf={cf} -> {len(out)} bytes clean={clean} head={out[:16].hex(' ')}")
                    hits += 1
print("hits:", hits)

print("\n=== key16 repeat XOR + LZW ===")
xc = xor_repeat(comp, key16)
for bw in [9]:
    for order in ['msb','lsb']:
        for cf in [False, True]:
            r = lzw_decode(xc, 0, 0, order, bw, require_clear_first=cf)
            if r is None: continue
            out, clean, sc = r
            if len(out) in TARGET_SIZES or (clean and len(out)>4):
                print(f"  key16 bw={bw} order={order} cf={cf} -> {len(out)} bytes clean={clean} head={out[:16].hex(' ')}")

print("\nDone.")
