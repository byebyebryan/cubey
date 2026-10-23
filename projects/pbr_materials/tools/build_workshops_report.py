#!/usr/bin/env python3
"""Assemble the bounded native material workshop review bundle."""
import argparse
import html
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
LABELS = {"plain": "Neutral gray", "limestone": "Limestone", "marble": "Balanced marble", "clay": "Dry terracotta", "ceramic": "Ivory ceramic", "bronze": "Satin bronze", "glass": "Tinted glass"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=ROOT / "outputs/material-workshops/qualification")
    parser.add_argument("--output", type=Path, default=ROOT / "outputs/material-workshops/report")
    args = parser.parse_args()
    source, output = args.input.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((source / "manifest.json").read_text())
    timings = json.loads((source / "timings.json").read_text())
    for p in source.iterdir():
        if p.suffix in (".png", ".mp4", ".log", ".csv", ".json", ".txt"):
            shutil.copy2(p, output / p.name)
    shutil.copy2(ROOT / "outputs/material-workshops/final-controls/manifest.json", output / "compatibility.json")
    shutil.copy2(ROOT / "outputs/material-workshops/glass-check/diagnostics.json", output / "glass-diagnostics.json")
    recipes = output / "recipes"
    recipes.mkdir(exist_ok=True)
    recipe_blocks = []
    for key, label in LABELS.items():
        path = ROOT / "assets/materials" / f"{key}.json"
        shutil.copy2(path, recipes / path.name)
        recipe_blocks.append(f'<details><summary>{label} / <a href="recipes/{key}.json">JSON</a></summary><pre>{html.escape(path.read_text())}</pre></details>')
    rows = []
    for renderer, material, title in (("mesh", "gallery", "Mesh / seven-material gallery"), ("mesh", "glass", "Mesh / glass sphere"), ("sdf", "marble", "SDF / marble CSG"), ("sdf", "glass", "SDF / glass CSG")):
        for lighting in ("studio", "clear", "clouds"):
            data = timings[f"profile-{renderer}-{material}-{lighting}"]
            rows.append(f'<tr><td>{title}</td><td>{lighting}</td><td>{data["median_ms"]:.3f}</td><td>{data["p95_ms"]:.3f}</td><td>{data["samples"]}</td></tr>')
    films = []
    for material in ("bronze", "glass"):
        for lighting in ("studio", "clouds"):
            stem = f"mesh-{material}-{lighting}"
            films.append(f'<figure><video controls loop muted preload="metadata" poster="{stem}-front.png"><source src="{stem}-orbit.mp4" type="video/mp4"></video><figcaption>Mesh / {LABELS[material]} / {lighting}</figcaption></figure>')
    text = Path(__file__).with_name("workshops_report.html").read_text()
    values = {"__OPTIONS__": "".join(f'<option value="{k}"'+(' selected' if k == 'marble' else '')+f'>{v}</option>' for k, v in LABELS.items()),
        "__COSTROWS__": "".join(rows), "__RECIPES__": "".join(recipe_blocks), "__FILMS__": "".join(films), "__SOURCE__": manifest["source_commit"]}
    for key, value in values.items():
        text = text.replace(key, value)
    size = sum(p.stat().st_size for p in output.rglob("*") if p.is_file()) + len(text.encode())
    text = text.replace("__BUNDLE__", f"{size / 2**20:.1f}")
    assert "__" not in text
    (output / "index.html").write_text(text)
    if size >= 100 * 2**20:
        raise RuntimeError("review bundle exceeded its 100 MiB bound")
    print(f"Built {output}: {size / 2**20:.1f} MiB")


if __name__ == "__main__":
    main()
