#!/usr/bin/env python3
"""Write a deterministic Radiance environment for rough-transmission inspection.

This is generated demo lighting content, shared by the mesh and SDF captures.
It contains no external images. Sharp bars make optical filtering visible.
"""
import argparse
import math
from pathlib import Path


def write_environment(path: Path, width=1024, height=512):
    pixels = bytearray()
    for y in range(height):
        latitude = math.pi * ((y + .5) / height - .5)
        for x in range(width):
            longitude = 2 * math.pi * (x + .5) / width
            bar = int(longitude / (2 * math.pi) * 48) % 2
            value = .025 if bar else 1.8
            # Keep the bars readable, with a broad ceiling light for highlights.
            ceiling = 2 * max(0, math.sin(latitude)) ** 8
            color = (value + ceiling, value * .96 + ceiling, value * .88 + ceiling)
            _, exponent = math.frexp(max(color))
            multiplier = math.ldexp(256.0, -exponent)
            pixels.extend(min(255, int(c * multiplier)) for c in color)
            pixels.append(exponent + 128)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(f"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {height} +X {width}\n".encode() + pixels)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    write_environment(parser.parse_args().output)
