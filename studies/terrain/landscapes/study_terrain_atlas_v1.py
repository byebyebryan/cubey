#!/usr/bin/env python3
"""Display-only filtered world atlas. Cached native fields first; bounded producer second.

Never uses the coarse conditioning latent as replacement terrain. Filtering is the
exact separable box integral of the piecewise-linear native reconstruction.
Only a padded native tile and the 1025-square atlas are retained by this exporter.
"""
from __future__ import annotations
import argparse
import json
import math
import os
from pathlib import Path
import resource
import signal
import shutil
import subprocess
import tempfile
import time
import study_terrain_world_v1 as study

WORLD = study.ROOT / "cache/terrain/worlds/v1/f12244a1ff943e37"
SPACING = 256
SIZE = 1025


def inputs():
    paths = [p for p in WORLD.rglob("*") if p.is_file()]
    return {str(p.relative_to(study.ROOT)): study.digest(p) for p in sorted(paths)}


def owned(out):
    out = Path(out).resolve()
    if out.parent != study.ROOT / "outputs/terrain" or not out.name.startswith("readability-"):
        raise ValueError("fresh outputs/terrain/readability-* leaf required")
    return out


def freeze(out):
    if out.exists(): raise ValueError("refuse existing output")
    world = json.loads((WORLD / "world-recipe.json").read_text())
    out.mkdir()
    study.write(out / "protocol.json", {"schema": "cubey.terrain.readability-study.v1",
        "world": world, "packages": inputs(), "protected": study.protected(),
        "code_revision": world["producer"]["code_revision"], "wall_limit_seconds": 900,
        "atlas": {"shape": [SIZE, SIZE], "spacing_m": SPACING,
                  "origin_xz_m": world["survey_bounds"]["minimum"],
                  "purpose": "display-only-not-runtime-height-source"},
        "rendering": "headless-vulkan-only", "default_promotion": False, "push": False})


def check(out):
    doc = study.check(out)
    if doc["packages"] != inputs(): raise ValueError("prepared packages changed")
    return doc


def hat_integral(t):
    import numpy as np
    return np.where(t <= -1, 0, np.where(t < 0, .5*(t+1)**2,
                    np.where(t < 1, 1-.5*(1-t)**2, 1)))


def axis_weights(centers, origin, footprint=SPACING, spacing=30):
    import numpy as np
    coordinate = (np.asarray(centers, dtype=np.float64)-origin)/spacing
    radius = math.ceil(footprint/(2*spacing))+1
    indices = np.floor(coordinate).astype(np.int64)[:, None] + np.arange(-radius, radius+2)
    upper = coordinate[:, None] + footprint/(2*spacing) - indices
    lower = coordinate[:, None] - footprint/(2*spacing) - indices
    weights = (hat_integral(upper)-hat_integral(lower))*spacing/footprint
    if not np.allclose(weights.sum(axis=1), 1, atol=1e-12):
        raise ValueError("filter normalization failed")
    return indices, weights


def filtered(values, origin, xs, zs):
    import numpy as np
    xi, xw = axis_weights(xs, origin[0]); zi, zw = axis_weights(zs, origin[1])
    if min(xi.min(), zi.min()) < 0 or xi.max() >= values.shape[1] or zi.max() >= values.shape[0]:
        raise ValueError("filter halo outside native tile")
    result = np.zeros((len(zs), len(xs)), dtype=np.float64)
    for k in range(zi.shape[1]):
        taps = values[zi[:, k, None, None], xi[None, :, :]]
        result += (taps*xw[None, :, :]).sum(axis=2)*zw[:, k, None]
    return result.astype("<f4")


def build(out, missing=False):
    import numpy as np
    doc = check(out); origin = doc["atlas"]["origin_xz_m"]
    xs = origin[0]+np.arange(SIZE)*SPACING
    zs = origin[1]+np.arange(SIZE)*SPACING
    atlas = np.zeros((SIZE, SIZE), dtype="<f4")
    kind = np.zeros((SIZE, SIZE), dtype=np.uint8)
    # The support guard includes zero-valued end taps, so no indexing clamps.
    guard = 8*30
    for manifest in sorted(WORLD.glob("sites/*/heightfield.json")):
        field = json.loads(manifest.read_text()); g = field["grid"]
        values = np.memmap(manifest.parent / field["files"]["elevation"]["path"], dtype="<f4",
                           mode="r", shape=(g["height"], g["width"]))
        ox, oz = g["sample_origin_x_m"], g["sample_origin_z_m"]
        xx = np.where((xs >= ox+guard) & (xs <= ox+(g["width"]-1)*30-guard))[0]
        zz = np.where((zs >= oz+guard) & (zs <= oz+(g["height"]-1)*30-guard))[0]
        for a in range(0, len(zz), 32):
            yy = zz[a:a+32]
            for b in range(0, len(xx), 128):
                x = xx[b:b+128]; idx = np.ix_(yy, x)
                v = filtered(values, (ox, oz), xs[x], zs[yy])
                if np.any(kind[idx]):
                    overlap = np.abs(atlas[idx]-v)[kind[idx] > 0]
                    if len(overlap) and overlap.max() > .0001: raise ValueError("cached atlas overlap mismatch")
                atlas[idx] = v; kind[idx] = 1
        del values
    records = []; max_error = 0.; start = time.perf_counter(); setup = 0.
    if missing:
        world, torch, _ = study.pipeline(doc); setup = time.perf_counter()-start
        try:
            i = np.array([study.coverage.native_point(doc["world"], (xs[0], z))[0] for z in zs])
            j = np.array([study.coverage.native_point(doc["world"], (x, zs[0]))[1] for x in xs])
            for ty in np.unique(np.floor(i/1024).astype(int)):
                yy = np.where(np.floor(i/1024) == ty)[0]
                for tx in np.unique(np.floor(j/1024).astype(int)):
                    xx = np.where(np.floor(j/1024) == tx)[0]; idx = np.ix_(yy, xx)
                    if np.all(kind[idx]): continue
                    tick = time.perf_counter(); i0, j0 = int(ty)*1024, int(tx)*1024
                    raw = world.get(i0-64, j0-64, i0+1088, j0+1088, with_climate=False)
                    torch.cuda.synchronize()
                    values = raw["elev"].detach().cpu().numpy()
                    v = filtered(values, study.world_point(doc["world"], i0-64, j0-64), xs[xx], zs[yy])
                    old = kind[idx] > 0
                    if np.any(old):
                        max_error = max(max_error, float(np.abs(atlas[idx]-v)[old].max()))
                        if max_error > .0001: raise ValueError("generated/cached filtered overlap mismatch")
                    block = atlas[idx]; block[~old] = v[~old]; atlas[idx] = block
                    types = kind[idx]; types[~old] = 2; kind[idx] = types
                    records.append({"native_origin_ij": [i0, j0], "seconds": time.perf_counter()-tick})
                    save(out, atlas, kind, records, setup, start, max_error, torch)
                    print(f"atlas tile {len(records)} coverage {np.count_nonzero(kind)}/{kind.size}", flush=True)
        finally:
            world.close()
    save(out, atlas, kind, records, setup, start, max_error, torch if missing else None)
    check(out)


def save(out, atlas, kind, records, setup, start, error, torch=None):
    import numpy as np
    folder = out / "atlas"; folder.mkdir(exist_ok=True)
    data = study.raw_file(folder / "elevation.f32", atlas)
    kind.tofile(folder / "coverage.u8")
    study.write(folder / "atlas.json", {"schema": "cubey.terrain.display-atlas.v1",
        "world_id": json.loads((out / "protocol.json").read_text())["world"]["world_id"],
        "elevation": data, "coverage_sha256": study.digest(folder / "coverage.u8"),
        "coverage_codes": {"0": "uncovered-no-filler", "1": "cached-native-filtered", "2": "generated-native-filtered"},
        "covered_samples": int(np.count_nonzero(kind)), "total_samples": int(kind.size),
        "cached_samples": int(np.count_nonzero(kind == 1)), "generated_samples": int(np.count_nonzero(kind == 2)),
        "complete": bool(np.all(kind)), "spacing_m": SPACING,
        "filter": "256m box integral of native 30m piecewise-linear reconstruction; float64 accumulation",
        "generated_tiles": records, "setup_seconds": setup, "total_seconds": time.perf_counter()-start,
        "max_filtered_overlap_m": error, "peak_rss_kib": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
        "peak_cuda_allocated_bytes": torch.cuda.max_memory_allocated() if torch else None,
        "runtime_height_source": False})


def run(out):
    check(out)
    env = os.environ.copy(); env.pop("DISPLAY", None); env.pop("WAYLAND_DISPLAY", None)
    env.update(PYTHONPATH=str(study.REFERENCE), PYTHONDONTWRITEBYTECODE="1")
    cmd = ["rtk", "proxy", str(study.ENV), "-u", str(Path(__file__).resolve()), str(out), "--phase", "worker"]
    start = time.perf_counter(); timed_out = False
    with (out / "atlas-generation.log").open("x") as log:
        child = subprocess.Popen(cmd, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try: code = child.wait(timeout=900)
        except subprocess.TimeoutExpired:
            timed_out = True; os.killpg(child.pid, signal.SIGTERM)
            try: code = child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL); code = child.wait()
    study.write(out / "atlas-attempt.json", {"command": cmd, "returncode": code,
        "timed_out": timed_out, "wall_seconds": time.perf_counter()-start, "limit_seconds": 900})
    check(out)
    if code and not timed_out: raise RuntimeError("atlas failed; retained logs and partial coverage")


def publish(out):
    """Publish only a complete validated display atlas, separate from immutable sites."""
    import numpy as np
    doc=check(out); folder=out/"atlas"
    manifest=json.loads((folder/"atlas.json").read_text())
    raw=np.fromfile(folder/"elevation.f32",dtype="<f4")
    mask=np.fromfile(folder/"coverage.u8",dtype="u1")
    if (len(raw)!=SIZE*SIZE or len(mask)!=len(raw) or not np.isfinite(raw).all()
        or manifest["elevation"]["shape"]!=[SIZE,SIZE] or manifest["spacing_m"]!=SPACING
        or not manifest["complete"] or not np.isin(mask,[1,2]).all()
        or study.digest(folder/"elevation.f32")!=manifest["elevation"]["sha256"]
        or study.digest(folder/"coverage.u8")!=manifest["coverage_sha256"]
        or manifest["world_id"]!=doc["world"]["world_id"]):
        raise ValueError("cannot publish invalid or partial atlas")
    target=study.ROOT/"cache/terrain/atlases/v1"/doc["world"]["world_id"][:16]/"256m"
    if target.exists(): raise ValueError("refuse existing atlas cache")
    target.parent.mkdir(parents=True,exist_ok=True)
    temporary=Path(tempfile.mkdtemp(prefix=".atlas-prepare-",dir=target.parent))
    shutil.copy2(folder/"elevation.f32",temporary/"elevation.f32")
    shutil.copy2(folder/"coverage.u8",temporary/"coverage.u8")
    manifest.update(grid=doc["atlas"],height_transform=doc["world"]["height_transform"],
        producer=doc["world"]["producer"],purpose="display-only-not-runtime-height-source")
    study.write(temporary/"atlas.json",manifest)
    for name in ("elevation.f32","coverage.u8"):
        if study.digest(temporary/name)!=study.digest(folder/name): raise ValueError("atlas cache copy changed")
    temporary.rename(target)
    study.write(out/"published-atlas.json",{"cache_root":str(target),"copied_exact":True,
        "runtime_height_source":False,"world_packages_unchanged":True})
    check(out)


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--phase", choices=("freeze", "cached", "generate", "worker", "publish"), default="freeze")
    args = parser.parse_args(); out = owned(args.output)
    if args.phase == "freeze": freeze(out)
    elif args.phase == "generate": run(out)
    elif args.phase == "publish": publish(out)
    else: build(out, missing=args.phase == "worker")


if __name__ == "__main__": main()
