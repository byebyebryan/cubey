#!/usr/bin/env python3
"""Frozen rain-off study around the unchanged released SynxFlow GPU solver."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

import numpy as np
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "outputs/fluid/native-rain-recession-v1-20261003-1ZMqTJ"
PROTOCOL = OUT / "protocol.json"
PROTOCOL_SHA = "6ac7fb333460d90e2dfa4f4eeb852b6ab65af6f466e3668ee23f1a1938a04462"
PYTHON = ROOT / "outputs/fluid/native-runoff-reuse-v1-20261002-trm3Ve/synxflow-setup/env/.venv/bin/python"
EPS = float(np.finfo(np.float32).eps)
D4 = ndimage.generate_binary_structure(2, 1)


def sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write(path: Path, value) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False)+"\n")


def load() -> dict:
    if sha(PROTOCOL) != PROTOCOL_SHA:
        raise ValueError("frozen recession protocol drift")
    return json.loads(PROTOCOL.read_text())


def reference(protocol):
    path = ROOT / "projects/fluid/fluid_25d/run_native_runoff_reuse_v1.py"
    if sha(path) != protocol["reference_helper_sha256"]:
        raise ValueError("sealed native helper changed")
    sys.path.insert(0, str(path.parent))
    import run_native_runoff_reuse_v1 as ref
    if ref.inspect_source_identity() != protocol["native_backend_identity"]:
        raise ValueError("native solver identity drift")
    for name, expected in protocol["installed_synxflow_payload_files"].items():
        if sha(Path(name)) != expected:
            raise ValueError(f"installed package drift: {name}")
    if ref.mountain_source_arrays()[3] != protocol["terrain_source_identity"]:
        raise ValueError("immutable mountain identity drift")
    if ref.fixed_depression_masks()[1] != protocol["depression_mask_identity"]:
        raise ValueError("fixed observation masks drift")
    return ref


def history_value(history, time_s: float) -> tuple[float, float, str]:
    """Nominal linear source rate/integral, not an applied native source ledger."""
    if not math.isfinite(time_s) or time_s < 0 or time_s > history[-1][0]:
        raise ValueError("history evaluation time is outside the schedule")
    cumulative = 0.0
    rate, phase = float(history[-1][1]), "Off" if history[-1][1] == 0 else "On"
    for (a, ra), (b, rb) in zip(history, history[1:]):
        length = max(0.0, min(time_s, b)-a)
        slope = (rb-ra)/(b-a)
        cumulative += ra*length + 0.5*slope*length*length
        if a <= time_s < b:
            rate = ra+slope*(time_s-a)
            phase = "Off" if rate == 0 else "Tapering" if rb < ra else "On"
    return float(rate), float(cumulative), phase


def validate_history(history, duration, initial):
    if not isinstance(history, list) or len(history) < 2:
        raise ValueError("rain history requires at least two rows")
    if any(not isinstance(row, list) or len(row) != 2 or
           any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) or v < 0 for v in row)
           for row in history):
        raise ValueError("invalid rain history row")
    if history[0][0] != 0 or history[-1][0] != duration or history[0][1] != initial:
        raise ValueError("rain history endpoint/initial rate mismatch")
    if any(b[0] <= a[0] for a, b in zip(history, history[1:])):
        raise ValueError("rain history times must strictly increase")


def space(protocol):
    size = sum(p.stat().st_size for p in OUT.rglob("*") if p.is_file())
    free = shutil.disk_usage(OUT).free
    if size > protocol["limits"]["study_disk_budget_bytes"] or free < protocol["limits"]["minimum_free_bytes"]:
        raise ValueError(f"study disk/free-space gate: {size}/{free} bytes")
    return {"study_bytes": size, "free_bytes": free}


def prepare(spec, protocol, ref):
    case = OUT / "cases" / spec["name"]
    case.parent.mkdir(exist_ok=True)
    case.mkdir(exist_ok=False)
    validate_history(spec["rainfall_history"], spec["duration_s"], spec["rain_rate_m_per_s"])
    if spec["family"] == "mountain-rain":
        shutil.copyfile(Path(protocol["baseline_case"]) / "DEM.asc", case / "DEM.asc")
        assert sha(case / "DEM.asc") == protocol["baseline_dem_sha256"]
    else:
        ref.write_ascii(case / "DEM.asc", np.zeros((spec["rows"], spec["cols"])), spec["dx_m"])
    write(case / "case-spec.json", spec)
    write(case / "case-protocol.json", {
        "schema": "cubey.fluid25d.native_rain_recession.v1.case-protocol", "case": spec,
        "parent_protocol_sha256": PROTOCOL_SHA,
        "input_dem_ascii_sha256": sha(case / "DEM.asc"),
        "terrain_source_identity": protocol["terrain_source_identity"] if spec["family"] == "mountain-rain" else None,
        "settings": {"rain_source_schedule": spec["rainfall_history"]},
        "expected_saved_times_s": list(range(0, spec["duration_s"]+1, spec["output_interval_s"])),
        "frozen_before_native_execution": True,
    })
    shutil.copyfile(Path(__file__), case / "runner-archived.py")
    write(case / "harness-provenance.json", {
        "repository_launch_source": str(Path(__file__).resolve()), "runner_sha256": sha(Path(__file__)),
        "archived_copy_sha256": sha(case / "runner-archived.py"), "archive_is_not_launch_source": True,
        "reference_helper_sha256": protocol["reference_helper_sha256"],
    })
    return case


def serialize(case, spec, protocol, ref):
    from synxflow.IO.InputModel import InputModel
    model = InputModel(dem_data=str(case / "DEM.asc"), case_folder=str(case / "native"))
    shape = (spec["rows"], spec["cols"])
    model.set_initial_condition("h0", np.zeros(shape, dtype=np.float32))
    model.set_initial_condition("hU0x", np.zeros(shape, dtype=np.float32))
    model.set_initial_condition("hU0y", np.zeros(shape, dtype=np.float32))
    model.set_boundary_condition([], outline_boundary=spec["boundary"])
    model.set_rainfall(rain_mask=0, rain_source=np.asarray(spec["rainfall_history"], dtype=np.float64))
    model.set_grid_parameter(manning=spec["manning_n"], sewer_sink=0.0, cumulative_depth=0.0,
        hydraulic_conductivity=0.0, capillary_head=0.0, water_content_diff=0.0)
    model.set_runtime([0, spec["duration_s"], spec["output_interval_s"], spec["duration_s"]])
    model.set_device_no([0])
    model.write_input_files()
    field = case / "native/input/field"
    hashes = ref._input_hashes(case / "native/input")
    schedule = np.loadtxt(field / "precipitation_source_all.dat", skiprows=1, ndmin=2)
    assert schedule.shape == (len(spec["rainfall_history"]), 2)
    assert np.array_equal(schedule[:, 0], np.asarray(spec["rainfall_history"])[:, 0])
    assert np.allclose(schedule[:, 1], np.asarray(spec["rainfall_history"])[:, 1], rtol=2e-8, atol=1e-14)
    changed = []
    if spec["family"] == "mountain-rain":
        baseline = protocol["baseline_native_input_sha256"]
        assert set(hashes) == set(baseline)
        changed = sorted(name for name in hashes if hashes[name] != baseline[name])
        assert changed == sorted(protocol["input_allowlist_vs_two_hour_baseline"]), changed
        if spec == protocol["cases_in_order"][1]:
            other = OUT / "cases" / protocol["cases_in_order"][0]["name"] / "native-preflight.json"
            prior = json.loads(other.read_text())["native_input_sha256"]
            assert set(hashes) == set(prior)
            assert sorted(name for name in hashes if hashes[name] != prior[name]) == protocol["input_allowlist_between_four_hour_cases"]
    for name in ("h", "sewer_sink", "cumulative_depth", "hydraulic_conductivity", "capillary_head", "water_content_diff", "precipitation_mask"):
        assert np.all(ref._element_values(field / f"{name}.dat") == 0)
    assert np.all(ref._element_records(field / "hU.dat")[1] == 0)
    assert np.allclose(ref._element_values(field / "manning.dat"), spec["manning_n"], rtol=0, atol=1e-7)
    assert (case / "native/input/times_setup.dat").read_text().split() == ["0", str(spec["duration_s"]), str(spec["output_interval_s"]), str(spec["duration_s"])]
    _, z = ref._grid_from_native_ids(field / "z.dat", spec["rows"], spec["cols"])
    if spec["family"] != "mountain-rain":
        assert np.all(z == 0)
        assert all(code == (2, 0, 0) for code in ref._serialized_boundary_codes(field / "h.dat"))
        assert all(code == (2, 2, 0) for code in ref._serialized_boundary_codes(field / "hU.dat"))
    audit = {"z_field_sha256": sha(field / "z.dat"), "z_element_count": int(z.size),
        "rain_source_sha256": sha(field / "precipitation_source_all.dat"),
        "serialized_rainfall_history": schedule.tolist(), "native_input_sha256": hashes,
        "changed_native_input_paths_vs_two_hour_baseline": changed,
        "declared_boundary": spec["boundary"], "zero_initial_depth_and_momentum": True,
        "all_other_source_sink_parameters_zero": True, "runtime": [0,spec["duration_s"],spec["output_interval_s"],spec["duration_s"]]}
    write(case / "native-preflight.json", audit)
    return audit


def child(case, protocol, ref):
    assert Path(os.path.abspath(sys.executable)) == PYTHON
    assert case.parent.resolve() == (OUT / "cases").resolve() and not case.is_symlink()
    spec = json.loads((case / "case-spec.json").read_text())
    assert spec in [protocol["control"], *protocol["cases_in_order"]]
    assert not (case / "native").exists()
    provenance = json.loads((case / "harness-provenance.json").read_text())
    assert provenance["runner_sha256"] == sha(Path(__file__)) == sha(case / "runner-archived.py")
    if spec["family"] == "mountain-rain":
        assert sha(case / "DEM.asc") == protocol["baseline_dem_sha256"]
    audit = serialize(case, spec, protocol, ref)
    from synxflow import __version__, flood
    assert __version__ == "1.0.1"
    started = time.monotonic()
    flood.run(str(case / "native"))
    elapsed = time.monotonic()-started
    after = ref._input_hashes(case / "native/input")
    assert after == audit["native_input_sha256"]
    write(case / "native-postrun.json", {"native_call_wall_s": elapsed,
        "input_sha256_after_run": after, "unchanged_inputs": True,
        "python": str(PYTHON), "synxflow_version": __version__})


def components(mask):
    labels, count = ndimage.label(mask, D4)
    for number, box in enumerate(ndimage.find_objects(labels, count), 1):
        if box is not None:
            yield box, labels[box] == number


def quiet_tracks(previous, candidates, timestamp, previous_time):
    tracks = []
    for cells in candidates:
        matches = [(track["start"], track["core"] & cells) for track in previous
                   if len(track["core"] & cells) >= 4]
        start, core = min(matches, key=lambda pair: pair[0]) if matches else (timestamp, cells)
        tracks.append({"start": start, "core": core, "support_s": timestamp-start})
    return tracks


def analyze(case, spec, protocol, ref):
    output = case / "native/output"
    expected = list(range(0, spec["duration_s"]+1, spec["output_interval_s"]))
    paths = ref.snapshot_paths(output)
    assert all(sorted(paths[field]) == expected for field in ("h", "hUx", "hUy"))
    log_path = output / "timestep_log.txt"
    log_summary = ref.parse_timestep_log(log_path, spec["duration_s"])
    log = np.loadtxt(log_path, ndmin=2)
    consumed = np.diff(np.r_[0.0, log[:, 0]])
    starts = np.r_[0.0, log[:-1, 0]]
    assert np.all(consumed >= 0)
    serialized_history = json.loads((case / "native-preflight.json").read_text())["serialized_rainfall_history"]
    # Approximate applied-source check from rounded native current-time logs;
    # use only the small closed control as a source-response reference.
    native_rates = [history_value(serialized_history, min(max(float(t),0),spec["duration_s"]))[0] for t in starts]
    integrated_logged = np.cumsum(np.asarray(native_rates)*consumed)
    taper_steps = [float(dt) for t, dt in zip(starts, consumed)
                   if any(a <= t < b and rb < ra for (a,ra),(b,rb) in zip(serialized_history,serialized_history[1:]))]
    quadrature_upper_depth = spec["rain_rate_m_per_s"]*max(taper_steps, default=0)
    _, bed = ref._grid_from_native_ids(case / "native/input/field/z.dat", spec["rows"], spec["cols"])
    bed = bed.astype(np.float32).astype(np.float64)
    mountain = spec["family"] == "mountain-rain"
    rois = ref.fixed_roi_masks(bed.shape) if mountain else {}
    depressions, identity = ref.fixed_depression_masks() if mountain else ({},None)
    if mountain:
        assert identity == protocol["depression_mask_identity"]
    masks = {**rois, **depressions}
    tracks = {name: [] for name in masks if name.startswith("label-")}
    longest = {name: 0 for name in tracks}
    core_counts = {name: 0 for name in tracks}
    rows, hashes, previous_volume, previous_time, longest_corridor, support = [], {}, None, 0, 0, 0
    previous_corridor = False
    area, cells = spec["dx_m"]**2, bed.size
    quantization_volume = cells*area*.5e-6
    control_max_error, control_tolerance_max = 0.0,0.0
    dry_growth_max = 0.0
    for timestamp in expected:
        fields = []
        for field in ("h", "hUx", "hUy"):
            path = paths[field][timestamp]
            header, values, valid = ref.read_ascii(path)
            assert values.shape == bed.shape and valid.all() and np.isfinite(values).all()
            assert header["cellsize"] == spec["dx_m"]
            hashes[path.name] = sha(path)
            if mountain and timestamp <= 7200:
                assert hashes[path.name] == protocol["baseline_raw_fields_sha256"][path.name], f"prefix drift: {path.name}"
            fields.append(values)
        h, qx, qy = fields
        assert np.all(h >= 0)
        assert not np.any((h == 0) & ((qx != 0) | (qy != 0))), f"dry nonzero momentum at{timestamp}"
        if timestamp == 0:
            assert not any(np.any(field != 0) for field in fields)
        speed = np.divide(np.hypot(qx,qy), h, out=np.zeros_like(h), where=h>0)
        assert np.isfinite(speed).all()
        rate, direct_depth, phase = history_value(spec["rainfall_history"], timestamp)
        volume = float(h.sum()*area)
        step_count = int(np.count_nonzero(log[:,0] <= timestamp))
        source_upper = direct_depth*cells*area + quantization_volume + quadrature_upper_depth*cells*area + 8*EPS*(step_count+4)*direct_depth*cells*area
        assert volume <= source_upper, f"storage above source/precision bound at{timestamp}"
        interval_steps = int(np.count_nonzero((log[:,0]>previous_time)&(log[:,0]<=timestamp)))
        dry_tolerance = None
        if previous_volume is not None and history_value(spec["rainfall_history"],previous_time)[0] == 0 and rate == 0:
            dry_tolerance = 2*quantization_volume+8*EPS*max(previous_volume,volume)*(interval_steps+4)
            growth = volume-previous_volume
            dry_growth_max = max(dry_growth_max,growth)
            assert growth <= dry_tolerance, f"unexplained dry-period storage increase at{timestamp}"
        material = h >= .01
        route = material & (h-direct_depth >= .01) & (speed >= .02)
        if mountain:
            route[:4,:]=False; route[-4:,:]=False; route[:,:4]=False; route[:,-4:]=False
        spans = [max((box[1].stop-box[1].start-1)*spec["dx_m"],(box[0].stop-box[0].start-1)*spec["dx_m"])
                 for box,_ in components(route)]
        span = max(spans,default=0)
        corridor = span >= 300
        support = support+timestamp-previous_time if corridor and previous_corridor else 0
        longest_corridor = max(longest_corridor,support)
        local, ponds = {}, {}
        for name, mask in masks.items():
            weights, velocities = h[mask], speed[mask]
            local[name] = {"stored_m3": float(weights.sum()*area), "own_nominal_rain_m3": float(mask.sum()*area*direct_depth),
                "net_storage_beyond_own_rain_m3": float(weights.sum()*area-mask.sum()*area*direct_depth),
                "max_depth_m": float(weights.max()), "water_weighted_speed_m_per_s": float(np.sum(weights*velocities)/weights.sum()) if weights.sum()>0 else None}
            if name not in tracks:
                continue
            candidates = []
            for box, part in components(mask & material):
                hh, uu = h[box][part], speed[box][part]
                if part.sum()<4 or np.ptp((bed+h)[box][part])>.05 or np.sum(hh*uu)/hh.sum()>.02:
                    continue
                zz, xx = np.nonzero(part)
                candidates.append(set(((zz+box[0].start)*bed.shape[1]+xx+box[1].start).tolist()))
            tracks[name] = quiet_tracks(tracks[name],candidates,timestamp,previous_time)
            best = max(tracks[name],key=lambda item:item["support_s"], default=None)
            if best and best["support_s"] > longest[name]:
                longest[name],core_counts[name] = best["support_s"],len(best["core"])
            ponds[name] = {"quiet_near_level_candidates": len(candidates),
                "persistent_same_cell_support_s": best["support_s"] if best else 0,
                "persistent_core_cells": len(best["core"]) if best else 0}
        if not mountain:
            index = int(np.searchsorted(log[:,0],timestamp+.0001,side="right"))-1
            applied = float(integrated_logged[index]) if index>=0 else 0.0
            tolerance = 2e-6 + spec["rain_rate_m_per_s"]*.001 + EPS*(step_count+4)*applied
            error = float(np.max(np.abs(h-applied)))
            assert error<=tolerance and np.ptp(h)<=tolerance and not np.any(qx) and not np.any(qy)
            control_max_error,control_tolerance_max=max(control_max_error,error),max(control_tolerance_max,tolerance)
        rows.append({"time_s": timestamp,"rain_phase":phase,"rain_mm_per_hour":rate*3600000,
            "cumulative_nominal_rain_depth_m":direct_depth,"stored_m3":volume,"max_depth_m":float(h.max()),
            "max_speed_m_per_s":float(speed.max()),"water_weighted_speed_m_per_s":float(np.sum(h*speed)/h.sum()) if h.sum()>0 else None,
            "material_water_weighted_speed_m_per_s":float(np.sum(h[material]*speed[material])/h[material].sum()) if material.any() else None,
            "material_cells":int(material.sum()),"moving_material_cells":int(np.count_nonzero(material & (speed>=.02))),
            "moving_concentrated_cells":int(route.sum()),"largest_corridor_span_m":span,
            "corridor_support_s":support,"fixed_storage":local,"ponds":ponds,
            "nominal_source_plus_precision_upper_bound_m3":source_upper,"dry_period_growth_tolerance_m3":dry_tolerance})
        previous_volume,previous_time,previous_corridor=volume,timestamp,corridor
    return {"all_saved_state_health_passed":True,"saved_states":len(expected),"observations":rows,
        "raw_asc_sha256":hashes,"timestep_log":log_summary,"source_taper_quadrature_upper_depth_m":quadrature_upper_depth,
        "longest_corridor_support_s":longest_corridor,"longest_persistent_quiet_patch_support_s":longest,
        "quiet_patch_core_cells_at_longest_support":core_counts,"maximum_dry_period_storage_increase_m3":dry_growth_max,
        "flat_control_max_depth_error_vs_log_euler_m":control_max_error if not mountain else None,
        "flat_control_depth_tolerance_max_m":control_tolerance_max if not mountain else None,
        "prefix_raw_exports_exact_through7200s":True if mountain else None,
        "limits":"Saved centers/rounded native timelog only; nominal integral is not certified applied rain or face-flow conservation; quiet-patch criterion is diagnostic, not calibrated lakes"}


def run_one(spec, protocol, ref):
    space(protocol)
    started=time.monotonic()
    case=prepare(spec,protocol,ref)
    before=sha(Path(__file__))
    command=[str(PYTHON),"-u",str(Path(__file__).resolve()),"--child",str(case)]
    cache=case/"cache"
    environment={**os.environ,"PYTHONDONTWRITEBYTECODE":"1","MPLCONFIGDIR":str(cache/"matplotlib"),"XDG_CACHE_HOME":str(cache/"xdg")}
    timed_out=False
    child_start=time.monotonic()
    try:
        process=subprocess.run(command,cwd=case,env=environment,capture_output=True,text=True,
            timeout=protocol["limits"]["native_timeout_s_per_case"])
        stdout,stderr,code=process.stdout,process.stderr,process.returncode
    except subprocess.TimeoutExpired as error:
        timed_out=True; code=None
        stdout=error.stdout.decode(errors="replace") if isinstance(error.stdout,bytes) else error.stdout or ""
        stderr=error.stderr.decode(errors="replace") if isinstance(error.stderr,bytes) else error.stderr or ""
    child_wall=time.monotonic()-child_start
    (case/"native.stdout").write_text(stdout); (case/"native.stderr").write_text(stderr)
    audit=json.loads((case/"native-preflight.json").read_text()) if (case/"native-preflight.json").exists() else None
    post=json.loads((case/"native-postrun.json").read_text()) if (case/"native-postrun.json").exists() else None
    result={"schema":"cubey.fluid25d.native_rain_recession.v1.case-result","case":spec,
        "status":"failed","command":command,"process_exit_code":code,"process_timeout":timed_out,
        "child_wall_s":child_wall,"native_call_wall_s":post["native_call_wall_s"] if post else None,
        "parent_protocol_sha256":PROTOCOL_SHA,"input_provenance":{"case_dem_ascii_sha256":sha(case/"DEM.asc"),
            "native_input_audit_before_solver":audit,"native_inputs_unchanged_during_run":post["unchanged_inputs"] if post else False},
        "harness_sha256_before":before,"harness_sha256_after":sha(Path(__file__)),
        "native_backend_identity":protocol["native_backend_identity"]}
    try:
        assert code==0 and not timed_out and "Simulation successfully finished!" in stdout
        assert before==sha(Path(__file__)) and post and audit
        assert ref._input_hashes(case/"native/input")==audit["native_input_sha256"]==post["input_sha256_after_run"]
        reference(protocol)
        result["metrics"]=analyze(case,spec,protocol,ref)
        result["space_after"]=space(protocol)
        result["status"]="healthy"
    except Exception as error:
        result["failure"]=f"{type(error).__name__}: {error}"
    result["end_to_end_wall_s"]=time.monotonic()-started
    write(case/"case-result.json",result)
    print(json.dumps({"case":spec["name"],"status":result["status"],"failure":result.get("failure"),
        "native_call_s":result["native_call_wall_s"],"case_wall_s":result["end_to_end_wall_s"]}),flush=True)
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--child",type=Path)
    parser.add_argument("--go",action="store_true",help="launch the three predeclared cases sequentially")
    args=parser.parse_args()
    protocol=load(); ref=reference(protocol)
    if args.child:
        child(args.child,protocol,ref); return
    assert args.go,"native execution requires explicit --go"
    assert not (OUT/"cases").exists(),"refusing to rerun or overwrite cases"
    results=[]
    for spec in [protocol["control"],*protocol["cases_in_order"]]:
        result=run_one(spec,protocol,ref); results.append({"case":spec["name"],"status":result["status"],
            "case_result":str(OUT/"cases"/spec["name"]/"case-result.json")})
        if result["status"]!="healthy":
            write(OUT/"phase-summary.json",{"status":"stopped","cases":results,"failure":result.get("failure")})
            raise SystemExit(1)
    left,right=[json.loads((OUT/"cases"/s["name"]/"native-preflight.json").read_text())["native_input_sha256"] for s in protocol["cases_in_order"]]
    assert sorted(name for name in left if left[name]!=right[name])==protocol["input_allowlist_between_four_hour_cases"]
    write(OUT/"phase-summary.json",{"status":"complete","cases":results,"only_rain_schedule_differs_between_mountain_cases":True})


if __name__=="__main__":
    main()
