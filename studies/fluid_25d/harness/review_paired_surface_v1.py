#!/usr/bin/env python3
"""Bounded saved-state paired-surface review. Never starts a hydraulic worker."""
import argparse
import json
from pathlib import Path
import shutil

import review_terrain_presentation_v1 as reference


def capture(out, phase):
    root = out.resolve()
    if root.parent != reference.ROOT / "outputs/terrain" or not root.name.startswith("captured-water-paired-"):
        raise ValueError("owned captured-water-paired output directory required")
    directory = root / phase
    directory.mkdir()
    runtime = reference.runtime()
    inputs = reference.input_identity("mountain")
    frozen = directory / "fluid_25d"
    shutil.copy2(reference.APP, frozen)
    shutil.copytree(reference.SHADERS, directory / "shaders")
    rows = []
    for camera in ("overview", "slope", "basin"):
        path = directory / (camera + ".png")
        argv = reference.command("mountain", camera, "shaded", path, surface="correlated",
                                 shadow="scene", shadow_extent=2048)
        argv[2] = str(frozen)
        result = reference.run_capture(argv, path.with_suffix(".log"))
        if not path.is_file():
            raise ValueError("missing capture")
        rows.append(dict(camera=camera, path=path.name, sha256=reference.digest(path), **result))
    if runtime != reference.runtime() or inputs != reference.input_identity("mountain"):
        raise ValueError("runtime or source changed during capture")
    (directory / "manifest.json").write_text(json.dumps(dict(runtime=runtime, inputs=inputs,
        assets=rows, boundary="held rain-fed native state; Vulkan offscreen; no hydraulic work"), indent=2)+"\n")
    print(json.dumps(dict(phase=phase, captured=len(rows))))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--phase", required=True, choices=("baseline-fluid", "shared-fluid", "shared-scene-fluid", "shared-final-fluid", "final-engine-fluid"))
    args = parser.parse_args()
    capture(args.output, args.phase)
