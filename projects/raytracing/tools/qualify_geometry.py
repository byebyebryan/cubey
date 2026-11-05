# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy==2.5.4"]
# ///
"""Check native RT observations against independent double-precision triangle rays."""
import argparse
import json
import os
from pathlib import Path
import subprocess

import numpy as np

WIDTH, HEIGHT = 128, 96
DEPTH_LIMIT = 2e-5
NORMAL_LIMIT_DEGREES = .1
BARY_LIMIT = 2e-5


def normalized(a):
    return a / np.linalg.norm(a, axis=-1, keepdims=True)


def fixture():
    triangle = np.array([[1, 1, 0], [-1, 1, 0], [0, -1, 0]], dtype=float)
    a = .65
    rotation = np.array([[np.cos(a), 0, np.sin(a)], [0, 1, 0], [-np.sin(a), 0, np.cos(a)]])
    other = (triangle * [.35, .55, .5]) @ rotation.T + [.8, .15, -.6]
    floor = np.array([[-2, -1.015, -2], [-2, -1.015, 2], [2, -1.015, 2], [2, -1.015, -2]])
    return np.array([triangle, other, floor[[0, 1, 2]], floor[[0, 2, 3]]]), np.array([0, 1, 2, 2])


def intersect(origins, directions, triangles):
    origins = np.broadcast_to(origins, directions.shape)
    distances, bary = [], []
    for triangle in triangles:
        a, b, c = triangle
        e1, e2 = b - a, c - a
        h = np.cross(directions, e2)
        determinant = h @ e1
        valid = np.abs(determinant) > 1e-12
        inverse = np.divide(1, determinant, out=np.zeros_like(determinant), where=valid)
        delta = origins - a
        u = np.sum(delta * h, axis=1) * inverse
        q = np.cross(delta, e1)
        v = np.sum(q * directions, axis=1) * inverse
        t = (q @ e2) * inverse
        valid &= (u >= 0) & (v >= 0) & (u + v <= 1) & (t >= 1e-7) & (t <= 100)
        distances.append(np.where(valid, t, np.inf))
        bary.append(np.column_stack([1 - u - v, u, v]))
    distances = np.array(distances).T
    chosen = distances.argmin(axis=1)
    return distances[np.arange(len(chosen)), chosen], chosen, np.array(bary)[chosen, np.arange(len(chosen))]


def verify(binary, output, yaw, pitch, distance):
    output.mkdir(parents=True, exist_ok=True)
    command = [str(binary), "--headless", "--frames", "1", "--scene", "fixture", "--yaw", str(yaw),
               "--pitch", str(pitch), "--distance", str(distance), "--width", str(WIDTH), "--height", str(HEIGHT),
               "--require-validation", "--query-output", str(output / "queries.bin"),
               "--output", str(output / "frame.png")]
    env = dict(os.environ, VK_LAYER_VALIDATE_SYNC="1")
    process = subprocess.run(command, env=env, capture_output=True, text=True, timeout=60)
    (output / "native.log").write_text(process.stdout + process.stderr)
    if process.returncode or "vulkan validation error" in process.stderr.lower() or "hazard detected" in process.stderr.lower():
        raise RuntimeError("Native geometry run failed: " + process.stdout + process.stderr)
    actual = np.fromfile(output / "queries.bin", dtype="<f4").reshape(-1, 4, 4).astype(float)
    eye = np.array([np.cos(pitch) * np.sin(yaw), np.sin(pitch), np.cos(pitch) * np.cos(yaw)]) * distance
    forward = normalized(-eye)
    right = normalized(np.cross(forward, [0, 1, 0]))
    up = np.cross(right, forward)
    x, y = np.meshgrid((np.arange(WIDTH) + .5) / WIDTH * 2 - 1, (np.arange(HEIGHT) + .5) / HEIGHT * 2 - 1)
    directions = normalized(forward + np.tan(np.pi / 8) * (right * x.reshape(-1, 1) * WIDTH / HEIGHT - up * y.reshape(-1, 1)))
    triangles, instances = fixture()
    depth, chosen, bary = intersect(eye, directions, triangles)
    hit = np.isfinite(depth)
    native_hit = actual[:, 0, 0] >= 0
    assert np.array_equal(hit, native_hit), f"hit/miss disagreement: {np.sum(hit != native_hit)}"
    assert np.array_equal(actual[hit, 0, 1], instances[chosen[hit]]), "instance IDs disagree"
    selected = hit & (bary.min(axis=1) > 1e-4)
    error = np.max(np.abs(depth[selected] - actual[selected, 0, 0]))
    assert error <= DEPTH_LIMIT, f"depth error {error}"
    bary_error = np.max(np.abs(bary[selected] - actual[selected, 3, :3]))
    assert bary_error <= BARY_LIMIT, f"barycentric error {bary_error}"
    geometric = normalized(np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0]))[chosen]
    geometric *= np.where(np.sum(geometric * directions, axis=1) < 0, 1, -1)[:, None]
    normal_error = np.degrees(np.arccos(np.clip(np.sum(normalized(actual[selected, 1, :3]) * geometric[selected], axis=1), -1, 1))).max()
    shading_error = np.degrees(np.arccos(np.clip(np.sum(normalized(actual[selected, 2, :3]) * geometric[selected], axis=1), -1, 1))).max()
    assert normal_error <= NORMAL_LIMIT_DEGREES and shading_error <= NORMAL_LIMIT_DEGREES
    points = eye + directions[hit] * depth[hit, None]
    normals = geometric[hit]
    light = np.array([np.cos(.8) * np.sin(-.8), np.sin(.8), np.cos(.8) * np.cos(-.8)])
    offsets = np.maximum(.00002, 2e-6 * np.abs(points).max(axis=1))
    shadow_depth, _, shadow_bary = intersect(points + normals * offsets[:, None], np.broadcast_to(light, points.shape), triangles)
    lit_side = normals @ light > 1e-4
    unambiguous = ~np.isfinite(shadow_depth) | (shadow_bary.min(axis=1) > 1e-4)
    scored = lit_side & unambiguous
    shadow_errors = int(np.sum((actual[hit, 0, 3][scored] == 0) != np.isfinite(shadow_depth[scored])))
    assert shadow_errors == 0, f"shadow disagreements: {shadow_errors}"
    return {"rays": len(hit), "hits": int(hit.sum()), "regular_hits": int(selected.sum()),
            "depth_error_max": float(error), "barycentric_error_max": float(bary_error),
            "geometric_normal_error_max_degrees": float(normal_error), "shading_normal_error_max_degrees": float(shading_error),
            "shadow_rays_scored": int(scored.sum()), "shadow_errors": shadow_errors}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/dev/projects/raytracing/raytracing"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    results = []
    for i, camera in enumerate([(.35, .22, 3.5), (-.6, .4, 3.0), (2.4, .15, 3.0), (1.5, .04, 3.0)]):
        results.append(verify(args.binary, args.output / str(i), *camera))
    report = {"format": "cubey.raytracing.geometry-qualification", "depth_tolerance": DEPTH_LIMIT,
              "barycentric_tolerance": BARY_LIMIT, "normal_tolerance_degrees": NORMAL_LIMIT_DEGREES,
              "cameras": results, "passed": True}
    (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
