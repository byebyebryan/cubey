#!/usr/bin/env python3
"""Independent scalar/cell examples for the continuous-editing design review.

No renderer is invoked. These checks establish small counterexamples and source
capacity accounting, not GPU timing, a general root oracle, or editor acceptance.
"""
import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path
import random
import re
import struct
import subprocess


def smooth_min(a, b, k):
    h = max(k - abs(a - b), 0.0) / k
    return min(a, b) - h * h * k * 0.25


def smooth_max(a, b, k):
    return -smooth_min(-a, -b, k)


def half(x):
    return struct.unpack("e", struct.pack("e", x))[0]


def sphere(p, c, r):
    return math.dist(p, c) - r


def box(p, c, extents):
    q = [abs(x - y) - e for x, y, e in zip(p, c, extents)]
    return math.sqrt(sum(max(x, 0) ** 2 for x in q)) + min(max(q), 0)


def count_tree(node):
    if node is None:
        return 0, 0
    children = node.get("children")
    if children is None:
        return 1, 1
    counts = [count_tree(c) for c in children]
    return (sum(x[0] for x in counts),
            sum(x[1] for x in counts) + (0 if node["type"] == "group" else len(children) - 1))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    rng = random.Random(20261029)
    cases = {}

    # A local brush's exact scalar distance affects values outside its solid AABB.
    p = (0.20, 0, 0)
    a = sphere(p, (0, 0, 0), .1)
    b = sphere(p, (.30, 0, 0), .05)
    assert min(a, b) < a and p[0] < .25
    p2 = (.10, 0, 0)
    a2 = sphere(p2, (0, 0, 0), .3)
    b2 = sphere(p2, (0, 0, 0), .05)
    assert max(a2, -b2) > a2 and p2[0] > .05
    cases["brush_aabb_is_not_exact_distance_influence"] = {
        "add": {"point": p, "base": a, "edited": min(a, b), "brush_x_bounds": [.25, .35]},
        "cut": {"point": p2, "base": a2, "edited": max(a2, -b2), "brush_x_bounds": [-.05, .05]},
        "meaning": "Exact scalar-value invalidation needs more than primitive solid bounds. A bounded-band cache needs its own contract."
    }

    # Interior of a large box; an analytic subgrid cutter makes a cavity.
    c = (.5, .5, .5)
    corners = list(itertools.product((0., 1.), repeat=3))
    values = [max(box(p, c, (2., 2., 2.)), -sphere(p, c, .35)) for p in corners]
    live = max(box(c, c, (2., 2., 2.)), -sphere(c, c, .35))
    baked = sum(half(v) for v in values) / 8
    assert live > 0 and baked < 0 and all(v < 0 for v in values)
    cases["live_to_baked_subgrid_cavity"] = {
        "units": "sample spacing h = 1; independent local cell, not native Cubey pixels",
        "cutter_radius_h": .35, "analytic_center_value_h": live,
        "eight_corner_values_h": values, "r16_trilinear_center_value_h": baked,
        "meaning": "A true pending cut can disappear when even a fresh canonical bake has no sampled sign change. Retiring the tail can change visible topology."
    }

    # Preserve grouping for polynomial smooth Boolean operators.
    left = smooth_min(smooth_min(0., 0., .1), .05, .1)
    right = smooth_min(0., smooth_min(0., .05, .1), .1)
    assert abs(left - right) > .001
    cases["smooth_grouping"] = {"left": left, "right": right, "difference": abs(left - right)}

    # Hard CSG on fixed sample knots is a useful control: selection + a monotone
    # quantizer commutes. Do not claim that every incremental bake must drift.
    hard_differences = 0
    smooth_differences = 0
    smooth_max_error = 0.
    for _ in range(2000):
        exact = rng.uniform(-.5, .5)
        baked_value = half(exact)
        for i in range(16):
            b = rng.uniform(-.5, .5)
            op = min if i % 2 == 0 else max
            exact = op(exact, b)
            baked_value = half(op(baked_value, b))
        hard_differences += half(exact) != baked_value
        exact = rng.uniform(-.1, .1)
        baked_value = half(exact)
        for i in range(16):
            b = exact + rng.uniform(-.04, .04)
            op = smooth_min if i % 2 == 0 else smooth_max
            exact = op(exact, b, .05)
            baked_value = half(op(baked_value, b, .05))
        err = abs(half(exact) - baked_value)
        smooth_differences += err != 0
        smooth_max_error = max(smooth_max_error, err)
    assert hard_differences == 0 and smooth_differences > 0
    cases["repeated_quantization"] = {
        "chains": 2000, "operations_per_chain": 16,
        "hard_fixed_knot_differences": hard_differences,
        "smooth_fixed_knot_differences": smooth_differences,
        "smooth_max_abs_difference": smooth_max_error,
        "meaning": "Fixed-grid hard selection is a non-drifting control here. Smooth evaluation with intermediate quantization is not identical to one canonical quantization. No resampling or native GPU arithmetic is tested."
    }

    # Supported scalar hard / compact polynomial smooth composition does not
    # amplify an existing base-value error in real arithmetic with the same tail.
    epsilon = .001
    maximum_error = 0.
    for _ in range(20000):
        a = rng.uniform(-.5, .5)
        a_hat = a + rng.uniform(-epsilon, epsilon)
        for _ in range(8):
            b = rng.uniform(-.5, .5)
            k = rng.uniform(.01, .1)
            op = rng.randrange(4)
            if op == 0:
                a, a_hat = min(a, b), min(a_hat, b)
            elif op == 1:
                a, a_hat = max(a, -b), max(a_hat, -b)
            elif op == 2:
                a, a_hat = smooth_min(a, b, k), smooth_min(a_hat, b, k)
            else:
                a, a_hat = smooth_max(a, -b, k), smooth_max(a_hat, -b, k)
        maximum_error = max(maximum_error, abs(a - a_hat))
    assert maximum_error <= epsilon + 1e-12
    cases["bounded_base_error_scalar_control"] = {
        "chains": 20000, "tail_length": 8, "initial_error_limit": epsilon,
        "maximum_final_error": maximum_error,
        "meaning": "A useful pointwise bound, not proof of safe tracing: sampling/lookup certificates, zero topology, ray grazing, material ownership and rounding still matter."
    }

    # A worker finishes a prefix while additional edits have arrived.
    base, candidate, latest = 10, 12, 14
    pending_before = list(range(base + 1, latest + 1))
    suffix = [x for x in pending_before if x > candidate]
    assert suffix == [13, 14]
    cases["prefix_publication"] = {
        "base": base, "candidate": candidate, "latest": latest,
        "pending_before": pending_before, "pending_after_valid_publication": suffix,
        "clear_entire_tail_would_lose": suffix,
        "stale_branch_candidate_must_publish": False,
        "meaning": "An illustrative sequence model only. The native ticket/lifetime/branch protocol remains to implement and test."
    }

    # Queue conservation: a short tail is not a throughput solution.
    arrivals, service, seconds = 120, 80, 5
    backlog = (arrivals - service) * seconds
    assert backlog > 64
    cases["queue_conservation"] = {
        "scope": "Illustrative rates; no native service-rate prediction",
        "arrivals_per_second": arrivals, "service_per_second": service,
        "seconds": seconds, "backlog": backlog, "tail_cap": 64,
        "meaning": "A bounded renderer must reject or slow admission explicitly when durable edits arrive faster than cache service; dropping completed stroke segments is not coalescing."
    }

    header = (root / "projects/sdf/sdf_authored.h").read_text()
    max_leaves = int(re.search(r"authored_max_leaves\s*=\s*(\d+)", header)[1])
    max_instructions = int(re.search(r"authored_max_instructions\s*=\s*(\d+)", header)[1])
    contents = {}
    for name in ("sculpture-guardian-limestone", "survey-robot-fieldkit"):
        scene = json.loads((root / f"projects/sdf/scenes/{name}.json").read_text())
        leaves, instructions = count_tree(scene["root"])
        room = min(max_leaves - leaves, (max_instructions - instructions) // 2)
        contents[name] = {"leaves": leaves, "instructions": instructions,
                          "history_operations": len(scene["history"]), "root_append_leaf_room": room}
        if name.startswith("sculpture"):
            cumulative = 0
            prefix = None
            for i, step in enumerate(scene["history"], 1):
                cumulative += len(step.get("nodes", [step.get("node")]))
                if cumulative <= 400:
                    prefix = {"history_prefix": i, "leaves": cumulative, "label": step["label"]}
            contents[name]["proposed_private_prefix"] = prefix
            assert prefix["leaves"] + 600 <= max_leaves
    assert contents["sculpture-guardian-limestone"]["root_append_leaf_room"] == 13

    def identity(repo):
        return subprocess.check_output(["rtk", "proxy", "git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()

    sources = {}
    for rel in ("projects/sdf/sdf_authored.h", "projects/sdf/sdf_field.cpp", "projects/sdf/sdf_app.cpp",
                "projects/sdf/shaders/selected_trace.glsl", "projects/sdf/shaders/source_trace.glsl",
                "projects/sdf/shaders/trace.glsl", "include/cubey/vulkan/gpu_runtime.h"):
        sources[rel] = hashlib.sha256((root / rel).read_bytes()).hexdigest()
    original = Path("/home/bryan/code/projectR")
    original_sources = {}
    for rel in ("Archive/MarchingCubes/Scripts/ModificationJobSet_CommandQueue.cs",
                "Archive/MarchingCubes/Scripts/Workspace/Workspace_ModificationManagement.cs",
                "Archive/MarchingCubes/Scripts/ModificationJobSet.cs",
                "Assets/Scripts/BrickEngine/MainController.cs", "Assets/Scripts/BrickEngine/Workspace.cs"):
        original_sources[rel] = hashlib.sha256((original / rel).read_bytes()).hexdigest()
    result = {
        "scope": "Read-only source accounting and independent Python scalar/cell probes; no native renderer/editor/timing run",
        "seed": 20261029, "cubey_head": identity(root), "projectr_head": identity(original),
        "cases": cases, "capacities": {"max_leaves": max_leaves, "max_instructions": max_instructions,
                                         "content": contents},
        "sources": sources, "projectr_sources": original_sources,
        "probe_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "passed": True,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"passed": True, "cases": len(cases), "capacities": result["capacities"],
                      "repeated_quantization": cases["repeated_quantization"]}, indent=2))


if __name__ == "__main__":
    main()
