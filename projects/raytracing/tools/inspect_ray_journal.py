"""Decode a private ray journal without losing its original float32 bit patterns."""

import argparse
import json
import math
import struct
from pathlib import Path

KINDS = {
    1: "primary sample",
    2: "sample result",
    10: "traversal result",
    11: "reconstructed surface",
    12: "adjacent face",
    13: "triangle vertices",
    14: "spawn",
    20: "dequeue branch",
    21: "path iteration",
    40: "dielectric boundary",
    41: "main scatter",
    42: "queued reflection",
    101: "inside miss",
    102: "wrong instance",
    103: "orientation mismatch",
}


def floats(words):
    return [
        value if math.isfinite(value) else None
        for value in struct.unpack("<4f", struct.pack("<4I", *words))
    ]


def load(path):
    journal = json.loads(Path(path).read_text())
    if journal["format"] != "cubey.ray-journal.v1":
        raise ValueError("unsupported journal format")
    count, capacity = journal["attempted_records"], journal["capacity"]
    if journal["overflow"] or count > capacity or count != len(journal["records"]):
        raise ValueError("incomplete or overflowing journal")
    if not count or not 0 <= journal["first_failure_record_plus_one"] <= count:
        raise ValueError("empty journal or invalid failure index")
    for record in journal["records"]:
        if len(record) != 8 or any(len(words) != 4 for words in record):
            raise ValueError("invalid packet shape")
        if any(
            type(word) is not int or not 0 <= word <= 0xFFFFFFFF
            for row in record
            for word in row
        ):
            raise ValueError("packet word is not uint32")
    return journal


def decode(journal):
    events = []
    for index, packet in enumerate(journal["records"]):
        kind, branch, depth, medium = packet[0]
        events.append(
            {
                "record": index,
                "kind": kind,
                "event": KINDS.get(kind, "unknown"),
                "branch": branch,
                "depth": depth,
                "medium": medium if medium < 0x80000000 else medium - 0x100000000,
                "float_payload": [floats(words) for words in packet[1:]],
                "uint_payload": packet[1:],
            }
        )
    return {
        "note": "Payload interpretation is documented in raytracing/README.md; integer fields retain uint payloads.",
        "control": journal["control"],
        "failure_record": journal["first_failure_record_plus_one"] - 1,
        "events": events,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("journal", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--compare", type=Path, help="compare raw event bits with another journal"
    )
    args = parser.parse_args()
    journal = load(args.journal)
    result = decode(journal)
    if args.compare:
        other = load(args.compare)
        a, b = journal["records"], other["records"]
        common = next(
            (i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b))
        )
        result["comparison"] = {
            "common_prefix_records": common,
            "bit_identical": a == b,
            "other_records": len(b),
            "other_path": str(args.compare),
        }
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n")
    print(f"{len(result['events'])} events; failure record {result['failure_record']}")
    if "comparison" in result:
        print(json.dumps(result["comparison"]))


if __name__ == "__main__":
    main()
