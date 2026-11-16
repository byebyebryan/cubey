"""Build/run the Q1 interface fixture in a complete private RT shader set.

The ordinary executable, SPIR-V, config and scene routes are never replaced.
The private source snapshot reuses --dielectric-contract only as a transport for
the Q1 probe. Its receipt explicitly identifies the substituted entry point.
"""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--fixture", choices=["interface", "flux"], default="interface")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    project = Path("projects/raytracing").resolve()
    source = out / "source"
    shutil.copytree(project, source, ignore=shutil.ignore_patterns("__pycache__"))
    trace = source / "shaders/trace.rgen"
    old = "if(camera.options.w>3.5){dielectricContractProbe();return;}"
    text = trace.read_text()
    assert text.count(old) == 1
    text = text.replace(
        "void main(){",
        '#include "optical_normal_experiment.glsl"\n#include "optical_normal_probe.glsl"\nvoid main(){',
    )
    trace.write_text(
        text.replace(old, "if(camera.options.w>3.5){q1NormalProbe();return;}")
    )
    fixture = (
        project
        / "tools/fixtures"
        / (
            "optical-normal-probe.glsl"
            if args.fixture == "interface"
            else "optical-normal-flux-probe.glsl"
        )
    )
    shutil.copy2(fixture, source / "shaders/optical_normal_probe.glsl")
    build_command = [
        sys.executable,
        "-B",
        str(project / "tools/build_ray_replay.py"),
        "--source",
        str(source),
        "--output",
        str(out / "build"),
    ]
    build = subprocess.run(build_command, check=False, capture_output=True, text=True)
    (out / "build.log").write_text(build.stdout + build.stderr)
    assert build.returncode == 0, build.stdout + build.stderr
    binary = out / "build/raytracing"
    command = [
        str(binary),
        "--headless",
        "--frames",
        "1",
        "--width",
        "512",
        "--height",
        "128",
        "--require-validation",
        "--dielectric-contract",
        "--optics-output",
        str(out / "probe.bin"),
        "--output",
        str(out / "probe.png"),
        "--metrics",
        str(out / "metrics.json"),
    ]
    run = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        env=dict(os.environ, VK_LAYER_VALIDATE_SYNC="1"),
        timeout=180,
    )
    log = run.stdout + run.stderr
    (out / "native.log").write_text(log)
    passed = run.returncode == 0 and not any(
        t in log.lower()
        for t in ["vuid-", "hazard detected", "vulkan validation error"]
    )
    result = {
        "passed": passed,
        "command": command,
        "exit_code": run.returncode,
        "fixture": "Q1 "
        + args.fixture
        + ", private substituted dielectric-contract entry point",
        "logical_extent": [512, 128],
        "record_bytes": 256,
        "binary_sha256": digest(binary),
        "shader_sha256": digest(out / "build/shaders/trace.rgen.spv"),
        "source_sha256": {
            str(p.relative_to(project)): digest(p)
            for p in [
                project / "shaders/optical_normal_experiment.glsl",
                fixture,
                Path(__file__).resolve(),
            ]
        },
    }
    if (out / "probe.bin").exists():
        result["probe_sha256"] = digest(out / "probe.bin")
    (out / "receipt.json").write_text(json.dumps(result, indent=2) + "\n")
    assert passed, log
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
