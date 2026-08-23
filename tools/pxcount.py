# -*- coding: utf-8 -*-
"""Счётчик пикселей заданных цветов на скриншотах (чистый Python, PNG GDI+)."""
import os
import struct
import sys
import zlib


def load_png(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    pos, w, h, idat = 8, 0, 0, b''
    while pos < len(data):
        ln, tag = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + ln]
        if tag == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
            assert depth == 8 and ctype == 6
        elif tag == b'IDAT':
            idat += body
        pos += 12 + ln
    raw = zlib.decompress(idat)
    stride = w * 4
    out = bytearray(w * h * 4)
    prev = bytearray(stride)
    src = 0
    for y in range(h):
        f = raw[src]; src += 1
        line = bytearray(raw[src:src + stride]); src += stride
        if f == 1:
            for i in range(4, stride):
                line[i] = (line[i] + line[i - 4]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i - 4] if i >= 4 else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - 4] if i >= 4 else 0
                c = prev[i - 4] if i >= 4 else 0
                b = prev[i]
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, out


def count(path, targets, tol=25):
    w, h, px = load_png(path)
    res = [0] * len(targets)
    for i in range(0, len(px), 4):
        r, g, b = px[i], px[i + 1], px[i + 2]
        for t, (tr, tg, tb) in enumerate(targets):
            if abs(r - tr) <= tol and abs(g - tg) <= tol and abs(b - tb) <= tol:
                res[t] += 1
    return ' '.join('%s:%d' % (t, c) for t, c in zip(targets, res))


if __name__ == '__main__':
    tmp = os.environ['TEMP']
    O, G, R = (255, 146, 0), (0, 255, 62), (230, 57, 70)
    print('blink1:', count(tmp + r'\opencode\v2_blink1.png', [O, G]))
    print('blink2:', count(tmp + r'\opencode\v2_blink2.png', [O, G]))
    for i in range(9):
        f = tmp + r'\opencode\v2_flash_%d.png' % i
        print('flash_%d:' % i, count(f, [R]))
