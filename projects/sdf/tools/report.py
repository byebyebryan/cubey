#!/usr/bin/env python3
"""Assemble only the intended SDF review artifacts for static publication."""
import argparse
import html
import json
from pathlib import Path
import re
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
    hero_config = json.loads((source / manifest["runs"]["hero"]["config"]).read_text())
    benchmark_config = json.loads((source / manifest["runs"]["profile-hard"]["config"]).read_text())
    if hero_config["ground"]:
        presentation_note = "The presentation uses a ground receiver lit by the shared atmosphere."
        hard_description = "Hard shadows · one visibility ray toward the sun, on the sculpture and ground receiver."
    else:
        presentation_note = ('Hard self-shadows, no ground plane: the background is Cubey’s shared '
            'atmosphere sky. The earlier downward-facing capture was filled by the receiver plane, '
            'hiding the sky. <a href="http://starship:8787/reports/7f1fd699ff363f13294391b0917eac5e1ba9e98f348aaedced78de93d4142211/">'
            'View the earlier ground-plane study</a>.')
        hard_description = ('Hard self-shadows · one visibility ray toward the sun. Shared atmosphere '
            'sunlight and diffuse sky irradiance light the sculpture; the sky fills the background.')
    template = template.replace("__PRESENTATION_NOTE__", presentation_note)
    template = template.replace("__HARD_DESCRIPTION__", hard_description)
    template = template.replace("__GROUND_STATE__", "enabled" if benchmark_config["ground"] else "disabled")
    hero_log = (source / manifest["runs"]["hero"]["log"]).read_text()
    for marker, key in (("__BUILD_MS__", "build_ms"), ("__FILTER_MS__", "filter_gpu_ms"),
                        ("__BRICK_MS__", "brick_gpu_ms")):
        value = re.search(rf"\b{key}=([0-9.]+)", hero_log)
        if not value:
            raise ValueError(f"hero receipt is missing {key}")
        template = template.replace(marker, f"{float(value[1]):.1f}")
    ambiguities = re.search(r"\bsampling_ambiguous=(\d+)", hero_log)
    if not ambiguities:
        raise ValueError("hero receipt is missing the sampling ambiguity count")
    template = template.replace("__AMBIGUITIES__", ambiguities[1])
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
