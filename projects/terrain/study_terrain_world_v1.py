#!/usr/bin/env python3
"""Bounded setup-time Terrain Diffusion world study; never starts a solver or GUI.

The optional producer is isolated in a subprocess with a 900 second wall bound.
No downloads, package installation, producer edits or source/capture overwrite.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "projects/terrain/tools"))
import terrain_world_coverage as coverage

SOURCE = ROOT / "cache/terrain/sources/v1/landscape-variations/temperate-mountain-valley/heightfield.json"
CAPTURE = ROOT / "cache/terrain/water/v1/mountain-wide-rain-fed-1913-32m-study/water-capture.json"
REFERENCE = ROOT / "cache/terrain/tooling/v1/terrain-diffusion-src"
ENV = ROOT / "cache/terrain/tooling/v1/terrain-diffusion-venv/bin/python"
DATA = ROOT / "cache/terrain/tooling/v1/terrain-diffusion-data"
LIMIT_SECONDS = 900


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""): h.update(block)
    return h.hexdigest()


def write(path, doc):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(doc, indent=2, sort_keys=True, allow_nan=False) + "\n")
    temporary.replace(path)


def protected():
    paths = [SOURCE, SOURCE.parent / "elevation.f32", SOURCE.parent / "surface-fields.json",
             SOURCE.parent / "climate.f32"]
    paths.extend(p for p in CAPTURE.parent.iterdir() if p.is_file())
    return {str(p.relative_to(ROOT)): digest(p) for p in sorted(paths)}


def owned(out):
    out = Path(out).resolve()
    if out.parent != ROOT / "outputs/terrain" or not out.name.startswith("world-sites-"):
        raise ValueError("study output must be a fresh outputs/terrain/world-sites-* leaf")
    return out


def freeze(out):
    if out.exists(): raise ValueError("fresh output required")
    source = json.loads(SOURCE.read_text())
    revision = subprocess.check_output(["rtk", "proxy", "git", "-C", str(REFERENCE),
                                       "rev-parse", "HEAD"], text=True).strip()
    if revision != source["source"]["code_revision"]: raise ValueError("producer pin mismatch")
    world = coverage.world_recipe(source)
    out.mkdir()
    write(out / "protocol.json", {"schema": "cubey.terrain.world-study.v1", "world": world,
          "protected": protected(), "code_revision": revision, "wall_limit_seconds": LIMIT_SECONDS,
          "sites": [coverage.site_plan(world, "mountain-original", (11520, 19200))],
          "rendering": "headless-vulkan-only", "water": "existing-state-only-no-new-solver",
          "default_promotion": False, "push": False})


def check(out):
    doc = json.loads((out / "protocol.json").read_text())
    if doc["protected"] != protected(): raise ValueError("protected source/capture changed")
    return doc


def pipeline(doc):
    import torch
    import terrain_diffusion_bake as bake
    from terrain_diffusion.inference.world_pipeline import WorldPipeline
    source = json.loads(SOURCE.read_text())
    snapshot = Path(source["source"]["model_snapshot"])
    if not snapshot.is_dir(): raise ValueError("pinned model must already be installed")
    if not torch.cuda.is_available(): raise ValueError("CUDA producer unavailable")
    # Match the accepted exporter, including arithmetic precision, not just seed.
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    torch.backends.cudnn.benchmark = False
    torch.set_float32_matmul_precision("highest")
    torch.use_deterministic_algorithms(True, warn_only=True)
    os.chdir(DATA)
    bake._seed_process_rngs(torch, doc["world"]["seed"])
    torch.cuda.reset_peak_memory_stats()
    world = WorldPipeline.from_pretrained(str(snapshot), seed=doc["world"]["seed"],
        latents_batch_size=1, torch_compile=False, dtype=None,
        caching_strategy="direct", cache_limit=None, log_mode="info")
    world.to("cuda"); world.bind()
    if world.native_resolution != 30: raise ValueError("native producer resolution changed")
    return world, torch, bake


def world_point(recipe, i, j):
    m = recipe["coordinate_mapping"]; oi, oj = m["model_origin_ij"]
    ox, oz = m["world_origin_xz_m"]; s = m["native_spacing_m"]
    return ox + (j - oj) * s, oz + (i - oi) * s


def raw_file(path, values):
    import numpy as np
    values = np.asarray(values, dtype="<f4")
    if not np.isfinite(values).all(): raise ValueError("nonfinite generated field")
    path.parent.mkdir(parents=True, exist_ok=True)
    values.tofile(path)
    return {"path": path.name, "sha256": digest(path), "byte_count": path.stat().st_size,
            "dtype": "float32-le", "layout": "row-major-zx", "shape": list(values.shape)}


def low_frequency(world, i0, j0, i1, j1):
    """Read upstream low-frequency metres, not arbitrary latent channels.

    Affine decoding and signed-square follow the pinned producer's _compute_elev.
    This omits the residual/denoising contribution: an approximation to STUDY,
    never declared equivalent to the filtered native terrain.
    """
    data = world.latents[:, i0:i1, j0:j1]
    sqrt_m = (data[4] / data[-1]) * 38.6 - 31.4
    return (sqrt_m.sign() * sqrt_m.square()).detach().cpu().numpy()


def survey(out, doc, world):
    import numpy as np
    bounds = doc["world"]["survey_bounds"]
    a = coverage.native_point(doc["world"], bounds["minimum"])
    b = coverage.native_point(doc["world"], bounds["maximum"])
    i0, j0 = (math.floor(v / 256) for v in a)
    i1, j1 = (math.ceil(v / 256) + 1 for v in b)
    data = world.coarse[:, i0:i1, j0:j1]
    data = (data[:-1] / data[-1:]).detach().cpu().numpy()
    elevation = np.sign(data[0]) * data[0] ** 2
    meta = raw_file(out / "survey/elevation.f32", elevation)
    candidates = []
    for i in range(1, elevation.shape[0] - 1):
        for j in range(1, elevation.shape[1] - 1):
            center = world_point(doc["world"], (i0 + i) * 256, (j0 + j) * 256)
            patch = elevation[i-1:i+2, j-1:j+2]
            plan = coverage.site_plan(doc["world"], "candidate", center)
            if plan["admitted"] and float(patch.min()) > 0:
                candidates.append({"center_xz_m": list(center), "relief_m": float(np.ptp(patch)),
                                   "mean_height_m": float(patch.mean())})
    chosen = [doc["sites"][0]]
    ranked = sorted(candidates, key=lambda c: (-c["relief_m"], c["mean_height_m"], c["center_xz_m"]))
    for name in ("mountain-interior", "valley-interior"):
        eligible = [c for c in ranked if c["relief_m"] > 200 and all(math.dist(c["center_xz_m"],
                    s["center_xz_m"]) >= 24000 for s in chosen)]
        if not eligible: raise ValueError("survey found fewer than two distinct interior candidates")
        c = eligible[0] if name == "mountain-interior" else min(eligible,
             key=lambda c: (c["mean_height_m"], -c["relief_m"]))
        chosen.append(dict(coverage.site_plan(doc["world"], name, c["center_xz_m"]), survey=c))
    write(out / "survey.json", {"elevation": meta, "grid": {"origin_xz_m":
          list(world_point(doc["world"], i0 * 256, j0 * 256)), "spacing_m": 7680},
          "candidates": candidates, "sites": chosen, "not_renderable_terrain": True})


def spike(out, doc, world, torch, bake):
    import numpy as np
    center = doc["sites"][0]["center_xz_m"]
    ni, nj = coverage.native_point(doc["world"], center)
    i, j = math.floor(ni - 255.5), math.floor(nj - 255.5)
    # Coarse samples centered on native blocks of 8; request identical native extent.
    i, j = (i // 8) * 8, (j // 8) * 8
    torch.cuda.synchronize(); start = time.perf_counter()
    low = low_frequency(world, i // 8, j // 8, i // 8 + 64, j // 8 + 64)
    torch.cuda.synchronize(); low_seconds = time.perf_counter() - start
    torch.cuda.synchronize(); start = time.perf_counter()
    # Keep the accepted exporter's explicit context halo. The upstream output
    # reconstruction itself has a boundary influence beyond the returned ROI.
    native = world.get(i - 64, j - 64, i + 576, j + 576, with_climate=True)
    torch.cuda.synchronize(); native_seconds = time.perf_counter() - start
    fine = native["elev"][64:-64, 64:-64].detach().cpu().numpy()
    filtered = bake.area_average_field(fine[None], 8)[0]
    error = np.abs(filtered - low)
    source = json.loads(SOURCE.read_text()); old = np.fromfile(SOURCE.parent / "elevation.f32", dtype="<f4").reshape(2048, 2048)
    origin = source["provenance"]["model_native_origin"]
    a, b = i - origin["i"], j - origin["j"]
    original_error = float(np.abs(fine - old[a:a+512, b:b+512]).max())
    files = {name: raw_file(out / "spike" / (name + ".f32"), v) for name, v in
             (("native", fine), ("filtered-native", filtered), ("low-frequency", low))}
    write(out / "spike.json", {"files": files, "native_origin_ij": [i, j],
          "world_origin_xz_m": list(world_point(doc["world"], i, j)),
          "low_frequency_seconds": low_seconds, "native_incremental_seconds": native_seconds,
          "low_frequency_vs_filtered_native": {"mean_abs_m": float(error.mean()),
          "p95_abs_m": float(np.percentile(error, 95)), "max_abs_m": float(error.max())},
          "existing_native_overlap_max_m": original_error, "native_overlap_pass": original_error <= 1e-4,
          "low_frequency_is_approximate": True})
    if original_error > 1e-4: raise ValueError("regenerated native terrain does not match original")


def prepare_site(out, doc, world, torch, bake, site_index):
    """Trusted fallback: native tiles for ONE backdrop, not a native huge world.

    Retain a validated regular source; its existing CPU mip sampler provides
    filtered footprints. Independently exported tier views document coverage.
    """
    import numpy as np
    survey_doc = json.loads((out / "survey.json").read_text())
    plan = survey_doc["sites"][site_index]
    if not plan["admitted"]: raise ValueError("site not admitted")
    spike_doc = json.loads((out / "spike.json").read_text())
    if not spike_doc["native_overlap_pass"]: raise ValueError("native generation gate not passed")
    bounds = plan["levels"][-1]["bounds"]
    margin = plan["sampling_margin_m"]
    minimum = [v - margin for v in bounds["minimum"]]
    maximum = [v + margin for v in bounds["maximum"]]
    a = coverage.native_point(doc["world"], minimum)
    b = coverage.native_point(doc["world"], maximum)
    i0, j0 = (math.floor(v / 8) * 8 for v in a)
    i1, j1 = (math.ceil(v / 8) * 8 + 8 for v in b)
    h, w = i1-i0, j1-j0
    if h*w > 25000000: raise ValueError("native site cell budget exceeded")
    folder = out / "sites" / plan["site_id"]
    if (folder / "site.json").exists(): raise ValueError("completed site already exists")
    folder.mkdir(parents=True, exist_ok=True)
    fine = np.empty((h, w), dtype="<f4")
    climate = np.empty((4, h//8, w//8), dtype="<f4")
    records = []; max_overlap = 0.0; previous_row = {}; current_row = {}
    start = time.perf_counter()
    for y in range(0, h, 1024):
        previous_row, current_row = current_row, {}
        left = None
        for x in range(0, w, 1024):
            sy, sx = min(1024, h-y), min(1024, w-x)
            tick = time.perf_counter()
            # Square padded requests match the accepted producer path even for
            # a partial final tile. Only admitted retained coverage is exported.
            result = world.get(i0+y-64, j0+x-64, i0+y+1088, j0+x+1088, with_climate=True)
            torch.cuda.synchronize()
            elev = result["elev"].detach().cpu().numpy()
            if left is not None:
                max_overlap = max(max_overlap, float(np.abs(left[64:-64, 1080:1096] - elev[64:-64, 56:72]).max()))
            if x in previous_row:
                max_overlap = max(max_overlap, float(np.abs(previous_row[x][56:72, 64:-64] - elev[56:72, 64:-64]).max()))
            if max_overlap > 1e-4: raise ValueError("native tile overlap mismatch")
            fine[y:y+sy, x:x+sx] = elev[64:64+sy, 64:64+sx]
            channels = bake.normalize_climate_channels(result["climate"][:4].detach().cpu().numpy())
            climate[:, y//8:(y+sy)//8, x//8:(x+sx)//8] = bake.area_average_field(
                channels[:, 64:64+sy, 64:64+sx], 8)
            left = elev.copy(); current_row[x] = elev[-128:].copy()
            records.append({"offset_yx": [y, x], "retained_shape": [sy, sx],
                            "seconds": time.perf_counter()-tick})
            print(f"site {plan['site_id']} tile {len(records)} retained {sy}x{sx}", flush=True)
    original = json.loads(SOURCE.read_text())
    old_origin = original["provenance"]["model_native_origin"]
    oy, ox = old_origin["i"] - i0, old_origin["j"] - j0
    old_overlap = None
    if 0 <= oy <= h-2048 and 0 <= ox <= w-2048:
        old = np.fromfile(SOURCE.parent / "elevation.f32", dtype="<f4").reshape(2048, 2048)
        old_overlap = float(np.abs(fine[oy:oy+2048, ox:ox+2048]-old).max())
        if old_overlap > 1e-4: raise ValueError("whole retained source overlap changed")
    elevation_file = raw_file(folder / "elevation.f32", fine)
    ox_world, oz_world = world_point(doc["world"], i0, j0)
    heightfield = {"schema": "cubey.terrain.heightfield.v1", "seed": doc["world"]["seed"],
          "source": original["source"], "height": original["height"],
          "grid": {"axis_mapping": {"world_x": "model_j", "world_z": "model_i"},
          "width": w, "height": h, "sample_spacing_m": 30,
          "sample_origin_x_m": ox_world, "sample_origin_z_m": oz_world},
          "files": {"elevation": elevation_file},
          "provenance": {"purpose": "cubey-terrain-world-site-v1", "selection": plan["site_id"],
          "world_id": doc["world"]["world_id"], "model_native_origin": {"i": i0, "j": j0}}}
    write(folder / "heightfield.json", heightfield)
    # Reuse the existing companion schema/units, with the correct block-center phase.
    companion = json.loads((SOURCE.parent / "surface-fields.json").read_text())
    climate_file = raw_file(folder / "climate.f32", climate)
    climate_file["layout"] = "channel-major-zx"
    climate_file["channels"] = companion["files"]["climate"]["channels"]
    companion["seed"] = doc["world"]["seed"]
    companion["files"]["climate"] = climate_file
    companion["grid"].update(width=w//8, height=h//8, sample_spacing_m=240,
        sample_origin_x_m=ox_world+105, sample_origin_z_m=oz_world+105)
    companion["heightfield"]["elevation_sha256"] = elevation_file["sha256"]
    companion["provenance"] = heightfield["provenance"]
    write(folder / "surface-fields.json", companion)
    # All tier fields are filtered from THIS native source, never unrelated models.
    tier_records = []
    current = fine
    for index, level in enumerate(plan["levels"]):
        factor = 2**index
        if index:
            current = current[:current.shape[0]//2*2, :current.shape[1]//2*2].reshape(
                current.shape[0]//2, 2, current.shape[1]//2, 2).mean(axis=(1, 3), dtype=np.float32)
        spacing = 30*factor
        origin = (ox_world+(spacing-30)/2, oz_world+(spacing-30)/2)
        box = level["bounds"]
        xa, za = (math.floor((v-o)/spacing) for v, o in zip(box["minimum"], origin))
        xb, zb = (math.ceil((v-o)/spacing)+1 for v, o in zip(box["maximum"], origin))
        values = current[za:zb, xa:xb]
        record = raw_file(folder / f"tier-{index}.f32", values)
        tier_records.append(dict(record, sample_spacing_m=spacing,
            sample_origin_xz_m=[origin[0]+xa*spacing, origin[1]+za*spacing], requested=level))
    plan.update({"heightfield": "heightfield.json", "surface_fields": "surface-fields.json",
                 "actual_native_bounds": {"minimum": [ox_world, oz_world],
                 "maximum": [ox_world+(w-1)*30, oz_world+(h-1)*30]},
                 "tier_fields": tier_records, "generation_strategy": "native-tiled-filtered-fallback",
                 "generation": {"seconds": time.perf_counter()-start, "tiles": records,
                 "max_tile_overlap_m": max_overlap, "retained_source_overlap_max_m": old_overlap},
                 "runtime_source": "native-raster-with-existing-filtered-mip-sampling"})
    write(folder / "site.json", plan)
    write(folder / "world-recipe.json", doc["world"])


def worker(out, phase):
    doc = check(out); start = time.perf_counter()
    world, torch, bake = pipeline(doc)
    setup = time.perf_counter() - start
    try:
        if phase == "survey": survey(out, doc, world)
        elif phase == "spike": spike(out, doc, world, torch, bake)
        elif phase.startswith("site-"): prepare_site(out, doc, world, torch, bake, int(phase[-1]))
        else: raise ValueError("unknown worker phase")
        check(out)
        write(out / (phase + "-receipt.json"), {"phase": phase, "setup_seconds": setup,
              "total_seconds": time.perf_counter() - start, "peak_rss_kib": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
              "peak_cuda_allocated_bytes": torch.cuda.max_memory_allocated(), "producer_unchanged": True})
    finally:
        world.close()


def run(out, phase):
    check(out)
    env = os.environ.copy(); env.pop("DISPLAY", None); env.pop("WAYLAND_DISPLAY", None)
    env["PYTHONPATH"] = str(REFERENCE)
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    cmd = ["rtk", "proxy", str(ENV), "-u", str(Path(__file__).resolve()), str(out), "--worker", phase]
    start = time.perf_counter()
    log_path = out / (phase + ".log")
    if log_path.exists():
        attempt = len(list(out.glob(phase + "-previous-*.log")))
        log_path.rename(out / f"{phase}-previous-{attempt}.log")
        previous = out / (phase + "-attempt.json")
        if previous.exists(): previous.rename(out / f"{phase}-previous-{attempt}.json")
    with log_path.open("w") as log:
        child = subprocess.Popen(cmd, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            returncode = child.wait(timeout=LIMIT_SECONDS)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid, signal.SIGTERM)
            try: child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL); child.wait()
            write(out / (phase + "-attempt.json"), {"timeout_seconds": LIMIT_SECONDS, "command": cmd})
            raise
    check(out)
    write(out / (phase + "-attempt.json"), {"command": cmd, "returncode": returncode,
          "wall_seconds": time.perf_counter() - start, "runner_sha256": digest(__file__)})
    if returncode: raise RuntimeError("producer phase failed; see " + str(out / (phase + ".log")))


def validate(out):
    doc = check(out); receipts = []
    for folder in sorted((out / "sites").iterdir()):
        site = json.loads((folder / "site.json").read_text())
        # Also finalizes packages created before the world-recipe sidecar was added.
        write(folder / "world-recipe.json", doc["world"])
        expected = coverage.site_plan(doc["world"], site["site_id"], site["center_xz_m"])
        for key in ("world_id", "levels", "required_bounds", "admitted", "rejection_reason"):
            if json.dumps(site[key], sort_keys=True) != json.dumps(expected[key], sort_keys=True):
                raise ValueError("site coverage contract mismatch: " + key)
        field = json.loads((folder / "heightfield.json").read_text())
        companion = json.loads((folder / "surface-fields.json").read_text())
        records = [field["files"]["elevation"], companion["files"]["climate"], *site["tier_fields"]]
        for record in records:
            path = folder / record["path"]
            if (path.is_symlink() or not path.resolve().is_relative_to(folder.resolve())
                    or path.stat().st_size != record["byte_count"] or digest(path) != record["sha256"]
                    or math.prod(record["shape"]) * 4 != record["byte_count"]):
                raise ValueError("invalid prepared terrain payload")
        if companion["heightfield"]["elevation_sha256"] != field["files"]["elevation"]["sha256"]:
            raise ValueError("climate binding mismatch")
        for tier in site["tier_fields"]:
            lower = tier["sample_origin_xz_m"]
            upper = [lower[0]+(tier["shape"][1]-1)*tier["sample_spacing_m"],
                     lower[1]+(tier["shape"][0]-1)*tier["sample_spacing_m"]]
            if not coverage.Box(tuple(lower), tuple(upper)).contains(coverage.Box(**{
                    k: tuple(v) for k, v in tier["requested"]["bounds"].items()})):
                raise ValueError("tier does not cover its requested box")
        receipts.append({"site_id": site["site_id"], "bytes": sum(r["byte_count"] for r in records),
                         "site_sha256": digest(folder / "site.json"), "heightfield_sha256": digest(folder / "heightfield.json")})
    write(out / "package-validation.json", {"sites": receipts, "protected_unchanged": True})


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("output", type=Path)
    phases = ("freeze", "survey", "spike", "site-0", "site-1", "site-2", "validate")
    parser.add_argument("--phase", choices=phases, default="freeze")
    parser.add_argument("--worker", choices=phases[1:-1])
    args = parser.parse_args(); out = owned(args.output)
    if args.worker: worker(out, args.worker)
    elif args.phase == "freeze": freeze(out)
    elif args.phase == "validate": validate(out)
    else: run(out, args.phase)


if __name__ == "__main__": main()
