#!/usr/bin/env python3
"""Expand deduplicated TAA evidence into the existing SDF action-study format."""
import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    packet = json.loads(args.input.read_text())
    if packet.get("format") != "cubey.taa.action-evidence" or packet.get("version") != 1:
        raise SystemExit("Expected cubey.taa.action-evidence version 1")
    manifest = packet["manifest"]
    for action in manifest["actions"]:
        for field in ("scene", "visible_scene"):
            value = action.get(field)
            if isinstance(value, dict) and set(value) == {"$scene"}:
                action[field] = packet["scenes"][value["$scene"]]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
