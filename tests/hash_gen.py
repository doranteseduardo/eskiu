#!/usr/bin/env python3
"""Print the "lcg" lines of tests/hash.expected from Python's hashlib.

tests/hash.esk hashes data from the same 32-bit LCG; run this and compare with
`grep ^lcg tests/hash.expected` after changing either side.
"""
import hashlib

x, buf = 12345, bytearray()
for _ in range(1000):
    x = (x * 1103515245 + 12345) & 0xFFFFFFFF
    buf.append((x >> 16) & 0xFF)
for n in list(range(0, 130, 13)) + [1000]:
    d = bytes(buf[:n])
    print(f"lcg{n} {hashlib.sha256(d).hexdigest()} {hashlib.sha1(d).hexdigest()} {hashlib.md5(d).hexdigest()}")
