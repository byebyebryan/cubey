# /// script
# dependencies = ["numpy==2.4.6", "pillow==12.0.0"]
# ///
"""Private geometric/guarded-smooth dragon appearance comparison.

Uses two otherwise matched source/build snapshots. The normal executable,
shaders, CLI, presets and scene are never changed. Captures retain and report
transport flags as diagnostics; this is not a promotion/optical-accuracy gate.
"""

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

from measure_dragon_glass import VIEWS, capture, digest


def prepare(root, policy):
    source, build = root / ("source-" + policy), root / ("build-" + policy)
    shutil.copytree(root / "original-source", source)
    trace = source / "shaders/trace.rgen"
    text = trace.read_text()
    include = '#include "material_dielectric.glsl"'
    assert text.count(include) == 1
    trace.write_text(
        text.replace(
            include,
            include
            + '\n#include "optical_normal_experiment.glsl"\n'
            + "const bool RT_VISUAL_NORMAL_SMOOTH = "
            + ("true" if policy == "smooth" else "false")
            + ";",
        )
    )
    material = source / "shaders/material_workshop.glsl"
    text = material.read_text()
    old = """            vec3 n=faceforward(s.outward,direction,s.outward),v=-direction;
            mat3 frame=rtDirectionFrame(n);
            vec3 localV=transpose(frame)*v;
            float eta=medium>=0?1/boundaryRecipe.optics.z:boundaryRecipe.optics.z;"""
    new = """            vec3 n=faceforward(s.outward,direction,s.outward),v=-direction;
            float eta=medium>=0?1/boundaryRecipe.optics.z:boundaryRecipe.optics.z;
            if(RT_VISUAL_NORMAL_SMOOTH && environment.glass.w>.5 && m.roughGlass<.5 && eta!=1) {
                Instance normalInstance=instances[s.id];
                uint normalStart=normalInstance.offsets.y+3*s.primitive;
                vec3 rawNormal=vec3(0);
                for(uint corner=0u;corner<3u;++corner)
                    rawNormal+=vertices[indices[normalStart+corner]+normalInstance.offsets.x].normal.xyz*s.bary[corner];
                rawNormal=mat3(normalInstance.normalWorld)*rawNormal;
                if(!entering)rawNormal=-rawNormal;
                RtNormalInterface selected=rtGuardedNormalInterface(direction,n,rawNormal,eta,true);
                n=selected.normal;
            }
            mat3 frame=rtDirectionFrame(n);
            vec3 localV=transpose(frame)*v;"""
    assert text.count(old) == 1
    material.write_text(text.replace(old, new))
    (source / "visual-comparison.json").write_text(
        json.dumps(
            {
                "policy": policy,
                "scope": "private appearance preview",
                "integration": "Q1 guard chooses the interface normal; existing sharp dielectric sampler/Fresnel weights and RNG are retained",
                "unchanged": [
                    "mesh",
                    "materials",
                    "lighting",
                    "spawning",
                    "medium tracking",
                    "budgets",
                    "footprint model",
                ],
                "limitations": [
                    "Q1 direction discontinuities and energy errors remain",
                    "varying-normal filtering unqualified",
                    "no default/native integration",
                ],
            },
            indent=2,
        )
        + "\n"
    )
    cmd = [
        sys.executable,
        "-B",
        "projects/raytracing/tools/build_ray_replay.py",
        "--source",
        str(source),
        "--output",
        str(build),
    ]
    result = subprocess.run(cmd, check=False, capture_output=True, text=True)
    (root / ("build-" + policy + ".log")).write_text(result.stdout + result.stderr)
    assert result.returncode == 0, result.stdout + result.stderr
    print("built", policy, flush=True)


def identity(root):
    return {
        policy: {
            "binary": digest(root / ("build-" + policy) / "raytracing"),
            "shaders": {
                p.name: digest(p)
                for p in sorted((root / ("build-" + policy) / "shaders").glob("*.spv"))
            },
        }
        for policy in ["geometric", "smooth"]
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--suite", choices=["prepare", "pilot", "views", "isolated"], required=True
    )
    args = parser.parse_args()
    root = args.output.resolve()
    assert (root / "starting-state.json").is_file(), "preserve the starting state first"
    if args.suite == "prepare":
        for policy in ["geometric", "smooth"]:
            prepare(root, policy)
        a = json.loads((root / "build-geometric/build.json").read_text())
        b = json.loads((root / "build-smooth/build.json").read_text())
        assert a["libraries"] == b["libraries"], "paired foundation library drift"
        (root / "pair-identity.json").write_text(
            json.dumps(identity(root), indent=2) + "\n"
        )
        return
    pinned = json.loads((root / "pair-identity.json").read_text())
    assert identity(root) == pinned
    if args.suite == "pilot":
        cases = [("pilot", 960, 540, 128, 19, [])]
    elif args.suite == "views":
        cases = [
            (view + "-s19", 1920, 1080, 4096, 19, extra)
            for view, extra in VIEWS.items()
        ]
        cases.append(("canonical-s43", 1920, 1080, 4096, 43, []))
    else:
        cases = [
            (
                "isolated-s19",
                1920,
                1080,
                2048,
                19,
                ["--workshop-lighting", "sky", "--no-pattern", "--no-bump"],
            )
        ]
    receipts = []
    for name, width, height, samples, seed, extra in cases:
        for policy in ["geometric", "smooth"]:
            assert identity(root) == pinned, "paired build drift"
            receipts.append(
                {
                    "case": name,
                    "policy": policy,
                    "receipt": capture(
                        root / ("build-" + policy) / "raytracing",
                        root / "captures",
                        name + "-" + policy,
                        width,
                        height,
                        samples,
                        seed,
                        extra,
                        diagnostic_continue=True,
                    ),
                }
            )
    assert identity(root) == pinned
    (root / (args.suite + "-captures.json")).write_text(
        json.dumps(receipts, indent=2) + "\n"
    )


if __name__ == "__main__":
    main()
