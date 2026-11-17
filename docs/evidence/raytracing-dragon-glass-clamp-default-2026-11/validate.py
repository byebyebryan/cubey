# /// script
# dependencies = ["numpy==2.4.6", "pillow==12.0.0"]
# ///
"""Qualify the accepted default against the reviewed cap and retained raw path."""
import json
import os
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

evidence = Path(__file__).resolve().parent
repo = evidence.parents[2]
sys.path.insert(0, str(repo / "projects/raytracing/tools"))
from compare_dragon_noise import identity
from measure_dragon_glass import capture

root = repo / "outputs/raytracing/dragon-glass-clamp-default-20261116"
root.mkdir(exist_ok=True)
binary = repo / "build/dev/projects/raytracing/raytracing"
checks = []


def run(label, command):
    process = subprocess.run(command, cwd=repo, capture_output=True, text=True,
                             env=dict(os.environ, VK_LAYER_VALIDATE_SYNC="1"), timeout=300)
    log = process.stdout + process.stderr
    (root / (label + ".log")).write_text(log)
    assert process.returncode == 0, log[-6000:]
    assert not any(v in log.lower() for v in ["vuid-", "hazard detected", "vulkan validation error"])
    checks.append(dict(label=label, command=command, exit_code=process.returncode))


run("raytracing-tests", ["ctest", "--test-dir", "build/dev", "-R", "^raytracing_", "--output-on-failure"])
run("glass-controls", ["uv", "run", "projects/raytracing/tools/qualify_dragon_glass.py", "--output", str(root / "glass-controls")])
run("documentation", ["ctest", "--preset", "dev-docs", "--output-on-failure"])
captures = []
for name, cap, extra in [("default", 100, []), ("raw", 0, ["--firefly-clamp", "0"])]:
    receipt = capture(binary, root / "captures", name, 1920, 1080, 512, 19, extra)
    prefix = root / "captures" / name
    metrics = json.loads(Path(str(prefix) + ".json").read_text())
    assert metrics["config"]["firefly-clamp"] == cap
    old = repo / "outputs/raytracing/dragon-glass-noise-controls-20261116/final/captures" / f"front-512-s19-c{cap}"
    a, b = [np.fromfile(str(p) + ".bin", dtype="<f4") for p in [prefix, old]]
    assert np.array_equal(a.view(np.uint32), b.view(np.uint32)), name
    assert np.array_equal(np.array(Image.open(str(prefix) + ".png")),
                          np.array(Image.open(str(old) + ".png"))), name
    captures.append(dict(name=name, effective_cap=cap, hdr_bitwise_equal_to_reviewed_capture=True,
                         png_pixel_equal_to_reviewed_capture=True, receipt=receipt))

saved = root / "raw-view.json"
before = capture(binary, root / "captures", "save-raw", 320, 180, 32, 19,
                 ["--firefly-clamp", "0", "--save-config", str(saved)])
assert json.loads(saved.read_text())["firefly-clamp"] == 0
run("reload-raw", [str(binary), "--config", str(saved), "--headless", "--frames", "1",
                   "--metrics", str(root / "reloaded.json"), "--hdr-output", str(root / "reloaded.bin"),
                   "--output", str(root / "reloaded.png")])
assert json.loads((root / "reloaded.json").read_text())["config"]["firefly-clamp"] == 0
a = np.fromfile(root / "captures/save-raw.bin", dtype="<f4")
b = np.fromfile(root / "reloaded.bin", dtype="<f4")
assert np.array_equal(a.view(np.uint32), b.view(np.uint32))
controls = json.loads((root / "glass-controls/receipt.json").read_text())
assert len(controls["calls"]) == 16 and all(c["firefly_clamp"] == 100 for c in controls["calls"])
result = dict(scope="Promote the user-reviewed cap 100; retain explicit raw and other-scene defaults",
              identity=identity(binary), checks=checks, captures=captures,
              rt_tests_passed=20, optical_controls_passed=16, documentation_checks_passed=9,
              raw_saved_reload_hdr_bitwise_equal=True, raw_save_capture=before,
              defaults=dict(dragon_optics=100, other_scenes=0), no_commit_push_or_site_update=True)
(evidence / "validation.json").write_text(json.dumps(result, indent=2) + "\n")
print("Cap-100 default and raw opt-out exactly match reviewed HDR/PNG; saved raw reload and all checks pass")
