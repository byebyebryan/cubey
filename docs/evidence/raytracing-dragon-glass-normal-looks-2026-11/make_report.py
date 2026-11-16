# /// script
# dependencies = ["numpy==2.4.6", "pillow==12.0.0"]
# ///
"""Bundle original paired PNG bytes and their capture/build identities."""

import argparse
import hashlib
import json
import shutil
from pathlib import Path


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--assessment", required=True)
    args = parser.parse_args()
    root = args.run_root.resolve()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    (out / "images").mkdir()
    definitions = [
        ("canonical-s19", "Front", 4096, 19),
        ("canonical-s43", "Front noise check", 4096, 43),
        ("elevated-s19", "Elevated", 4096, 19),
        ("grazing-s19", "Side / grazing", 4096, 19),
        ("isolated-s19", "Plain base + sky only", 2048, 19),
    ]
    front = {
        "scene": [0, 0, 1920, 1080],
        "head": [610, 205, 475, 335],
        "body": [710, 375, 625, 465],
        "base": [655, 655, 700, 285],
    }
    crops = {
        "canonical": front,
        "isolated": front,
        "elevated": {
            "scene": [0, 0, 1920, 1080],
            "head": [610, 180, 500, 360],
            "body": [700, 360, 660, 450],
            "base": [650, 620, 760, 350],
        },
        "grazing": {
            "scene": [0, 0, 1920, 1080],
            "head": [805, 230, 420, 320],
            "body": [815, 420, 455, 440],
            "base": [735, 700, 565, 245],
        },
    }
    cases = {}
    receipts = {}
    for key, title, samples, seed in definitions:
        case = {
            "title": title,
            "samples": samples,
            "seed": seed,
            "crops": crops[key.rsplit("-s", 1)[0]],
        }
        for policy in ["geometric", "smooth"]:
            name = key + "-" + policy
            prefix = root / "captures" / name
            receipt = json.loads(Path(str(prefix) + ".receipt.json").read_text())
            assert receipt["samples"] == samples and receipt["seed"] == seed
            assert receipt["finite_output"] and receipt["validation_clean"]
            for suffix, sha in receipt["artifacts"].items():
                assert digest(str(prefix) + suffix) == sha
            image = "images/" + name + ".png"
            shutil.copy2(str(prefix) + ".png", out / image)
            case[policy] = {
                "image": image,
                "sha256": digest(out / image),
                "status": receipt["status_or_pixels"],
                "transport_accepted": receipt["transport_accepted"],
            }
            receipts[name] = receipt
        cases[key] = case
    pair = json.loads((root / "pair-identity.json").read_text())
    fidelity = json.loads((root / "geometric-fidelity.json").read_text())
    fullhd = json.loads((root / "fullhd-display-differences.json").read_text())
    maximum_baseline_code_difference = max(
        row["private_geometric_vs_retained_q01"]["max_channel_rgb8"] for row in fullhd
    )
    data = {
        "cases": cases,
        "assessment": args.assessment,
        "fidelity": "The private geometric build reproduces the ordinary renderer byte for byte in both PNG and raw HDR at the 960 × 540 / 128-SPP pilot. Its full-HD geometric images differ from the retained Q0.1 images by at most "
        + str(int(maximum_baseline_code_difference))
        + " RGB8 code per channel; they are not universally byte-identical."
        if fidelity["hdr_byte_identical"] and fidelity["png_byte_identical"]
        else "The private geometric fidelity control is recorded in the manifest; it is not universally byte-identical.",
        "statusNames": {
            "1": "query limit",
            "2": "event limit",
            "4": "radiance limit",
            "8": "zero throughput",
            "16": "invalid medium",
            "32": "shadow limit",
            "64": "stack limit",
        },
    }
    manifest = {
        "scope": "paired settled appearance experiment; physical/temporal/default promotion not claimed",
        "data": data,
        "pair_identity": pair,
        "pilot_fidelity": fidelity,
        "fullhd_display_differences": fullhd,
        "receipts": receipts,
        "source_files": {
            str(p): digest(p)
            for p in [
                Path(__file__),
                Path("projects/raytracing/tools/compare_dragon_normals.py"),
                Path("projects/raytracing/shaders/optical_normal_experiment.glsl"),
            ]
        },
    }
    (out / "comparison.json").write_text(json.dumps(manifest, indent=2) + "\n")
    template = Path(__file__).with_name("report-template.html").read_text()
    (out / "index.html").write_text(
        template.replace("/*COMPARISON_DATA*/{}", json.dumps(data, allow_nan=False))
    )
    print(
        json.dumps(
            {
                "bundle": str(out),
                "pairs": len(cases),
                "files": len(list(out.rglob("*"))),
            }
        )
    )


if __name__ == "__main__":
    main()
