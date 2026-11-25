import os

# Shard 0 (RESOURCE.RES) in ISO: extent 71984, size 117141520
offset = 71984 * 2048
size = 117141520

with open('D:/Trabalho/Comanche/c3_ptbr.iso', 'rb') as f:
    f.seek(max(0, offset + size - 51200))
    data = f.read(51200)
    print(f'read {len(data)} bytes')

i = 0
count = 0
while i < len(data) - 1 and count < 20:
    if data[i] == 0:
        i += 1
        continue
    if 32 <= data[i] <= 126:
        j = i
        while j < len(data) and data[j] != 0:
            j += 1
        if j > i:
            name = data[i:j].decode('ascii', errors='ignore')
            if len(name) >= 3 and name.isprintable():
                if any(c.isalpha() for c in name) and '.PCX' in name:
                    rel_pos = size - 51200 + i
                    print(f'  Name at rel {rel_pos:08x}: {name}')
                    count += 1
            i = j + 1
        else:
            i += 1
    else:
        i += 1