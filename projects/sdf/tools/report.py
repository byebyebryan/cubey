#!/usr/bin/env python3
"""Assemble only the intended SDF review artifacts for static publication."""
import argparse
import html
import json
from pathlib import Path
import shutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=Path("outputs/sdf/qualification"))
    parser.add_argument("--output", type=Path, default=Path("outputs/sdf/review-export"))
    args = parser.parse_args()
    source, output = args.input.resolve(), args.output.resolve()
    if source == output or source in output.parents:
        raise ValueError("report export must be outside the qualification input tree")
    output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((source / "manifest.json").read_text())
    for name in ("captures", "logs", "profiles", "configs"):
        shutil.copytree(source / name, output / name, dirs_exist_ok=True)
    for name in ("manifest.json", "recipe.json", "presentation.mp4", "runtime-artifacts.json"):
        shutil.copy2(source / name, output / name)
    if (source/"windowed-interaction.json").exists():
        shutil.copy2(source/"windowed-interaction.json",output/"windowed-interaction.json")
    rows = []
    for key, label in (("primary", "Primary + atmosphere"), ("hard", "Hard shadows"),
                       ("soft", "Soft shadows · four rays"), ("soft-ao", "Soft shadows + local AO")):
        run = manifest["runs"][f"profile-{key}"]
        timing = run["gpu_frame"]
        rows.append(f'<tr><td>{label}</td><td>{timing["median_ms"]:.2f} ms</td>'
                    f'<td>{timing["p95_ms"]:.2f} ms</td><td>{timing["samples"]}</td>'
                    f'<td><a href="{html.escape(run["summary"])}">raw summary</a></td></tr>')
    template = Path(__file__).with_name("report_template.html").read_text()
    # The original review used a soft-shadow hero; newer runs capture it explicitly.
    soft_capture = "soft" if "soft" in manifest["runs"] else "hero"
    template = template.replace("__SOFT_CAPTURE__", soft_capture)
    template = template.replace("__PERFORMANCE_ROWS__", "\n".join(rows))
    template = template.replace("__COMMIT__", html.escape(manifest["source_commit"][:12]))
    runtime=json.loads((source/"runtime-artifacts.json").read_text())
    template=template.replace("__DEVICE__",html.escape(runtime["device"]))
    template=template.replace("__DRIVER__",html.escape(runtime["driver"]))
    if not (source/"windowed-interaction.json").exists():
        start=template.index('<div><img class="capture" src="captures/window-orbit.png"')
        end=template.index('<div class="card"><h3>Deliberate limits',start)
        template=template[:start]+'<div><p>Windowed input/resize evidence was not included in this capture run.</p></div>'+template[end:]
        template=template.replace('<a href="windowed-interaction.json">Input/resize receipt</a>','')
    (output / "index.html").write_text(template)
    print(output / "index.html")


if __name__ == "__main__":
    main()
