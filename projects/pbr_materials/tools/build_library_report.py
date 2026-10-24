#!/usr/bin/env python3
"""Export the bounded native material-library evidence as a static review bundle."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[3]


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
    if not all(v["returncode"] == 0 and v["byte_identical"] for v in compatibility.values()):
        raise ValueError("compatibility evidence has not passed")
    runs = {k: v for k, v in manifest["runs"].items() if v["artifact"] and not k.startswith("audition-")}
    images = [r for r in runs.values() if r["artifact"].endswith(".png")]
    films = [r for r in runs.values() if r["artifact"].endswith(".mp4")]
    if len(images) > 64 or len(films) > 4 or len(manifest["catalog"]["entries"]) != 12:
        raise ValueError("report exceeds its bounded collection")
    for r in runs.values():
        artifact = source / r["artifact"]
        if hashlib.sha256(artifact.read_bytes()).hexdigest() != r["sha256"]:
            raise ValueError(f"native capture bytes changed: {artifact}")
        shutil.copy2(artifact, target / artifact.name)
    for p in source.glob("*.log"):
        shutil.copy2(p, target / p.name)
    for name in ("manifest.json", "timings.json", "compatibility.json", "diagnostic.hdr"):
        shutil.copy2(source / name, target / name)
    for name in ("recipes", "scenes"):
        shutil.copytree(source / name, target / name, dirs_exist_ok=True)
    for name in ("tests-scoped.log", "tests.log"):
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
    inventory = {str(p.relative_to(target)): dict(bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                 for p in sorted(target.rglob("*")) if p.is_file() and p.name != "bundle.json"}
    (target / "bundle.json").write_text(json.dumps(inventory,indent=2)+"\n")
    print(json.dumps(dict(images=len(images),films=len(films),files=len(inventory)+1,
        bytes=sum(v["bytes"] for v in inventory.values()),output=str(target)),indent=2))


if __name__ == "__main__":
    main()
