# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy==2.4.6"]
# ///
"""Analyze Q1 private GPU controls, analytical calibration and physical limits.

Successful execution means the experiment was analyzed, not that its candidate
passed promotion. The receipt keeps arithmetic acceptance and policy decision
as separate fields. No dragon/default renderer qualification is implied.
"""

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import optical_normal_reference as ref


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def probe_data(directory):
    receipt = json.loads((directory / "receipt.json").read_text())
    assert receipt["passed"], "GPU execution failed"
    probe = directory / "probe.bin"
    assert digest(probe) == receipt["probe_sha256"], "changed GPU fixture output"
    assert digest(directory / "build/raytracing") == receipt["binary_sha256"]
    assert (
        digest(directory / "build/shaders/trace.rgen.spv") == receipt["shader_sha256"]
    )
    width, height = receipt["logical_extent"]
    raw = np.fromfile(probe, dtype="<f4").reshape(height, width, 16, 4).astype(float)
    return raw, receipt


def calibration():
    incoming = -ref.tilted(30)
    geom = np.array([0.0, 0.0, 1.0])
    b = ref.interface(incoming, geom, geom, 1.5)
    transmitted_angle = np.rad2deg(np.arccos(-b["transmitted"][2]))
    expected_angle = np.rad2deg(np.arcsin(np.sin(np.deg2rad(30)) / 1.5))
    normal = ref.interface([0, 0, -1], geom, geom, 1.5)
    matched = ref.interface(incoming, geom, ref.tilted(-30), 1)
    f0_error = abs(float(normal["fresnel"]) - 0.04)
    snell_error = abs(float(transmitted_angle) - expected_angle)
    assert f0_error < 1e-14 and snell_error < 1e-12
    assert np.array_equal(matched["transmitted"], incoming)
    critical = np.rad2deg(np.arcsin(1 / 1.5))
    below = ref.interface(-ref.tilted(critical - 1e-6), geom, geom, 1 / 1.5)
    above = ref.interface(-ref.tilted(critical + 1e-6), geom, geom, 1 / 1.5)
    assert not below["tir"] and above["tir"]
    return {
        "passed": True,
        "normal_incidence_fresnel": float(normal["fresnel"]),
        "snell_transmission_angle_degrees": float(transmitted_angle),
        "expected_transmission_angle_degrees": float(expected_angle),
        "critical_angle_degrees": float(critical),
        "index_match_exact": True,
        "direction_vector_tolerance": 1e-5,
        "fresnel_absolute_tolerance": 1e-5,
        "critical_discriminant_band": 1e-5,
        "tolerance_source": "same 1e-5 direction/Fresnel and critical band as existing dielectric qualifier; actual float operands are audited",
    }


def score_interface(raw):
    d, g, s, eta = raw[..., 0, :3], raw[..., 1, :3], raw[..., 2, :3], raw[..., 0, 3]
    expected = ref.interface(d, g, s, eta)
    reason = raw[..., 3, 3].astype(np.uint32)
    # Separate normal-field normalization from arithmetic at the recorded
    # float operands. Renormalizing the selected GPU normal in double precision
    # would change the input near the critical angle.
    arithmetic = ref.about(d, raw[..., 3, :3], eta)
    c = np.sum(-d * raw[..., 3, :3], axis=-1)
    discriminant = c * c + eta * eta - 1
    critical_fixture = raw[..., 11, 2] == 3
    regular = ~critical_fixture & (np.abs(discriminant) > 1e-5)
    reflection_error = np.linalg.norm(
        raw[..., 4, :3] - arithmetic["reflected"], axis=-1
    )
    transmission_error = np.linalg.norm(
        raw[..., 5, :3] - arithmetic["transmitted"], axis=-1
    )
    active_t = raw[..., 4, 3] < 1
    active_r = raw[..., 4, 3] > 0
    f_error = np.abs(raw[..., 4, 3] - arithmetic["fresnel"])
    disagreements = reason != expected["reason"]
    matched = eta == 1
    equal_normal = np.all(s == g, axis=-1)
    selected_finite = bool(np.isfinite(raw[..., 3:, :]).all())
    wrong_r = int(np.count_nonzero(active_r & (raw[..., 10, 0] <= 0)))
    wrong_t = int(np.count_nonzero(active_t & (raw[..., 10, 1] >= 0)))
    max_r = float(np.max(reflection_error[regular & active_r]))
    max_t = float(np.max(transmission_error[regular & active_t]))
    max_f = float(np.max(f_error[regular]))
    index_exact = bool(np.array_equal(raw[..., 5, :3][matched], d[matched]))
    equal_exact = bool(
        np.array_equal(raw[..., 4, :][equal_normal], raw[..., 8, :][equal_normal])
        and np.array_equal(raw[..., 5, :][equal_normal], raw[..., 9, :][equal_normal])
    )
    equal_regular = equal_normal & regular
    equal_r_error = float(
        np.max(np.abs(raw[..., 4, :] - raw[..., 8, :])[equal_regular])
    )
    equal_t_error = float(
        np.max(np.abs(raw[..., 5, :] - raw[..., 9, :])[equal_regular])
    )
    equal_critical_t_error = float(
        np.max(np.abs(raw[..., 5, :] - raw[..., 9, :])[equal_normal & ~regular])
    )
    equal_gate = max(equal_r_error, equal_t_error) <= 1e-5
    passed = (
        selected_finite
        and wrong_r == wrong_t == 0
        and max(max_r, max_t, max_f) <= 1e-5
        and not np.any(disagreements & ~critical_fixture)
        and index_exact
        and equal_gate
    )
    seams = []
    for row in range(64, 96):
        r_jumps = ref.angular_error(raw[row, 1:, 4, :3], raw[row, :-1, 4, :3])
        crossing = np.flatnonzero(reason[row, 1:] != reason[row, :-1])
        if not len(crossing):
            continue
        x = int(crossing[np.argmax(r_jumps[crossing])])
        seams.append(
            {
                "row": row,
                "columns": [x, x + 1],
                "eta": float(eta[row, x]),
                "normal_tilt_degrees": float(raw[row, x, 11, 1]),
                "incidence_angles_degrees": raw[row, x : x + 2, 11, 0].tolist(),
                "input_angle_step_degrees": float(
                    ref.angular_error(d[row, x], d[row, x + 1])
                ),
                "reflection_jump_degrees": float(r_jumps[x]),
                "transmission_jump_degrees": float(
                    ref.angular_error(raw[row, x, 5, :3], raw[row, x + 1, 5, :3])
                )
                if active_t[row, x] and active_t[row, x + 1]
                else None,
                "fresnel_before_after": raw[row, x : x + 2, 4, 3].tolist(),
                "reasons_before_after": reason[row, x : x + 2].tolist(),
            }
        )
    reverse_r = ref.angular_error(raw[..., 6, :3], -d)
    reverse_t = ref.angular_error(raw[..., 7, :3], -d)
    fixed_r = ref.angular_error(raw[..., 12, :3], -d)
    fixed_t = ref.angular_error(raw[..., 13, :3], -d)
    reason_counts = {
        str(int(k)): int(v) for k, v in zip(*np.unique(reason, return_counts=True))
    }
    mismatches = np.argwhere(disagreements)
    result = {
        "arithmetic_passed": bool(passed),
        "observations": int(eta.size),
        "selected_outputs_finite": selected_finite,
        "invalid_raw_input_observations": int(
            np.count_nonzero(~np.isfinite(s).all(axis=-1))
        ),
        "regular_observations": int(np.count_nonzero(regular)),
        "critical_fixture_observations": int(np.count_nonzero(critical_fixture)),
        "maximum_regular_reflection_vector_error": max_r,
        "maximum_regular_transmission_vector_error": max_t,
        "maximum_regular_fresnel_absolute_error": max_f,
        "wrong_geometric_reflections": wrong_r,
        "wrong_geometric_transmissions": wrong_t,
        "index_match_observations": int(np.count_nonzero(matched)),
        "index_match_bit_exact": index_exact,
        "equal_normal_observations": int(np.count_nonzero(equal_normal)),
        "equal_normal_bit_exact": equal_exact,
        "equal_normal_regular_max_reflection_and_fresnel_component_error": equal_r_error,
        "equal_normal_regular_max_transmission_component_error": equal_t_error,
        "equal_normal_critical_max_transmission_component_difference": equal_critical_t_error,
        "equal_normal_float_tolerance_passed": equal_gate,
        "whole_interface_reason_counts": reason_counts,
        "policy_reason_mismatches": int(np.count_nonzero(disagreements)),
        "noncritical_fixture_reason_mismatches": int(
            np.count_nonzero(disagreements & ~critical_fixture)
        ),
        "critical_reason_mismatch_locations": mismatches.tolist(),
        "reverse_selected_reflection_max_degrees": float(
            np.max(reverse_r[regular & active_r])
        ),
        "reverse_selected_transmission_max_degrees": float(
            np.max(reverse_t[regular & active_t])
        ),
        "fixed_interface_reverse_reflection_max_degrees": float(
            np.max(fixed_r[regular & active_r])
        ),
        "fixed_interface_reverse_transmission_max_degrees": float(
            np.max(fixed_t[regular & active_t])
        ),
        "reduced_radiance_local_furnace_max_error": float(
            np.max(np.abs(raw[..., 14, 0] - 1))
        ),
        "furnace_scope": "single-interface reduced-radiance algebra, not a complete-body furnace or flux conservation test",
        "seams": seams,
    }
    assert passed, json.dumps(result, indent=2)
    return result


def sphere_control(latitude):
    longitude = latitude * 2
    theta, phi = np.meshgrid(
        np.linspace(0, np.pi, latitude + 1),
        np.arange(longitude) * 2 * np.pi / longitude,
        indexing="ij",
    )
    p = np.stack(
        [np.sin(theta) * np.cos(phi), np.cos(theta), np.sin(theta) * np.sin(phi)],
        axis=-1,
    )
    quads = np.stack(
        [p[:-1], p[1:], np.roll(p[1:], -1, axis=1), np.roll(p[:-1], -1, axis=1)],
        axis=-2,
    )
    triangles = np.concatenate(
        [
            quads[..., [0, 1, 2], :].reshape(-1, 3, 3),
            quads[..., [0, 2, 3], :].reshape(-1, 3, 3),
        ]
    )
    cross = np.cross(
        triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0]
    )
    triangles = triangles[np.linalg.norm(cross, axis=-1) > 1e-12]
    cross = np.cross(
        triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0]
    )
    center = np.mean(triangles, axis=1)
    ng = ref.unit(cross)
    ng = np.where((np.sum(ng * center, axis=-1) < 0)[:, None], -ng, ng)
    eye = np.array([0.0, 0.0, 3.0])
    incoming = ref.unit(center - eye)
    front = np.sum(-incoming * ng, axis=-1) > 0
    center, ng, incoming = center[front], ng[front], incoming[front]
    ed = np.sum(incoming * eye, axis=-1)
    t = -ed - np.sqrt(np.maximum(0, ed * ed - (np.dot(eye, eye) - 1)))
    analytic_normal = ref.unit(eye + t[:, None] * incoming)
    control = ref.about(incoming, analytic_normal, 1.5)
    geometric = ref.interface(incoming, ng, center, 1.5, False)
    guarded = ref.interface(incoming, ng, center, 1.5, True)
    interior = np.sum(-incoming * analytic_normal, axis=-1) > 0.25
    summary = {
        "latitude": latitude,
        "longitude": longitude,
        "triangles": len(triangles),
        "front_face_centroids": len(center),
        "interior_comparisons": int(np.count_nonzero(interior)),
        "all_front_fallbacks": int(np.count_nonzero(guarded["reason"])),
        "scope": "first entering interface; camera (0,0,3), unit sphere; centroid rays; analytic hit and polygon hit positions differ; excludes silhouette score and exit/TIR qualification",
    }
    for name, value in [("geometric", geometric), ("guarded", guarded)]:
        for branch in ["reflected", "transmitted"]:
            error = ref.angular_error(value[branch], control[branch])[interior]
            summary[name + "_" + branch + "_median_degrees"] = float(np.median(error))
            summary[name + "_" + branch + "_p99_degrees"] = float(
                np.quantile(error, 0.99)
            )
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--interface", type=Path, required=True)
    parser.add_argument("--flux", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    calibration_result = calibration()
    raw, interface_receipt = probe_data(args.interface)
    interface_result = score_interface(raw)
    raw_flux, flux_receipt = probe_data(args.flux)
    assert np.isfinite(raw_flux).all()
    denominator = np.pi * np.sin(np.deg2rad(15)) ** 2 * np.cos(np.deg2rad(60))
    ratios = np.mean(raw_flux[..., 0, :], axis=(0, 1)) * 2 * np.pi / denominator
    gpu_flux = {
        "candidate_reflection": float(ratios[0]),
        "candidate_transmission": float(ratios[1]),
        "candidate_total": float(ratios[0] + ratios[1]),
        "geometric_reflection": float(ratios[2]),
        "geometric_transmission": float(ratios[3]),
        "geometric_total": float(ratios[2] + ratios[3]),
        "method": "actual GLSL; outgoing uniform cosine/phi quadrature 1024 x 4096; exterior beam at 60 degrees, radius 15 degrees; normal tilt 20 degrees; IOR 1.5",
    }
    crosscheck = []
    for order in [64, 128, 256]:
        for candidate in [False, True]:
            r = ref.flux_preimages(
                20, 60, beam_radius=15, order=order, candidate=candidate
            )
            r["candidate"] = candidate
            crosscheck.append(r)
    reference = crosscheck[-1]["total_ratio"]
    gpu_flux["preimage_reference"] = reference
    gpu_flux["independent_method_difference"] = abs(
        gpu_flux["candidate_total"] - reference
    )
    assert abs(gpu_flux["geometric_total"] - 1) < 0.005
    assert gpu_flux["independent_method_difference"] < 0.005
    curves = []
    for tilt in [2, 5, 10, 20]:
        for angle in [-80, -60, -30, 0, 30, 60, 80]:
            r = ref.flux_preimages(tilt, angle, order=128)
            curves.append(r)
    fine_witness = [ref.flux_preimages(20, 80, order=n) for n in [64, 128, 256]]
    spheres = [sphere_control(n) for n in [8, 16, 32]]
    result = {
        "experiment_analysis_complete": True,
        "decision": "reject v1 whole-interface fallback before dragon integration",
        "candidate_promotion_passed": False,
        "decision_basis": [
            "finite large discontinuity at a smooth normal field boundary",
            "nonconservative radiance operator independently reproduced in GLSL",
            "reselected normal policy fails reverse-path consistency",
        ],
        "calibration": calibration_result,
        "interface": interface_result,
        "gpu_flux": gpu_flux,
        "flux_crosscheck": crosscheck,
        "energy_curves": curves,
        "fine_grazing_witness": fine_witness,
        "spheres": spheres,
        "scope": "stage-1 diagnostic decision; no smooth dragon capture, native selector, Q2/Q3 or default promotion",
        "interface_probe": interface_receipt,
        "flux_probe": flux_receipt,
        "source_sha256": {
            str(p): digest(p) for p in [Path(__file__), Path(ref.__file__)]
        },
    }
    (out / "analysis.json").write_text(
        json.dumps(result, indent=2, allow_nan=False) + "\n"
    )
    print(
        json.dumps(
            {
                k: result[k]
                for k in [
                    "decision",
                    "candidate_promotion_passed",
                    "gpu_flux",
                    "spheres",
                ]
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    with np.errstate(invalid="ignore", divide="ignore"):
        main()
