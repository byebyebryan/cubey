#!/usr/bin/env python3
"""Bounded native qualification of the twelve-entry procedural material library.

GPU processes run sequentially. Each receipt retains exact commands, binary,
shader and recipe identities. Old workshop evidence is never overwritten.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from library_environment import write_environment
from qualify_workshops import write_timing_summary

ROOT = Path(__file__).resolve().parents[3]
FIXTURES = {"wood": "cut-block", "rusted-steel": "swatch", "kraft-cardboard": "swatch",
            "woven-cloth": "swatch", "frosted-glass": "sphere"}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "outputs/material-library/qualification")
    parser.add_argument("--stage", choices=("audition", "captures", "profiles", "films", "compatibility", "all"), default="all")
    parser.add_argument("--only", action="append", default=[], help="Run only these named native cases; repeatable.")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "recipes").mkdir(exist_ok=True)
    (output / "scenes").mkdir(exist_ok=True)
    catalog_path = ROOT / "assets/materials/catalog.json"
    catalog = json.loads(catalog_path.read_text())
    ids = [entry["id"] for entry in catalog["entries"]]
    if len(ids) != len(set(ids)):
        raise ValueError("duplicate catalog ID")
    environment = output / "diagnostic.hdr"
    write_environment(environment)
    binaries = {r: ROOT / "build/release/projects" / ("pbr_materials/pbr_materials" if r == "mesh" else "sdf/sdf") for r in ("mesh", "sdf")}
    identity = dict(source_commit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        source_dirty=bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()),
        binaries={r: sha(b) for r, b in binaries.items()},
        shaders={r: {p.name: sha(p) for p in sorted((b.parent / "shaders").glob("*.spv"))} for r, b in binaries.items()},
        recipes={p.name: sha(p) for p in sorted((ROOT / "assets/materials").glob("*.json"))},
        diagnostic_hdr_sha256=sha(environment))
    manifest_path = output / "manifest.json"
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {"runs": {}}
    manifest["identity"] = identity
    manifest["catalog"] = catalog

    def run(name, renderer, material, shape="sphere", lighting="studio", extra=(), yaw=0,
            pitch=.15, distance=1.05, size=(960, 720), suffix="png", bars=False, validate=False):
        if args.only and name not in args.only:
            return
        artifact = output / f"{name}.{suffix}"
        command = [str(binaries[renderer]), "--headless", "--require-validation", "--width", str(size[0]),
                   "--height", str(size[1]), "--material", material, "--yaw", str(yaw),
                   # The legacy orbit adapters use opposite pitch signs.
                   "--pitch", str(-pitch if renderer == "mesh" else pitch), "--distance", str(distance),
                   "--output", str(artifact)]
        if renderer == "mesh":
            command += ["--specimen", shape, "--lighting", lighting]
            if bars: command += ["--environment", str(environment)]
        else:
            command += ["--workshop", shape, "--lighting", "material-study" if lighting == "studio" else "material-atmosphere",
                        "--supersample", "1", "--glass-crossings", "8"]
            if lighting == "clouds": command += ["--clouds"]
            if bars: command += ["--material-environment", str(environment)]
            if validate: command += ["--validate-field"]
        command += list(extra)
        completed = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (output / f"{name}.log").write_text(completed.stdout)
        if completed.returncode or "vulkan validation error" in completed.stdout.lower():
            raise RuntimeError(f"{name} failed: {completed.stdout[-3000:]}")
        manifest["runs"][name] = dict(renderer=renderer, material=material, specimen=shape, lighting=lighting,
            yaw=yaw, pitch=pitch, distance=distance, bars=bars, command=command,
            log=f"{name}.log", artifact=artifact.name if artifact.exists() else None,
            sha256=sha(artifact) if artifact.exists() else None, identity=identity)
        manifest_path.write_text(json.dumps(manifest, indent=2)+"\n")
        print(name, flush=True)

    if args.stage in ("audition", "all"):
        for renderer in binaries:
            for material, shape in FIXTURES.items():
                run(f"audition-{renderer}-{material}",renderer,material,shape,yaw=.5,pitch=.35,
                    validate=renderer=="sdf",size=(800,640))
    if args.stage in ("captures", "all"):
        for renderer in binaries:
            for material in ids:
                export = (("--recipe-output",str(output / "recipes" / f"{material}.json")) if renderer=="mesh" else
                          ("--export-authored",str(output / "scenes" / f"{material}.json")))
                run(f"{renderer}-{material}-studio",renderer,material,size=(640,640),extra=export,
                    validate=renderer=="sdf" and material in FIXTURES)
            for material, shape in FIXTURES.items():
                run(f"{renderer}-{material}-fixture",renderer,material,shape,
                    yaw=0 if material=="frosted-glass" else .5,
                    pitch=.15 if material=="frosted-glass" else .35,bars=material=="frosted-glass")
            for material in ("wood","rusted-steel","woven-cloth","frosted-glass"):
                run(f"{renderer}-{material}-clouds",renderer,material,FIXTURES[material],"clouds",yaw=.5,pitch=.35)
            for roughness in (.05,.55):
                recipe=json.loads((ROOT/"assets/materials/frosted-glass.json").read_text())
                recipe["surface"]["roughness"]=roughness
                recipe_path=output/f"frost-{roughness}.json"
                recipe_path.write_text(json.dumps(recipe,indent=2)+"\n")
                run(f"{renderer}-frost-{roughness}",renderer,"frosted-glass",bars=True,
                    extra=("--recipe-input" if renderer=="mesh" else "--material-recipe",str(recipe_path)))
            for component, sdf_debug in (("base-color",6),("roughness",7),("metallic",12)):
                run(f"{renderer}-rust-{component}",renderer,"rusted-steel","swatch",yaw=.5,pitch=.35,
                    extra=("--debug-view",component) if renderer=="mesh" else ("--debug",str(sdf_debug)))
            run(f"{renderer}-cloth-far",renderer,"woven-cloth","swatch",distance=2.1,yaw=.5,pitch=.35)
        run("mesh-gallery","mesh","gallery",size=(1920,1080))
        for cut in (False,True):
            run(f"sdf-rust-csg-cut-{int(cut)}","sdf","rusted-steel","csg",yaw=.4,pitch=.25,
                extra=("--workshop-cut-material",) if cut else (),validate=True)
    if args.stage in ("profiles", "all"):
        for renderer,material,shape,lighting in (
            ("mesh","gallery","sphere","studio"),("mesh","gallery","sphere","clouds"),
            ("mesh","woven-cloth","swatch","studio"),("mesh","frosted-glass","sphere","studio"),
            ("mesh","frosted-glass","sphere","clouds"),("sdf","wood","cut-block","studio"),
            ("sdf","rusted-steel","csg","studio"),("sdf","woven-cloth","swatch","studio"),
            ("sdf","woven-cloth","swatch","clouds"),("sdf","frosted-glass","sphere","studio"),
            ("sdf","frosted-glass","sphere","clouds")):
            name=f"profile-{renderer}-{material}-{lighting}"
            run(name,renderer,material,shape,lighting,size=(1920,1080),extra=("--headless-render-only","--frames","112",
                "--profile-warmup-frames","16","--profile-output",str(output/name)))
        for material in ("rusted-steel","woven-cloth","frosted-glass"):
            run(f"stress-{material}","mesh",material,extra=("--headless-render-only","--frames","32","--stress-edits","1000"))
        write_timing_summary(output)
    if args.stage in ("films", "all"):
        for renderer,material,lighting in (("sdf","wood","clouds"),("mesh","woven-cloth","studio"),("sdf","frosted-glass","clouds")):
            orbit=("--orbit-degrees","45") if renderer=="mesh" else ("--motion","orbit","--duration","8")
            run(f"film-{renderer}-{material}",renderer,material,FIXTURES[material],lighting,suffix="mp4",
                extra=("--capture","video","--frames","240","--fps","30",*orbit))
    if args.stage in ("compatibility", "all"):
        references=json.loads((ROOT/"outputs/material-workshops/report/compatibility.json").read_text())
        comparison={}
        for name,reference in references.items():
            command=reference["command"][:]
            target=output/f"compat-{name}.png"
            command[command.index("--output")+1]=str(target)
            result=subprocess.run(command,cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            (output/f"compat-{name}.log").write_text(result.stdout)
            baseline=ROOT/"outputs/material-workshops/final-controls"/f"{name}.png"
            binary=(ROOT/command[0]).resolve()
            comparison[name]=dict(command=command,returncode=result.returncode,byte_identical=target.exists() and sha(target)==sha(baseline),
                validation_errors="vulkan validation error" in result.stdout.lower(),binary_sha256=sha(binary),
                shader_sha256={p.name:sha(p) for p in sorted((binary.parent/"shaders").glob("*.spv"))},
                baseline_sha256=sha(baseline),sha256=sha(target) if target.exists() else None)
            print("compat",name,comparison[name]["byte_identical"],flush=True)
        (output/"compatibility.json").write_text(json.dumps(comparison,indent=2)+"\n")
        if not all(v["returncode"]==0 and v["byte_identical"] and not v["validation_errors"] for v in comparison.values()):
            raise RuntimeError("compatibility comparison failed; inspect component evidence")


if __name__ == "__main__":
    main()
