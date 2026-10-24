#!/usr/bin/env python3
"""Export the bounded native material-library evidence as a static review bundle."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def validate_compatibility(compatibility):
    expected = {"sphere-clay-reference", "sphere-glass-reference", "guardian-clay-reference",
                "guardian-glass-reference", "furnace", "furnace-ior", "gltf"}
    if compatibility.keys() != expected:
        raise ValueError("compatibility evidence must contain all seven retained controls")
    for name, result in compatibility.items():
        if (result.get("returncode") != 0 or result.get("byte_identical") is not True
                or result.get("validation_errors") is not False
                or not result.get("sha256")
                or result.get("sha256") != result.get("baseline_sha256")):
            raise ValueError(f"compatibility evidence has not passed: {name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=ROOT / "outputs/material-library/qualification")
    parser.add_argument("--output", type=Path, default=ROOT / "outputs/material-library/report")
    args = parser.parse_args()
    source, target = args.input.resolve(), args.output.resolve()
    target.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((source / "manifest.json").read_text())
    timings = json.loads((source / "timings.json").read_text())
    compatibility = json.loads((source / "compatibility.json").read_text())
    validate_compatibility(compatibility)
    runs = {k: v for k, v in manifest["runs"].items() if v["artifact"] and not k.startswith("audition-")}
    images = [r for r in runs.values() if r["artifact"].endswith(".png")]
    films = [r for r in runs.values() if r["artifact"].endswith(".mp4")]
    if len(images) > 64 or len(films) > 4 or len(manifest["catalog"]["entries"]) != 12:
        raise ValueError("report exceeds its bounded collection")
    required={"mesh-gallery", "sdf-rust-csg-cut-0", "sdf-rust-csg-cut-1",
              "film-sdf-wood", "film-mesh-woven-cloth", "film-sdf-frosted-glass"}
    for renderer in ("mesh", "sdf"):
        required.update(f"{renderer}-{e['id']}-studio" for e in manifest["catalog"]["entries"])
        required.update(f"{renderer}-{name}-fixture" for name in
                        ("wood","rusted-steel","kraft-cardboard","woven-cloth","frosted-glass"))
        required.update(f"{renderer}-rust-{name}" for name in ("base-color","roughness","metallic"))
        required.update(f"{renderer}-{name}-clouds" for name in ("wood","rusted-steel","woven-cloth","frosted-glass"))
        required.update((f"{renderer}-frost-0.05",f"{renderer}-frost-0.55",f"{renderer}-cloth-far"))
    if missing:=required-runs.keys():
        raise ValueError(f"incomplete native report inputs: {sorted(missing)}")
    for entry in manifest["catalog"]["entries"]:
        for directory in ("recipes","scenes"):
            if not (source/directory/f"{entry['id']}.json").is_file():
                raise ValueError(f"missing resolved {directory} snapshot for {entry['id']}")
    for r in runs.values():
        artifact = source / r["artifact"]
        if hashlib.sha256(artifact.read_bytes()).hexdigest() != r["sha256"]:
            raise ValueError(f"native capture bytes changed: {artifact}")
        shutil.copy2(artifact, target / artifact.name)
    for p in source.glob("*.log"):
        shutil.copy2(p, target / p.name)
    for pattern in ("profile-*.csv", "profile-*.trace.json"):
        for p in source.glob(pattern): shutil.copy2(p,target/p.name)
    for name in ("manifest.json", "timings.json", "compatibility.json", "diagnostic.hdr"):
        shutil.copy2(source / name, target / name)
    for name in ("recipes", "scenes"):
        shutil.copytree(source / name, target / name, dirs_exist_ok=True)
    for name in ("tests-scoped.log", "tests.log", "scoped-recipes.json", "browser-qa.json", "video-qa.json"):
        path = ROOT / "outputs/material-library" / name
        if path.exists(): shutil.copy2(path, target / name)
    table = []
    for name, timing in timings.items():
        label = name.removeprefix("profile-").replace("-", " ")
        table.append(f'<tr><td>{html.escape(label)}</td><td>{timing["median_ms"]:.3f}</td><td>{timing["p95_ms"]:.3f}</td><td>{timing["samples"]}</td></tr>')
    data = dict(catalog=manifest["catalog"]["entries"], runs={k: {field: v[field] for field in
                ("artifact", "renderer", "material", "specimen", "lighting", "bars")} for k, v in runs.items()})
    template = Path(__file__).with_name("library_report.html").read_text()
    document = template.replace("@@DATA@@", json.dumps(data).replace("</", "<\\/"))
    document = document.replace("@@TIMINGS@@", "\n".join(table)).replace("@@IMAGE_COUNT@@",str(len(images))).replace("@@FILM_COUNT@@",str(len(films)))
    (target / "index.html").write_text(document)
    generator_files=(Path(__file__).resolve(),Path(__file__).with_name("library_report.html"))
    (target/"report.json").write_text(json.dumps(dict(
        generator_commit=subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip(),
        generator_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in generator_files},
        capture_commits=sorted({v["identity"]["source_commit"] for v in manifest["runs"].values()}),
        images=len(images),films=len(films)),indent=2)+"\n")
    inventory = {str(p.relative_to(target)): dict(bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                 for p in sorted(target.rglob("*")) if p.is_file() and p.name != "bundle.json"}
    (target / "bundle.json").write_text(json.dumps(inventory,indent=2)+"\n")
    print(json.dumps(dict(images=len(images),films=len(films),files=len(inventory)+1,
        bytes=sum(v["bytes"] for v in inventory.values()),output=str(target)),indent=2))


if __name__ == "__main__":
    main()
