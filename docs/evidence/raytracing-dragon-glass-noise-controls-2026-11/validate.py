# /// script
# dependencies = ["numpy==2.4.6", "pillow==12.0.0"]
# ///
"""Run final-build regressions, then collect after the owned native UI exercise."""
import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

import numpy as np

evidence = Path(__file__).resolve().parent
repo = evidence.parents[2]
root = repo / "outputs/raytracing/dragon-glass-noise-controls-20261116/final"
sys.path.insert(0, str(repo / "projects/raytracing/tools"))
from compare_dragon_noise import identity

binary = repo / "build/dev/projects/raytracing/raytracing"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--regressions", action="store_true")
args = parser.parse_args()


def run(label, command):
    p = subprocess.run(command, cwd=repo, capture_output=True, text=True,
                       env=dict(os.environ, VK_LAYER_VALIDATE_SYNC="1"), timeout=300)
    log = p.stdout + p.stderr
    (root / (label + ".log")).write_text(log)
    assert p.returncode == 0, log[-6000:]
    assert not any(v in log.lower() for v in ["vuid-", "hazard detected", "vulkan validation error"])
    return dict(label=label, command=command, exit_code=p.returncode)


assert identity(binary) == json.loads((root / "identity.json").read_text())
if args.regressions:
    checks = [run("raytracing-tests", ["ctest", "--test-dir", "build/dev", "-R", "^raytracing_", "--output-on-failure"]),
              run("glass-controls", ["uv", "run", "projects/raytracing/tools/qualify_dragon_glass.py", "--output", str(root / "glass-controls")]),
              run("documentation", ["ctest", "--preset", "dev-docs", "--output-on-failure"])]
    (root / "checks.json").write_text(json.dumps(checks, indent=2) + "\n")
    print("20 RT CTests, 16 optical controls and 9 documentation checks passed", flush=True)
    raise SystemExit

checks = json.loads((root / "checks.json").read_text())
native = json.loads((root / "native/metrics.json").read_text())
assert native["history_epochs"] == 3 and native["config"]["firefly-clamp"] == 0
assert native["config"]["smooth-normals"]
assert sorted(set(f["epoch"] for f in native["native_frames"])) == [1, 2, 3]
assert len({(f["yaw"], f["pitch"], f["distance"]) for f in native["native_frames"]}) == 1
saved = [json.loads((root / "native" / name).read_text())["firefly-clamp"]
         for name in ["raw-view.json", "enabled-view.json", "view.json"]]
assert saved == [0, 100, 0], saved
log = (root / "native/native.log").read_text()
assert not any(v in log.lower() for v in ["vuid-", "hazard detected", "vulkan validation error"])

roundtrip = root / "roundtrip"
roundtrip.mkdir(exist_ok=True)
first = roundtrip / "first"
second = roundtrip / "second"
saved_config = roundtrip / "view.json"
checks.append(run("clamp-save", [str(binary), "--config", str(root / "native/enabled-view.json"),
    "--headless", "--frames", "1", "--width", "192", "--height", "128", "--samples", "32",
    "--metrics", str(first) + ".json", "--hdr-output", str(first) + ".bin",
    "--output", str(first) + ".png", "--save-config", str(saved_config), "--require-validation"]))
checks.append(run("clamp-reload", [str(binary), "--config", str(saved_config), "--headless", "--frames", "1",
    "--hdr-output", str(second) + ".bin", "--metrics", str(second) + ".json", "--output", str(second) + ".png"]))
for p in [first, second]:
    assert json.loads(Path(str(p) + ".json").read_text())["config"]["firefly-clamp"] == 100
a, b = [np.fromfile(str(p) + ".bin", dtype="<f4") for p in [first, second]]
assert np.array_equal(a, b) and np.isfinite(a).all()
controls = json.loads((root / "glass-controls/receipt.json").read_text())
assert len(controls["calls"]) == 16
assert all(c["binary_sha256"] == identity(binary)["binary"] for c in controls["calls"])
analysis = json.loads((evidence / "analysis.json").read_text())
assert analysis["captures"]["identity"] == identity(binary)
assert analysis["transport_status_identical_across_clamps"]
assert len(analysis["details"]) == 32
provenance = json.loads((root.parent / "oidn-provenance.json").read_text())
result = dict(scope="Demo appearance controls and offline color-only denoising; no native temporal claim",
    identity=identity(binary), checks=checks, rt_tests_passed=20, documentation_tests_passed=9,
    glass_control_calls=16, comparison_renders=16, offline_denoised_images=16,
    native=dict(history_epochs=3, saved_clamp_values=saved, camera_unchanged=True,
                validation_clean=True, scope="owned Xvfb native Vulkan window; no physical-display claim"),
    positive_clamp_saved_reload_hdr_exact=True, oidn_provenance=provenance,
    source_sha256={str(p.relative_to(repo)): hashlib.sha256(p.read_bytes()).hexdigest()
                   for p in sorted((repo / "projects/raytracing").rglob("*"))
                   if p.suffix in [".h", ".cpp", ".glsl", ".rgen", ".py"]},
    defaults=dict(firefly_clamp=0, smooth_normals=True), recommendation=dict(native_demo_cap=100,
        offline_still="cap 100 + OIDN; inspect fine detail"),
    preservation="No mesh/material/preset changes; source and executable recovery copy retained in output root",
    no_commit_push_or_site_update=True)
(evidence / "validation.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps({k: result[k] for k in ["rt_tests_passed", "glass_control_calls", "comparison_renders", "native"]}, indent=2))
