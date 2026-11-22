#!/usr/bin/env python3
"""Recursive-descent listing of the D-50 firmware (v1.06) from the sweeps dasm.wasm makes (main.cpp), for docs/D50-FIRMWARE.md.

usage: analyze.py FIRMWARE.bin SWEEP_BANK2.txt SWEEP_BANK3.txt OUT_DIR
The listing is by file offset (the CPU address, plus 0x4000 in the banked window 0x8000-0xBFFF when bank 3 (file 0xC000..) is meant), so it is
a derivative of Roland's code: OUT_DIR is under roms/out (git-ignored). Writes listing.txt, functions.txt (entry, size, callers) and xrefs.txt.
"""
import re, sys, collections

rom = open(sys.argv[1], 'rb').read()
rom += b'\xff' * (0x10000 - len(rom))

def load(p):
    d = {}
    for l in open(p, encoding='ascii', errors='replace'):
        a, n, f, t = l.rstrip('\n').split(' ', 3)
        d[int(a, 16)] = (int(n), f, t)
    return d
sw = {2: load(sys.argv[2]), 3: load(sys.argv[3])}

def foff(addr, bank):                       # CPU address in a bank view -> file offset
    return addr + 0x4000 if bank == 3 and 0x8000 <= addr < 0xC000 else addr
def in_rom(addr): return addr < 0xC000

code = {}                                    # file offset -> (len, flags, text, bank)
calls = collections.defaultdict(set)         # target (file offset) -> callers
jumps = collections.defaultdict(set)
func = set()
seen = set()
work = []

def push(addr, bank, frm=None, kind='j'):
    if not in_rom(addr) or addr < 0x80 and addr != 0:   # (below 0x80: the vectors and the BR table, data)
        return
    for b in ((2, 3) if 0x8000 <= addr < 0xC000 and bank == 0 else (bank or 2,)):
        o = foff(addr, b)
        if frm is not None:
            (calls if kind == 'c' else jumps)[o].add(frm)
        if kind == 'c':
            func.add(o)
        if (addr, b) not in seen:
            seen.add((addr, b)); work.append((addr, b))

def run():
    while work:
        addr, bank = work.pop()
        while in_rom(addr):
            o = foff(addr, bank)
            if o in code and code[o][3] == bank:
                break
            n, f, t = sw[2 if bank == 0 else bank][addr]
            if n == 0 or addr + n > 0xC000:
                break
            code[o] = (n, f, t, bank)
            seen.add((addr, bank))
            m = t.split()
            mn = m[0] if m else ''
            nxt = addr + n
            ops = t[len(mn):]
            tg = [int(x, 16) for x in re.findall(r'\$0?([0-9A-F]{4})H', ops)] + [int(x, 16) for x in re.findall(r'(?<![0-9A-F#$])!0?([0-9A-F]{4})H', ops)]
            if mn == 'CALL' or mn == 'CALLF':
                for x in tg: push(x, 0 if x < 0x8000 else bank, o, 'c')
            elif mn == 'BR':
                for x in tg: push(x, 0 if x < 0x8000 else bank, o)
                break
            elif mn.startswith('B') or mn.startswith('DBNZ') or mn.startswith('NOT1') and False:
                for x in tg: push(x, 0 if x < 0x8000 else bank, o)
            elif mn in ('RET', 'RETI', 'RETB'):
                break
            addr = nxt

# seeds: reset, the vectors that point into the ROM, the BR table at 0x40
seeds = [0x0699]
for a in range(0, 0x40, 2):
    w = rom[a] | rom[a + 1] << 8
    if 0x80 <= w < 0xC000: seeds.append(w)
for a in range(0x40, 0x79, 3):
    if rom[a] == 0x2c: seeds.append(rom[a + 1] | rom[a + 2] << 8)
for s in seeds:
    func.add(s); push(s, 0, None)
run()
extra = [int(x, 16) for x in sys.argv[5:]] if len(sys.argv) > 5 else []
for s in extra:
    func.add(s); push(s, 0, None); run()

out = sys.argv[4]
import os
os.makedirs(out, exist_ok=True)
def label(o, bank=None):
    a = o if o < 0xC000 else o - 0x4000
    sfx = '_B3' if o >= 0xC000 else ''
    return ('F_' if o in func else 'L_') + '%04X%s' % (a, sfx)
targets = set(calls) | set(jumps)
L = open(out + '/listing.txt', 'w')
o = 0
while o < 0x10000:
    if o in code:
        n, f, t, b = code[o]
        if o in targets or o in func:
            L.write('\n%s:%s\n' % (label(o), '   ; called from ' + ' '.join('%04X' % x for x in sorted(calls.get(o, ()))[:6]) if o in calls else ''))
        L.write('  %05X  %-9s %s\n' % (o, rom[o:o + n].hex(), t))
        o += n
    else:
        e = o
        while e < 0x10000 and e not in code: e += 1
        for a in range(o, e, 16):
            chunk = rom[a:min(a + 16, e)]
            L.write('  %05X  DB %-48s ; %s\n' % (a, ' '.join('%02X' % c for c in chunk), ''.join(chr(c) if 32 <= c < 127 else '.' for c in chunk)))
        o = e
L.close()
F = open(out + '/functions.txt', 'w')
for fo in sorted(func):
    end = fo
    while end in code: end += code[end][0]
    F.write('%s size %d callers %s\n' % (label(fo), end - fo, ' '.join('%04X' % x for x in sorted(calls.get(fo, ()))[:8])))
F.close()
cov = sum(code[x][0] for x in code)
print('instructions', len(code), 'bytes', cov, 'functions', len(func), 'of file 0x10000; 0x2000..0xFFFF is', 0x10000 - 0x2000)
