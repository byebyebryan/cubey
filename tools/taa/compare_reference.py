#!/usr/bin/env python3
"""Compare half-float HDR/motion dumps from the GLSL and pinned HLSL fixtures."""
import argparse
import json
import math
from pathlib import Path
import struct


def half_values(path):
    data = path.read_bytes()
    return struct.unpack("<" + str(len(data) // 2) + "e", data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tolerance", type=float, default=0.005)
    args = parser.parse_args()
    files = sorted(args.candidate.glob("*.bin"), key=lambda path: int(path.stem))
    if len(files) != 24 or {p.name for p in files} != {p.name for p in args.reference.glob("*.bin")}:
        raise SystemExit("expected the same 24 accepted frames in both fixture runs")
    rows = []
    for path in files:
        candidate = half_values(path)
        reference = half_values(args.reference / path.name)
        if len(candidate) != len(reference):
            raise SystemExit("fixture output sizes differ")
        if not all(math.isfinite(value) for value in (*candidate, *reference)):
            raise SystemExit("fixture output contains nonfinite data")
        absolute = [abs(a - b) for a, b in zip(candidate, reference)]
        scaled = sorted(d / max(1.0, abs(b)) for d, b in zip(absolute, reference))
        rows.append({"frame": int(path.stem), "max_abs": max(absolute),
                     "max_scaled": max(scaled), "p99_scaled": scaled[int(.99 * (len(scaled) - 1))]})
    result = {"frames": rows, "max_scaled": max(row["max_scaled"] for row in rows),
              "tolerance": args.tolerance, "metric": "abs(candidate-reference)/max(1,abs(reference))"}
    result["pass"] = result["max_scaled"] <= args.tolerance
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"{'PASS' if result['pass'] else 'FAIL'}: maximum scaled half-float error "
          f"{result['max_scaled']:.8f}, tolerance {args.tolerance}")
    if not result["pass"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
