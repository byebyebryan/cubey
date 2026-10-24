#!/usr/bin/env python3
"""Package the selected native material stories as a compact static presentation."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[3]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=ROOT / "outputs/material-presentation/qualification")
    parser.add_argument("--output", type=Path, default=ROOT / "outputs/material-presentation/report")
    args = parser.parse_args()
    source, target = args.input.resolve(), args.output.resolve()
    if target.exists() and any(target.iterdir()):
        raise ValueError("Choose an empty export directory; published presentation bundles are immutable")
    manifest = json.loads((source / "manifest.json").read_text())
    required = {"pbr-0", "pbr-4", "pbr-8", "sdf-0", "sdf-2.1", "sdf-3", "sdf-8",
                "pbr-master-1080p", "sdf-master-1080p", "profile-pbr", "profile-sdf"}
    if required != manifest["runs"].keys():
        raise ValueError("Incomplete presentation qualification")
    for run in manifest["runs"].values():
        if run["returncode"] or run["validation_errors"]:
            raise ValueError("Native qualification failed")
        if run["artifact"] and sha(source / run["artifact"]) != run["sha256"]:
            raise ValueError("Native artifact hash changed")
    for name, recipe in manifest["derived"].items():
        if (sha(source / name) != recipe["sha256"] or
                sha(source / recipe["source"]) != recipe["source_sha256"]):
            raise ValueError("Derived media hash changed")
    if manifest["derived"].keys() != {"pbr.mp4", "sdf.mp4", "pbr-poster.png", "sdf-poster.png"}:
        raise ValueError("Both gallery films and posters are required")
    windowed = source.parent / "windowed/windowed.json"
    window_checks = json.loads(windowed.read_text())
    if (set(window_checks) != {"pbr_materials", "sdf"} or
            not all(v.get("passed") is True for v in window_checks.values())):
        raise ValueError("Native window checks failed")
    controls = json.loads((source / "compatibility.json").read_text())
    if set(controls) != {"mesh-gallery", "sdf-wood-studio"}:
        raise ValueError("Both retained controls are required")
    for control in controls.values():
        if (control["returncode"] or control["validation_errors"] or not control["byte_identical"] or
                control["sha256"] != control["baseline_sha256"] or
                sha(source / control["artifact"]) != control["sha256"]):
            raise ValueError("Retained control has not passed")
    video = json.loads((source / "video-qa.json").read_text())
    if set(video) != {"pbr.mp4", "sdf.mp4", "pbr-master-1080p.mp4", "sdf-master-1080p.mp4"}:
        raise ValueError("Missing decoded-video checks")
    for result in video.values():
        streams = result["streams"]
        if (len(streams) != 1 or streams[0]["codec_name"] != "h264" or
                streams[0]["nb_read_frames"] != "480" or streams[0]["r_frame_rate"] != "60/1" or
                float(result["format"]["duration"]) != 8):
            raise ValueError("Video contract has not passed")
    target.mkdir(parents=True, exist_ok=True)
    for run in manifest["runs"].values():
        for name in (run["artifact"], run["log"]):
            if name:
                shutil.copy2(source / name, target / name)
    for name in manifest["derived"]:
        shutil.copy2(source / name, target / name)
    for name in ("manifest.json", "timings.json", "video-qa.json", "compatibility.json", "hardware.json"):
        shutil.copy2(source / name, target / name)
    for p in source.glob("profile-*.*"):
        shutil.copy2(p, target / p.name)
    shutil.copytree(source / "inputs", target / "inputs")
    shutil.copy2(windowed, target / "windowed.json")
    for name in ("tests-scoped.log", "scoped-recipes.json"):
        shutil.copy2(source.parent / name, target / name)
    timing = json.loads((source / "timings.json").read_text())
    page = Path(__file__).with_name("presentation_report.html").read_text()
    for renderer in ("pbr", "sdf"):
        page = page.replace(f"@@{renderer.upper()}_MS@@", f'{timing[f"profile-{renderer}"]["median_ms"]:.2f}')
    (target / "index.html").write_text(page)
    files = {str(p.relative_to(target)): dict(bytes=p.stat().st_size, sha256=sha(p))
             for p in sorted(target.rglob("*")) if p.is_file()}
    (target / "bundle.json").write_text(json.dumps(files, indent=2) + "\n")
    print(json.dumps(dict(output=str(target), files=len(files) + 1,
                         bytes=sum(v["bytes"] for v in files.values())), indent=2))


if __name__ == "__main__":
    main()
