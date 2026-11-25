import struct

data = open('entry_1685','rb').read()
cs = struct.unpack_from('<I', data, 0x10)[0]
ds = struct.unpack_from('<I', data, 0x18)[0]
fl = struct.unpack_from('<I', data, 0x1c)[0]
comp = data[0x20:0x20+cs]
print(f'entry_1685: cs={cs} ds={ds} fl={fl:#x}')

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
    for i in range(256):
        d[i] = bytes([i])
    prev = None
    out = bytearray()
    saw_clear = False
    saw_end = False
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
            saw_end = True
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
    return bytes(out), saw_end, saw_clear

def xor1(buf, x):
    return bytes(b ^ x for b in buf)

def bit_reverse_byte(b):
    r = 0
    for i in range(8):
        r = (r << 1) | ((b >> i) & 1)
    return r

def bit_reverse_buf(buf):
    return bytes(bit_reverse_byte(b) for b in buf)

# Try XOR + bit reversal + LZW
hits = 0
for x in range(256):
    xc = xor1(comp, x)
    for br in [False, True]:
        bc = bit_reverse_buf(xc) if br else xc
        for bw in range(8,15):
            for order in ['msb','lsb']:
                for cf in [False, True]:
                    r = lzw_decode(bc, 0, 0, order, bw, require_clear_first=cf, max_output=ds+100)
                    if r is None: continue
                    out, clean, sc = r
                    if len(out) == ds:
                        print(f'  MATCH XOR=0x{x:02x} bitrev={br} bw={bw} order={order} cf={cf} clean={clean} head={out[:16].hex(" ")}')
                        hits += 1
print(f'hits: {hits}')
