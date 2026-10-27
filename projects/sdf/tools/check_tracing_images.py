#!/usr/bin/env python3
"""Check native transport diagnostics without a golden-image comparison.

The fixed homogeneous sphere's center must enter the bounded glass path (blue),
and no diagnostic pixel may report exhaustion (magenta).
"""
import argparse
import json
from pathlib import Path
import struct
import zlib


def pixels(path):
    data = path.read_bytes(); offset = 8; compressed = b""
    while offset < len(data):
        length = struct.unpack("!I", data[offset:offset+4])[0]
        kind = data[offset+4:offset+8]; payload = data[offset+8:offset+8+length]
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = struct.unpack("!2I5B", payload)
            if depth != 8 or color not in (2, 6) or compression or filtering or interlace:
                raise ValueError("expected 8-bit native RGB/RGBA PNG")
        if kind == b"IDAT":
            compressed += payload
        offset += length+12
    channels = 4 if color == 6 else 3; stride = width*channels
    raw = zlib.decompress(compressed); rows = []; previous = bytearray(stride)
    def paeth(a, b, c):
        p = a+b-c; distances = [abs(p-a), abs(p-b), abs(p-c)]
        return [a, b, c][distances.index(min(distances))]
    for y in range(height):
        start = y*(stride+1); kind = raw[start]; row = bytearray(raw[start+1:start+1+stride])
        if kind not in range(5):
            raise ValueError("invalid PNG filter")
        for x in range(stride):
            left = row[x-channels] if x >= channels else 0
            up = previous[x]; corner = previous[x-channels] if x >= channels else 0
            predictor = [0, left, up, (left+up)//2, paeth(left, up, corner)][kind]
            row[x] = (row[x]+predictor)&255
        rows.append(row); previous = row
    return width, height, channels, rows


def main():
    p = argparse.ArgumentParser(description=__doc__); p.add_argument("directory", type=Path)
    args = p.parse_args(); receipt = []
    for mode in ("sampled", "analytic", "hybrid"):
        file = args.directory/f"glass-{mode}-transport.png"
        width, height, channels, rows = pixels(file)
        center = list(rows[height//2][width//2*channels:width//2*channels+3])
        magenta = sum(row[i] > 245 and row[i+1] < 10 and row[i+2] > 245
                      for row in rows for i in range(0, len(row), channels))
        passed = center[2] >= 32 and center[0] < 10 and center[1] < 10 and not magenta
        receipt.append(dict(mode=mode, center_rgb=center, exhaustion_pixels=magenta, passed=passed))
    print(json.dumps(receipt, indent=2))
    if not all(r["passed"] for r in receipt):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
