# /// script
# dependencies = ["numpy==2.4.6", "pillow==12.0.0"]
# ///
"""Matched ordinary-renderer lighting/seed/sample controls; no renderer edits."""
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path("projects/raytracing/tools").resolve()))
from measure_dragon_glass import PRESET, capture, digest

root = Path("outputs/raytracing/dragon-glass-speckle-20261116").resolve()
root.mkdir(parents=True, exist_ok=True)
evidence = Path(__file__).resolve().parent
binary = Path("build/dev/projects/raytracing/raytracing").resolve()

def identity():
    return dict(
        commit=subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
        binary_sha256=digest(binary),
        shaders={p.name:digest(p) for p in sorted((binary.parent/"shaders").glob("*.spv"))},
        preset_sha256=digest(PRESET),
        model_sha256=digest(json.loads(PRESET.read_text())["model"]),
        recipes={p.name:digest(p) for p in sorted(Path("assets/materials").glob("*.json"))},
    )

pinned = identity()
pin_path = root / "identity.json"
if pin_path.exists():
    assert json.loads(pin_path.read_text()) == pinned
else:
    pin_path.write_text(json.dumps(pinned, indent=2) + "\n")

old = Path("outputs/raytracing/dragon-glass-smooth-default-20261116/captures/canonical-s19-smooth").resolve()
receipt = json.loads(Path(str(old)+".receipt.json").read_text())
assert receipt["identity"]["binary_sha256"] == pinned["binary_sha256"]
for suffix, sha in receipt["artifacts"].items():
    assert digest(str(old)+suffix) == sha
assert json.loads(Path(str(old)+".json").read_text())["config"]["smooth-normals"]
records = [dict(lighting="combined", samples=4096, seed=19, prefix=str(old), receipt=receipt)]
cases = [("combined", n, seed) for n in (128, 512) for seed in (19, 43)]
cases += [("combined", 4096, 43)]
cases += [(lighting, 4096, seed) for lighting in ("sky", "sun") for seed in (19, 43)]
cases += [("combined", 8192, seed) for seed in (19, 43)]
for lighting, samples, seed in cases:
    assert identity() == pinned, "rendering inputs changed"
    name = f"{lighting}-{samples}-s{seed}"
    r = capture(binary, root/"captures", name, 1920, 1080, samples, seed,
                ["--workshop-lighting", lighting], diagnostic_continue=True)
    assert identity() == pinned
    records.append(dict(lighting=lighting, samples=samples, seed=seed,
                        prefix=str(root/"captures"/name), receipt=r))
    (evidence/"captures.json").write_text(json.dumps(dict(identity=pinned, captures=records), indent=2)+"\n")
print("matched control captures complete", flush=True)
