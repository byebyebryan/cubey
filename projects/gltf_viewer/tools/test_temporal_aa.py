"""Submit a native static glTF through the viewer and verify AA admission and pixels."""

import csv
import json
import struct
import subprocess
import sys
from pathlib import Path


def write_triangles(directory):
    """One front-facing red triangle, with identical geometry in both material cases."""
    positions = (-1, -1, 0, 1, -1, 0, 0.15, 1, 0)
    normals = (0, 0, 1) * 3
    (directory / "triangle.bin").write_bytes(struct.pack("<18f", *positions, *normals))
    for name, alpha in (("opaque", "OPAQUE"), ("masked", "MASK")):
        asset = {
            "asset": {"version": "2.0"},
            "scene": 0,
            "scenes": [{"nodes": [0]}],
            "nodes": [{"mesh": 0}],
            "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "material": 0}]}],
            "materials": [{
                "pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.03, 0.02, 0.25], "metallicFactor": 0},
                "doubleSided": True,
                "alphaMode": alpha,
            }],
            "buffers": [{"uri": "triangle.bin", "byteLength": 72}],
            "bufferViews": [
                {"buffer": 0, "byteOffset": offset, "byteLength": 36} for offset in (0, 36)
            ],
            "accessors": [
                {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                 "min": [-1, -1, 0], "max": [1, 1, 0]},
                {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
            ],
        }
        (directory / f"{name}.gltf").write_text(json.dumps(asset))


def run_view(viewer, directory, asset, mode, frames=1):
    prefix = directory / f"{asset}-{mode or 'default'}"
    cmd = [
        viewer, "--headless", "--width", "128", "--height", "96",
        "--input", directory / f"{asset}.gltf", "--pbr-environment-source", "static",
        "--capture-camera-yaw", "0", "--capture-camera-pitch", "0", "--profile-output", prefix,
    ]
    if mode:
        cmd += ["--anti-aliasing", mode]
    if frames > 1:
        cmd += ["--headless-render-only", "--frames", str(frames)]
    else:
        cmd += ["--output", prefix.with_suffix(".png")]
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    prefix.with_suffix(".log").write_text(result.stdout)
    if result.returncode or "vulkan validation error" in result.stdout.lower():
        raise RuntimeError(result.stdout)
    assert "activated generation 1" in result.stdout and "1 triangles" in result.stdout, result.stdout
    with prefix.with_suffix(".metrics.csv").open() as stream:
        rows = list(csv.DictReader(stream))
    metrics = {}
    for row in rows:
        name = f"{row['category']}.{row['name']}"
        series = metrics.setdefault(name, {})
        frame = int(row["frame_index"])
        assert frame not in series, (name, frame)
        series[frame] = float(row["value"])
    return prefix, metrics


def main(viewer, directory):
    directory.mkdir(parents=True, exist_ok=True)
    write_triangles(directory)
    frames = 8
    _, active = run_view(viewer, directory, "opaque", "taa", frames)
    for index in range(frames):
        assert active["aa.requested"][index] == active["aa.effective"][index] == 1
        assert active["aa.bypass_reason"][index] == 0
        assert active["taa.history_age"][index] == index
        assert active["taa.reset_reason"][index] == (1 if index == 0 else 0)
    assert len(set(zip(active["taa.jitter_x"].values(), active["taa.jitter_y"].values()))) == frames

    pixels = {}
    for asset, mode, requested, reason in (
        ("opaque", None, 0, 0), ("opaque", "off", 0, 0),
        ("masked", "taa", 1, 4), ("masked", "off", 0, 0),
    ):
        prefix, metrics = run_view(viewer, directory, asset, mode)
        assert metrics["aa.requested"] == {0: requested}
        assert metrics["aa.effective"] == {0: 0} and metrics["aa.bypass_reason"] == {0: reason}
        assert "taa.jitter_x" not in metrics and "taa.history_age" not in metrics
        pixels[asset, mode] = prefix.with_suffix(".png").read_bytes()
    assert pixels["opaque", None] == pixels["opaque", "off"]
    assert pixels["masked", "taa"] == pixels["masked", "off"]
    assert pixels["opaque", "off"] != pixels["masked", "off"]
    print("8 opaque submissions, history ages 0..7; headless Auto/Off and masked TAA/Off PNGs match")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
