import struct
import sys

data = open('D:/Games/C3/RESOURCE.RES','rb').read()
print('file size:', len(data))

# Find all LZP1 occurrences
occ = []
off = 0
while True:
    i = data.find(b'LZP1', off)
    if i < 0: break
    occ.append(i)
    off = i + 1
print(f'LZP1 occurrences: {len(occ)}')

# Collect entry info
entries = []
for o in occ:
    entry_start = o - 0x14
    if entry_start < 0:
        continue
    cs = struct.unpack_from('<I', data, o - 4)[0]    # compressed_size
    ds = struct.unpack_from('<I', data, o + 4)[0]    # decompressed_size
    fl = struct.unpack_from('<I', data, o + 8)[0]    # flags
    entries.append((o, entry_start, cs, ds, fl))

# Print all entries sorted by decompressed size (target)
print(f'\nAll entries ({len(entries)}):')
for o, es, cs, ds, fl in entries:
    print(f'  occ=0x{o:x} entry=0x{es:x} cs={cs} ds={ds} fl={fl:#x}')

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
    for i in range(256):
        d[i] = bytes([i])
    prev = None
    out = bytearray()
    saw_clear = False

    def read_bits(n):
        nonlocal pos
        if pos + n > nbits:
            return None
        v = 0
        if order == 'msb':
            for b in range(n):
                bi = (pos+b)//8
                bit = 7-((pos+b)%8)
                if bi >= len(sub):
                    return None
                v = (v<<1) | ((sub[bi]>>bit)&1)
        else:
            for b in range(n):
                bi = (pos+b)//8
                bit = (pos+b)%8
                if bi >= len(sub):
                    return None
                v |= ((sub[bi]>>bit)&1) << b
        pos += n
        return v

    saw_end = False
    while pos < nbits:
        code = read_bits(bw)
        if code is None:
            break
        if code == end_code:
            saw_end = True
            return bytes(out), True, saw_clear
        if code == clear_code:
            saw_clear = True
            dsize = first_free
            bw = bw_start
            max_code = (1 << bw) - 1
            prev = None
            continue
        if prev is None:
            if code < 256:
                out.append(code)
                prev = code
            else:
                return None
            continue
        if code < dsize:
            seq = d[code]
            out.extend(seq)
            if dsize < max_dict:
                d[dsize] = d[prev] + seq[0:1]
                dsize += 1
                if dsize > max_code and bw < max_w:
                    bw += 1
                    max_code = (1 << bw) - 1
            prev = code
        elif code == dsize:
            seq = d[prev] + d[prev][0:1]
            out.extend(seq)
            if dsize < max_dict:
                d[dsize] = seq
                dsize += 1
                if dsize > max_code and bw < max_w:
                    bw += 1
                    max_code = (1 << bw) - 1
            prev = code
        else:
            return None
        if len(out) > max_output:
            return None
    return bytes(out), saw_end, saw_clear

def xor1(buf, x):
    return bytes(b ^ x for b in buf)

def xor_repeat(buf, key):
    return bytes(b ^ key[i % len(key)] for i, b in enumerate(buf))

# Try to decompress each entry with various LZW configurations
# For each entry, try to match claimed decompressed_size (ds)

print('\n=== Brute-force LZW on real entries (match claimed decompressed_size) ===')

hits = 0
for o, es, cs, ds, fl in entries[:50]:  # try first 50 entries
    comp = data[es+0x20:es+0x20+cs]
    found_for_entry = False
    for bw in range(8, 15):
        for order in ['msb', 'lsb']:
            for cf in [False, True]:
                for off in range(0, 4):
                    for al in range(8):
                        r = lzw_decode(comp, off, al, order, bw,
                                       require_clear_first=cf,
                                       max_output=ds+1000)
                        if r is None:
                            continue
                        out, clean, sc = r
                        if len(out) == ds:
                            print(f'  occ=0x{o:x} MATCH ds={ds} bw={bw} order={order} cf={cf} off={off} al={al} clean={clean} saw_clear={sc} head={out[:16].hex(" ")}')
                            hits += 1
                            found_for_entry = True
                            break
                    if found_for_entry:
                        break
                if found_for_entry:
                    break
            if found_for_entry:
                break
        if found_for_entry:
            break

print(f'hits (exact ds match): {hits}')
