#!/usr/bin/env python3
"""Prepare a static review of native negative-sticks captures and measurements."""
import argparse
import html
import json
from pathlib import Path
import re
import shutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=Path("outputs/sdf/negative-qualification"))
    parser.add_argument("--audition", type=Path, default=Path("outputs/sdf/negative-audition-selected"))
    parser.add_argument("--capacity", type=Path, default=Path("outputs/sdf/negative-audition-axis64"))
    parser.add_argument("--output", type=Path, default=Path("outputs/sdf/negative-review-export"))
    args = parser.parse_args()
    if args.output.exists():
        raise RuntimeError("Use a fresh report export directory")
    data = json.loads((args.input / "manifest.json").read_text())
    if not (args.input / "presentation.mp4").exists():
        raise RuntimeError("Capture the presentation with qualify_negative.py --video first")
    for source, name in ((args.input, "evidence"), (args.audition, "audition"), (args.capacity, "capacity")):
        shutil.copytree(source, args.output / name)
    recipe = json.loads((args.input / "recipe.json").read_text())
    hero = data["runs"]["hero"]
    oracle = next(s for s in hero["field_stats"] if s.startswith("sdf: oracle"))
    stats = next(s for s in hero["field_stats"] if s.startswith("sdf: edits="))
    bricks = re.search(r"bricks=(\d+)/(\d+)", stats)
    allocated = int(re.search(r"allocated=(\d+)", stats)[1])
    profiles = []
    for name, label in (("primary", "Primary + post"), ("hard", "Hard shadows + post")):
        p = data["runs"][f"profile-{name}"]["gpu_frame"]
        profiles.append(f'<tr><th>{label}</th><td>{p["median_ms"]:.2f} ms</td>'
                        f'<td>{p["p95_ms"]:.2f} ms</td><td>{p["samples"]}</td></tr>')
    carving = data["runs"]["profile-carving"]
    builds = carving["field_build_ms"]
    prepare = carving.get("cpu_prepare_geometry")
    update_text = f'{builds["median_ms"]:.1f} ms median / {builds["p95_ms"]:.1f} ms p95 per field build'
    if prepare:
        update_text += f'; geometry-preparation CPU span p95 {prepare["p95_ms"]:.1f} ms'
    audition_data = json.loads((args.audition / "manifest.json").read_text())
    cards = []
    for seed in (7, 42, 91):
        for cuts in (24, 48, 72):
            row = audition_data["runs"][f"seed-{seed}-cuts-{cuts}"]
            image = "audition/" + row["capture"]
            chosen = ' <span class="tag">Chosen</span>' if (seed, cuts) == (42, 72) else ""
            cards.append(f'<figure><a href="{image}"><img loading="lazy" src="{image}" '
                         f'alt="Seed {seed} with {cuts} cuts"></a><figcaption>Seed {seed} · {cuts} cuts{chosen}'
                         '</figcaption></figure>')
    window_path = args.input / "windowed-interaction.json"
    window = json.loads(window_path.read_text()) if window_path.exists() else None
    window_text = ('Passed: ' + ', '.join(window["actions"])) if window else 'Not recorded in this export.'
    replacements = {
        "__DEVICE__": html.escape(data["device"]),
        "__COMMIT__": html.escape(data["source_commit"]),
        "__DIRTY__": "dirty" if data["source_dirty"] else "clean",
        "__RECIPE_HASH__": html.escape(recipe["hash"]),
        "__BRICKS__": f'{int(bricks[1]):,} / {int(bricks[2]):,}',
        "__MEMORY__": f'{allocated / 1024**3:.2f} GiB',
        "__ORACLE__": html.escape(oracle),
        "__GPU_ROWS__": "\n".join(profiles),
        "__UPDATE_TEXT__": html.escape(update_text),
        "__AUDITION__": "\n".join(cards),
        "__WINDOW__": html.escape(window_text),
        "__CHECKS__": html.escape(json.dumps(data["checks"], indent=2)),
        "__IMAGE_COMPARISONS__": html.escape(json.dumps(data["image_comparisons"], indent=2)),
    }
    template = Path(__file__).with_name("negative_report_template.html").read_text()
    for key, value in replacements.items():
        template = template.replace(key, value)
    if re.search(r"__[A-Z_]+__", template):
        raise RuntimeError("Unresolved report placeholder")
    (args.output / "index.html").write_text(template)
    print(args.output.resolve())


if __name__ == "__main__":
    main()
