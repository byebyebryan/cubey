# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy==2.4.6", "pillow==12.0.0", "matplotlib==3.10.7"]
# ///
"""Prepare the explicit Q1 diagnostic report bundle; never publishes by itself."""

import argparse
import json
import shutil
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

sys.path.insert(0, str(Path("projects/raytracing/tools").resolve()))
import optical_normal_reference as ref


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    run = args.run_root.resolve()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    here = Path(__file__).resolve().parent
    data = json.loads((here / "analysis.json").read_text())
    validation = json.loads((here / "validation.json").read_text())
    raw = (
        np.fromfile(run / "probe-02/probe.bin", dtype="<f4")
        .reshape(128, 512, 16, 4)
        .astype(float)
    )
    rows = {2: 72, 5: 73, 10: 74, 20: 75}
    seams = {}
    for tilt, row in rows.items():
        records = []
        for value in raw[row]:
            records.append(
                {
                    "theta": float(value[11, 0]),
                    "incoming": value[0, :3].tolist(),
                    "normal": value[3, :3].tolist(),
                    "reason": int(value[3, 3]),
                    "reflected": value[4, :3].tolist(),
                    "transmitted": value[5, :3].tolist(),
                    "fresnel": float(value[4, 3]),
                }
            )
        seams[str(tilt)] = records
    plt.rcParams.update(
        {"font.size": 10, "axes.spines.top": False, "axes.spines.right": False}
    )
    fig, axes = plt.subplots(1, 3, figsize=(12, 3.5), layout="constrained")
    row = raw[75]
    theta = row[:, 11, 0]
    reflection = np.rad2deg(np.arctan2(row[:, 4, 0], row[:, 4, 2]))
    transmission = np.rad2deg(np.arctan2(row[:, 5, 0], -row[:, 5, 2]))
    for ax, values, base, label in zip(
        axes,
        [reflection, transmission, row[:, 4, 3]],
        [
            np.rad2deg(np.arctan2(row[:, 8, 0], row[:, 8, 2])),
            np.rad2deg(np.arctan2(row[:, 9, 0], -row[:, 9, 2])),
            row[:, 8, 3],
        ],
        [
            "Reflected angle (degrees)",
            "Transmitted angle (degrees)",
            "Fresnel reflectance",
        ],
    ):
        ax.plot(theta, base, color="#999999", linestyle="--", label="Geometric")
        ax.plot(theta, values, color="#13749b", label="Guarded smooth")
        ax.set_xlabel("Input incidence (degrees)")
        ax.set_ylabel(label)
        ax.grid(alpha=0.15)
        ax.ticklabel_format(axis="x", useOffset=False)
    axes[0].legend(frameon=False, fontsize=8)
    fig.savefig(out / "seam.svg")
    plt.close(fig)
    fig, ax = plt.subplots(figsize=(8, 3.8), layout="constrained")
    for tilt, color in zip(
        [2, 5, 10, 20], ["#236e82", "#31a8af", "#ba8215", "#b7404c"]
    ):
        values = [v for v in data["energy_curves"] if v["tilt_degrees"] == tilt]
        ax.plot(
            [v["beam_angle_degrees"] for v in values],
            [v["total_ratio"] for v in values],
            "-o",
            color=color,
            label=f"{tilt} degree normal tilt",
            markersize=4,
        )
    ax.axhline(1, color="#777", linestyle="--", label="Geometric: 1")
    ax.set_xlabel("Exterior beam center (degrees)")
    ax.set_ylabel("Outgoing flux / incoming flux")
    ax.set_ylim(0, 2.9)
    ax.grid(alpha=0.15)
    ax.legend(frameon=False, fontsize=8, ncol=2)
    fig.savefig(out / "flux.svg")
    plt.close(fig)
    # Analytic illustration, not a scene render. A raw normal varies linearly
    # between two vertex normals on one flat interface. A low-frequency analytic
    # environment maps direction to RGB. Opposite-medium reduced radiance uses
    # eta^2 so the Fresnel comparison itself does not add an eta brightness bias.
    width, height = 1024, 192
    u = (np.arange(width) + 0.5) / width
    raw_normal = (1 - u[:, None]) * ref.tilted(-30) + u[:, None] * ref.tilted(20)
    incoming = -ref.tilted(50)
    ng = np.array([0.0, 0.0, 1.0])
    environment = lambda direction: 0.1 + 0.9 * (direction * 0.5 + 0.5)
    field = {}
    for name, candidate in [("baseline", False), ("candidate", True)]:
        boundary = ref.interface(incoming, ng, raw_normal, 1.5, candidate)
        f = boundary["fresnel"][:, None]
        color = f * environment(boundary["reflected"]) + (1 - f) * environment(
            boundary["transmitted"]
        )
        srgb = np.where(
            color <= 0.0031308, 12.92 * color, 1.055 * color ** (1 / 2.4) - 0.055
        )
        pixels = np.tile(
            np.uint8(np.round(np.clip(srgb, 0, 1) * 255))[None], (height, 1, 1)
        )
        Image.fromarray(pixels).save(out / ("field-" + name + ".png"))
        field[name] = {
            "reason": boundary["reason"].tolist(),
            "fresnel": boundary["fresnel"].tolist(),
        }
    embedded = {
        "seams": seams,
        "analysis": data,
        "validation": validation,
        "illustration_scope": "CPU analytic flat-interface normal field, not a dragon image",
    }
    html = (
        (here / "report-template.html")
        .read_text()
        .replace("/*REPORT_DATA*/{}", json.dumps(embedded, allow_nan=False))
    )
    (out / "index.html").write_text(html)
    for name in ["analysis.json", "validation.json", "preservation.json"]:
        shutil.copy2(here / name, out / name)
    (out / "seam-data.json").write_text(json.dumps(seams, allow_nan=False) + "\n")
    (out / "field-data.json").write_text(json.dumps(field, allow_nan=False) + "\n")
    print(json.dumps({"bundle": str(out), "files": [p.name for p in out.iterdir()]}))


if __name__ == "__main__":
    main()
