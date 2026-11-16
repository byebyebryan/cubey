"""CPU-only transcription of the known ray's primary RNG inputs; no GPU replay."""

import argparse
import json
import struct
from pathlib import Path


def shader_hash(value):
    value &= 0xFFFFFFFF
    value ^= value >> 16
    value = (value * 0x7FEB352D) & 0xFFFFFFFF
    value ^= value >> 15
    value = (value * 0x846CA68B) & 0xFFFFFFFF
    return (value ^ (value >> 16)) & 0xFFFFFFFF


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def check():
    pixel, sample = 908 + 344 * 1920, 1335
    random_key = shader_hash(pixel ^ shader_hash(7))
    batch, within = sample >> 6, sample & 63
    key = shader_hash(
        random_key ^ shader_hash(0x68E31DA4) ^ shader_hash(batch + 0xB5297A4D)
    )
    cell = (within * ((key >> 6) | 1) + (key & 63)) & 63
    jitter = [
        shader_hash(key ^ shader_hash(within + constant)) >> 8
        for constant in [0x9E3779B9, 0x85EBCA6B]
    ]
    subpixel = [
        min(
            1 - 1 / 16777216,
            max(1 / 16777216, f32(f32(float(c) + f32(j / 16777216)) / 8)),
        )
        for c, j in zip([cell & 7, cell >> 3], jitter, strict=True)
    ]
    uv = [
        f32(f32(float(p) + v) / dimension)
        for p, v, dimension in zip([908, 344], subpixel, [1920, 1080], strict=True)
    ]
    return {
        "source": "projects/raytracing/shaders/material_workshop.glsl, rtHash/rtStratified dimension 0",
        "pixel": [908, 344],
        "logical_extent": [1920, 1080],
        "seed": 7,
        "zero_based_sample": sample,
        "linear_pixel": pixel,
        "random_key": random_key,
        "batch": batch,
        "within": within,
        "dimension_0_key": key,
        "cell": cell,
        "jitter_u24": jitter,
        "cpu_float32_subpixel": subpixel,
        "cpu_float32_uv": uv,
        "scope": "Unsigned integer arithmetic transcribed from current shader; explicit CPU float32 rounding for expected primary jitter. Not a GPU replay, camera ray, failure trace, or verified GPU float-bit match.",
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(check(), indent=2) + "\n")
    print("CPU replay-input check written:", args.output)
