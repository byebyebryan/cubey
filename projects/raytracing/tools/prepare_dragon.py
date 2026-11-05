# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy==2.5.4"]
# ///
"""Prepare the pinned Stanford Dragon without changing triangle connectivity."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import tarfile
import urllib.request

import numpy as np

URL = "https://graphics.stanford.edu/pub/3Dscanrep/dragon/dragon_recon.tar.gz"
ARCHIVE_SHA = "74ac1d90989c9b1732edee82d57e9ce71452144cf4355f108d8c9c616d28d02f"
MEMBER = "dragon_recon/dragon_vrip.ply"
SOURCE_SHA = "fea87ff48f2aba22fb53e7b67c3ff3f7b8c2a3b3a0653af62c48bba67c6d5744"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def prepare(data):
    if sha(data) != SOURCE_SHA:
        raise ValueError("Stanford source checksum mismatch")
    end = data.index(b"end_header\n") + len(b"end_header\n")
    header = data[:end].decode()
    if "format ascii 1.0" not in header:
        raise ValueError("Unexpected PLY encoding")
    counts = {line.split()[1]: int(line.split()[2]) for line in header.splitlines() if line.startswith("element ")}
    if counts != {"vertex": 437645, "face": 871414}:
        raise ValueError("Unexpected pinned geometry counts")
    source = io.BytesIO(data[end:])
    positions = np.loadtxt(source, max_rows=counts["vertex"], dtype=np.float64)
    faces = np.loadtxt(source, max_rows=counts["face"], dtype=np.int64)
    if positions.shape != (counts["vertex"], 3) or faces.shape != (counts["face"], 4):
        raise ValueError("Unexpected geometry shape")
    if not np.all(faces[:, 0] == 3) or faces[:, 1:].min() < 0 or faces[:, 1:].max() >= len(positions):
        raise ValueError("Unexpected triangle indices")
    indices = np.asarray(faces[:, 1:], dtype="<u4")
    low, high = positions.min(axis=0), positions.max(axis=0)
    center, scale = (low + high) / 2, 1 / (high - low).max()
    normalized = (positions - center) * scale
    p = normalized.astype("<f4")
    triangles = p[indices].astype(np.float64)
    face_normals = np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0])
    normals = np.zeros_like(positions)
    for corner in range(3):
        np.add.at(normals, indices[:, corner], face_normals)
    lengths = np.linalg.norm(normals, axis=1)
    fallback = lengths < 1e-30
    normals[fallback] = [0, 1, 0]
    normals /= np.linalg.norm(normals, axis=1)[:, None]
    n = normals.astype("<f4")
    if not np.isfinite(p).all() or not np.isfinite(n).all():
        raise ValueError("Nonfinite prepared geometry")
    pieces = [p.tobytes(), n.tobytes(), indices.tobytes()]
    offsets = [0, len(pieces[0]), len(pieces[0]) + len(pieces[1])]
    binary = b"".join(pieces)
    document = {
        "asset": {"version": "2.0", "generator": "Cubey pinned Stanford Dragon preparation"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2, "material": 0}]}],
        "materials": [{"name": "geometry reference", "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0, "roughnessFactor": .4}}],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [{"buffer": 0, "byteOffset": o, "byteLength": len(d)} for o, d in zip(offsets, pieces)],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(p), "type": "VEC3", "min": p.min(axis=0).tolist(), "max": p.max(axis=0).tolist()},
            {"bufferView": 1, "componentType": 5126, "count": len(n), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5125, "count": indices.size, "type": "SCALAR"},
        ],
    }
    j = json.dumps(document, separators=(",", ":")).encode()
    j += b" " * (-len(j) % 4)
    binary += b"\0" * (-len(binary) % 4)
    glb = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(j) + 8 + len(binary))
    glb += struct.pack("<II", len(j), 0x4E4F534A) + j
    glb += struct.pack("<II", len(binary), 0x004E4942) + binary
    report = {"vertices": len(p), "triangles": len(indices), "source_bounds": [low.tolist(), high.tolist()],
              "normalization_center": center.tolist(), "normalization_scale": float(scale),
              "float32_position_error_max": float(np.abs(normalized - p).max()),
              "normal_policy": "area-weighted shared-vertex normals; no welding or topology changes",
              "fallback_normals": int(fallback.sum()), "zero_area_triangles": int((np.linalg.norm(face_normals, axis=1) < 1e-30).sum()),
              "index_connectivity_sha256": sha(indices.tobytes()), "surface_operation": "uniform normalization and float32 encoding only"}
    return glb, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("cache/raytracing/stanford-dragon"))
    parser.add_argument("--archive", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.archive or args.output / "dragon_recon.tar.gz"
    if not archive.exists():
        with urllib.request.urlopen(URL, timeout=60) as response:
            data = response.read(32 * 1024 * 1024 + 1)
        if sha(data) != ARCHIVE_SHA:
            raise ValueError("Downloaded archive checksum mismatch")
        archive.write_bytes(data)
    if sha(archive.read_bytes()) != ARCHIVE_SHA:
        raise ValueError("Archive checksum mismatch")
    with tarfile.open(archive) as source:
        data = source.extractfile(MEMBER).read()
        readme = source.extractfile("dragon_recon/README").read()
    glb, report = prepare(data)
    (args.output / "dragon.glb").write_bytes(glb)
    (args.output / "SOURCE_README.txt").write_bytes(readme)
    report.update({"format": "cubey.raytracing.dragon-preparation", "version": 1, "source_url": URL,
                   "terms_url": "https://graphics.stanford.edu/data/3Dscanrep/", "attribution": "Stanford Computer Graphics Laboratory",
                   "archive_sha256": ARCHIVE_SHA, "member": MEMBER, "source_sha256": SOURCE_SHA,
                   "glb_sha256": sha(glb), "glb_bytes": len(glb)})
    (args.output / "manifest.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
