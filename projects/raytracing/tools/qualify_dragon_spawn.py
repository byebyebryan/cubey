# /// script
# dependencies = ["numpy==2.4.6", "pillow==12.0.0"]
# ///
"""Opt-in full-HD regression for the Q0 thin-fold spawn failure.

Requires the prepared certified dragon and a normal, uninstrumented RT build.
The seed-7 prefix of 1336 includes the previously failing pixel/sample. Full
images are checked, including the adjacent pixels and earlier logical samples.
"""

import argparse
import json
from pathlib import Path

from measure_dragon_glass import PRESET, VIEWS, capture, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--binary", type=Path, default=Path("build/dev/projects/raytracing/raytracing")
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--references", action="store_true", help="also run six 4096-SPP views"
    )
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    shaders = args.binary.resolve().parent / "shaders"
    assert list(shaders.glob("*.spv")), (
        "use a build with its adjacent private/normal shader set"
    )

    def identity():
        return {
            "binary": digest(args.binary),
            "shaders": {p.name: digest(p) for p in sorted(shaders.glob("*.spv"))},
            "preset": digest(PRESET),
            "model": digest(json.loads(PRESET.read_text())["model"]),
            "materials": {
                p.name: digest(p)
                for p in sorted(Path("assets/materials").glob("*.json"))
            },
        }

    pinned = identity()
    identity_path = args.output / "spawn-identity.json"
    if identity_path.exists():
        assert json.loads(identity_path.read_text()) == pinned, (
            "qualification inputs changed"
        )
    else:
        assert not list(args.output.glob("*.receipt.json")), (
            "cannot adopt unpinned prior captures"
        )
        identity_path.write_text(json.dumps(pinned, indent=2) + "\n")
    results = []

    def run(name, samples, seed=7, extra=()):
        assert identity() == pinned, "qualification inputs changed"
        results.append(
            capture(args.binary, args.output, name, 1920, 1080, samples, seed, extra)
        )
        assert identity() == pinned, "qualification inputs changed during capture"

    for samples in (1335, 1336, 2048):
        run(f"canonical-{samples}-s7", samples)
    run(
        "canonical-2048-s7-deep",
        2048,
        extra=[
            "--max-events",
            "256",
            "--max-path-rays",
            "2048",
            "--max-total-rays",
            "8192",
        ],
    )
    run("canonical-2048-s7-no-shadows", 2048, extra=["--no-shadows"])
    if args.references:
        for view, extra in VIEWS.items():
            for seed in (19, 43):
                run(f"{view}-4096-s{seed}", 4096, seed, extra)
    (args.output / "spawn-qualification.json").write_text(
        json.dumps({"passed": True, "captures": results}, indent=2) + "\n"
    )


if __name__ == "__main__":
    main()
