#!/usr/bin/env python3
"""show.py SWEEP ADDR [COUNT]: the sweep's linear disassembly from ADDR (hex), COUNT instructions (default 40)."""
import sys
d = {}
for l in open(sys.argv[1], encoding='ascii', errors='replace'):
    a, n, f, t = l.rstrip('\n').split(' ', 3); d[int(a, 16)] = (int(n), t)
a = int(sys.argv[2], 16)
for _ in range(int(sys.argv[3]) if len(sys.argv) > 3 else 40):
    n, t = d[a]; print('%04X  %s' % (a, t)); a += n
