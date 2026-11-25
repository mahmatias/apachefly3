import struct
from collections import Counter

data = open('D:/Games/C3/RESOURCE.RES','rb').read()

# Find all LZP1 occurrences
occ = []
off = 0
while True:
    i = data.find(b'LZP1', off)
    if i < 0: break
    occ.append(i)
    off = i + 1

# Collect entries
entries = []
for o in occ:
    entry_start = o - 0x14
    cs = struct.unpack_from('<I', data, o - 4)[0]
    ds = struct.unpack_from('<I', data, o + 4)[0]
    fl = struct.unpack_from('<I', data, o + 8)[0]
    entries.append((o, entry_start, cs, ds, fl))

# Group by (ds, fl) to see patterns
groups = Counter((ds, fl) for _,_,_,ds,fl in entries)
print('Top (ds, fl) groups:')
for (ds, fl), c in groups.most_common(20):
    print(f'  ds={ds} fl={fl:#x}: {c} entries')

print()

# For each unique ds value, show sample entries with their compressed data
for ds in sorted(set(ds for _,_,_,ds,_ in entries)):
    samples = [(o, es, cs, fl) for o, es, c, d, f in entries if d == ds][:3]
    print(f'ds={ds}:')
    for o, es, cs, fl in samples:
        comp = data[es+0x20:es+0x20+min(cs, 32)]
        print(f'  occ=0x{o:x} cs={cs} fl={fl:#x} comp[:32]={comp.hex(" ")}')
    print()
