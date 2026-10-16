#!/usr/bin/env python3
"""Matched surface-foam ablations; retained flecks, clocks and hydraulic fields."""
from __future__ import annotations

import argparse
from pathlib import Path

import review_whitewater_v1 as white

BASE = dict(white.VARIANTS["fast"])
FOAM = {**BASE, "water_stream_foam_strength": 1}
VARIANTS = {
    "no-surface": BASE,
    "current": FOAM,
    "dim": {**FOAM, "water_stream_foam_brightness": .4},
    "moderate": {**FOAM, "water_stream_foam_patchiness": 2 / 3},
    "sparse": {**FOAM, "water_stream_foam_patchiness": 1},
    "combined": {**FOAM, "water_stream_foam_patchiness": 1,
                 "water_stream_foam_brightness": .4},
    "subtle": {**BASE, "water_stream_foam_strength": .3,
               "water_stream_foam_patchiness": 1,
               "water_stream_foam_brightness": .55},
}


def run(args):
    retained_sources = white.sources

    def sources():
        pins = retained_sources()
        path = Path(__file__).resolve()
        pins[str(path.relative_to(white.ref.ROOT))] = white.ref.sha256_file(path)
        return pins

    # Reuse the existing capture/provenance implementation in this process only.
    old_variants = white.VARIANTS
    white.VARIANTS, white.sources = VARIANTS, sources
    try:
        white.run(args)
    finally:
        white.VARIANTS, white.sources = old_variants, retained_sources


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--mode", choices=("stills", "motion", "advancing", "profile"), default="stills")
    parser.add_argument("--scenes", nargs="+", choices=[s[0] for s in white.review.SCENES], default=["streams", "lake", "wide"])
    parser.add_argument("--variants", nargs="+", choices=tuple(VARIANTS), default=list(VARIANTS))
    parser.add_argument("--fps", type=int, choices=(30, 60), default=30)
    parser.add_argument("--seconds", type=int, choices=(4, 12, 16), default=16)
    parser.add_argument("--budget", type=int)
    parser.add_argument("--bank")
    run(parser.parse_args())
