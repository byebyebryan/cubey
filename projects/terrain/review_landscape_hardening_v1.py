#!/usr/bin/env python3
"""Frozen headless qualification of landscape availability and preparation fixes."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import xml.etree.ElementTree as ET

import review_prepared_landscapes_v1 as base

ROOT = base.ROOT
CASES = {"partial-auto", "partial-explicit", "changed-pin", "wrong-size",
         "unsafe-link", "unreadable", "legacy-options", "uniform-apply", "fixed-apply", "camera-invalid-apply"}


def support_inputs():
    fixture = ROOT / "tests/assets/terrain/backdrop-smoke"
    paths = [fixture / "heightfield.json", fixture / "elevation.f32"]
    for id in base.WET:
        site, candidate = base.prepare.WATER_LOCATIONS[id]
        paths.append(base.OLD / site / "visual" / candidate / "local-water.png")
    return {str(path.relative_to(ROOT)): base.render.digest(path) for path in paths}


def verify(out):
    frozen = base.verify(out)
    if frozen["driver_sha256"] != base.render.digest(Path(__file__)):
        raise ValueError("hardening driver changed; retain old evidence")
    if frozen["support_inputs"] != support_inputs():
        raise ValueError("compatibility/reference inputs changed")
    return frozen


def metadata_cache(out, name):
    """Own small copies only; never mutate, link to, or duplicate real payloads."""
    target = out / ("fixture-" + name)
    target.mkdir()
    for relative in base.identities():
        source = ROOT / relative
        if source.suffix == ".json" and source != base.CATALOG:
            destination = target / source.relative_to(ROOT / "cache/terrain")
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
    return target


def fixture_paths(cache):
    preset = json.loads(base.CATALOG.read_bytes())["presets"][0]
    site = cache / preset["site"]["path"]
    binding = cache / preset["water"]["path"]
    return site, site.parent / "elevation.f32", binding.parent / "depth.f32"


def frame(out, name, args):
    image = out / (name + ".png")
    receipt = base.checked(args + ["--frames", "1", "--width", "640", "--height", "360",
                                  "--output", str(image)], out / (name + ".log"))
    return image, receipt


def exact(a, b):
    difference = base.render.pixel_difference(a, b)
    if difference["max_abs_8bit"]:
        raise ValueError(f"startup/compatibility frame changed: {a.name}: {difference}")
    return difference


def apply_options(detail):
    # Explicit non-default controls must survive Apply; uniform's budget is
    # intentionally insufficient for fixed topology but irrelevant to uniform.
    return ["--terrain-water-detail", detail, "--terrain-water-vertex-budget",
            "2000000" if detail == "uniform" else "4500000",
            "--terrain-water-detail-focus-x", "11584", "--terrain-water-detail-focus-z", "19136",
            "--terrain-water-detail-span", "8192", "--terrain-water-span", "18000",
            "--terrain-water-yaw", ".2", "--terrain-water-pitch", "-.5",
            "--no-terrain-water-context", "--no-terrain-water-animate",
            "--terrain-readability", "detail-off"]


def compatibility(out):
    verify(out)
    rows = []
    partial = metadata_cache(out, "partial")
    automatic = base.command("auto") + ["--terrain-landscape-cache-root", str(partial)]
    image, receipt = frame(out, "partial-auto", automatic)
    if "not installed" not in (out / "partial-auto.log").read_text():
        raise ValueError("partial auto did not report fallback")
    legacy, control = frame(out, "partial-legacy", base.command("legacy"))
    rows.append({"case": "partial-auto", "difference": exact(image, legacy),
                 "run": receipt, "control": control})
    log = out / "partial-explicit.log"
    receipt = base.checked(base.command("mountain-local") + ["--terrain-landscape-cache-root",
                           str(partial)], log, 1)
    if "not installed" not in log.read_text():
        raise ValueError("partial explicit selection did not fail clearly")
    rows.append({"case": "partial-explicit", "run": receipt})
    for case, reason in (("changed-pin", "hash mismatch"), ("wrong-size", "wrong size"),
                         ("unsafe-link", "symlink rejected"), ("unreadable", "unreadable")):
        cache = metadata_cache(out, case)
        site, elevation, depth = fixture_paths(cache)
        if case == "changed-pin":
            with site.open("ab") as stream:
                stream.write(b"\n")
        elif case == "wrong-size":
            depth.write_bytes(b"bad")
        elif case == "unsafe-link":
            elevation.symlink_to(out / "nonexistent-outside-payload")
        else:
            site.chmod(0)
            if os.access(site, os.R_OK):
                raise ValueError("unreadable fixture not enforceable under this user")
        log = out / (case + ".log")
        receipt = base.checked(base.command("auto") + ["--terrain-landscape-cache-root", str(cache)], log, 1)
        text = log.read_text()
        if reason not in text or "NVIDIA GeForce" in text:
            raise ValueError(f"wrong availability rejection: {case}: {text}")
        rows.append({"case": case, "run": receipt})
    # Runtime legacy compatibility, in addition to portable config-layer tests.
    options = ["--terrain-heightfield", str(ROOT / "tests/assets/terrain/backdrop-smoke/heightfield.json"),
               "--terrain-seed", "7", "--terrain-placement", "raw-sample",
               "--no-clouds", "--terrain-backdrop-azimuth", "20"]
    automatic, receipt = frame(out, "legacy-auto", base.command("auto") + options)
    legacy, control = frame(out, "legacy-explicit", base.command("legacy") + options)
    if "bypassed" not in (out / "legacy-auto.log").read_text():
        raise ValueError("legacy options did not bypass automatic selection")
    rows.append({"case": "legacy-options", "difference": exact(automatic, legacy),
                 "run": receipt, "control": control})
    for detail in ("uniform", "fixed"):
        id = "mountain-wide" if detail == "uniform" else "mountain-local"
        args = base.command(id) + apply_options(detail) + ["--terrain-water-audit"]
        startup, receipt = frame(out, detail + "-startup", args)
        applied, control = frame(out, detail + "-apply", args + ["--terrain-landscape-smoke-sequence", id])
        first = (out / (detail + "-startup.log")).read_text()
        second = (out / (detail + "-apply.log")).read_text()
        prepared = re.findall(r"terrain_capture_prepared:[^\n]+", first + second)
        if len(prepared) != 3 or len(set(prepared)) != 1 or "terrain_landscape_applied:" not in second:
            raise ValueError("startup and Apply used different preparation controls")
        if "focus=(11584.000,19136.000) span=8192.000" not in prepared[0]:
            raise ValueError("explicit detail focus/span lost")
        if detail == "uniform" and ("terrain_fixed_lod" in first + second or "terrain_context" in first + second):
            raise ValueError("uniform/context-off allocated unwanted topology/context")
        if "terrain_water_capture_immutable: GPU bed/depth/decoded-velocity exact" not in first or (
                "terrain_water_capture_immutable: GPU bed/depth/decoded-velocity exact" not in second):
            raise ValueError("immutable fields not audited")
        rows.append({"case": detail + "-apply", "difference": exact(startup, applied),
                     "preparation": prepared[0], "run": receipt, "control": control})
    # This camera point belongs to wide water, but not the sharper local state.
    # A sticky explicit camera must fail before installation, even in uniform.
    args = base.command("mountain-wide") + ["--terrain-water-detail", "uniform",
        "--terrain-water-focus-x", "50000", "--terrain-water-focus-z", "19200",
        "--no-terrain-water-context", "--no-terrain-water-animate", "--terrain-water-audit"]
    log = out / "camera-invalid-apply.log"
    receipt = base.checked(args + ["--headless-render-only", "--frames", "1", "--width", "640",
        "--height", "360", "--terrain-landscape-smoke-sequence", "!mountain-local"], log)
    text = log.read_text()
    if "showing=mountain-wide reason=capture camera focus is outside the captured domain" not in text or (
            "terrain_landscape_applied:" in text or
            "terrain_water_capture_immutable: GPU bed/depth/decoded-velocity exact" not in text):
        raise ValueError("invalid camera Apply failed to retain old scene")
    rows.append({"case": "camera-invalid-apply", "run": receipt})
    base.write(out / "compatibility.json", {"rows": rows, "scope": "Independent copied metadata fixtures; real cache unchanged"})
    verify(out)


def qualification(out):
    frozen = verify(out)
    rows = json.loads((out / "compatibility.json").read_bytes())["rows"]
    if len(rows) != len(CASES) or {row["case"] for row in rows} != CASES:
        raise ValueError("incomplete/duplicate compatibility qualification")
    if any(row.get("difference", {}).get("max_abs_8bit", 0) for row in rows):
        raise ValueError("compatibility parity failed")
    tests = ET.parse(out / "regression-tests.xml").getroot().findall(".//testcase")
    if len(tests) != len(frozen["required_tests"]) or {t.get("name") for t in tests} != set(frozen["required_tests"]):
        raise ValueError("regression qualification missing required groups")
    if any(any(t.find(key) is not None for key in ("failure", "skipped", "error")) for t in tests):
        raise ValueError("regression qualification failed/skipped")
    return rows


def report(out):
    rows = qualification(out)
    base.report(out)
    page = out / "index.html"
    document = page.read_text().replace("Prepared terrain landscapes V1", "Prepared terrain hardening V1")
    section = """<section><h2>Hardening verdict</h2><p>Missing binary payloads now fall back only under explicit auto; named selections still fail clearly. Changed metadata, wrong-size payloads, unsafe links and unreadable metadata remain errors, even beside missing files. Uniform Apply no longer builds fixed topology or terrain context. Startup and Apply share one preparation routine and preserve explicit rendering controls.</p><p>Partial-cache fallback and explicit legacy controls are pixel-exact to legacy startup. Fixed and uniform Apply are pixel-exact to their equivalent startup, including custom focus/span, context-off and material controls. A camera point outside the newly selected capture is rejected before GPU installation, preserving the previous scene. CLI/config ownership tests cover zero/false assignments and file/CLI/set precedence.</p><p>No solver, assets, materials, live Fluid defaults or no-argument startup were changed. Original mountain / 32m is technically qualified for a later default decision; native GUI/layout and joint visual review remain deferred.</p><details><summary>Startup vs Apply controls</summary><div class="pair"><figure><img src="uniform-startup.png"><figcaption>Uniform startup</figcaption></figure><figure><img src="uniform-apply.png"><figcaption>Identical after Apply</figcaption></figure></div><div class="pair"><figure><img src="fixed-startup.png"><figcaption>Fixed custom-focus startup</figcaption></figure><figure><img src="fixed-apply.png"><figcaption>Identical after Apply</figcaption></figure></div></details><p><a href="compatibility.json">Ten hardening controls</a> · <a href="qualification.json">Qualification receipt</a></p></section>"""
    page.write_text(document.replace("<h1>Prepared terrain landscapes</h1>", "<h1>Prepared terrain hardening</h1>" + section))
    base.write(out / "qualification.json", {"compatibility_cases": len(rows), "tests": len(verify(out)["required_tests"]),
        "pixel_exact_pairs": sum("difference" in row for row in rows), "default_promoted": False,
        "native_gui_review": "deferred", "runtime": base.render.runtime(),
        "report_sha256": base.render.digest(page)})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--phase", required=True, choices=("freeze", "capture", "lifecycle", "negative", "compatibility", "performance", "report"))
    args = parser.parse_args()
    out = args.out.resolve()
    if args.phase == "freeze":
        base.prepare.prepare(base.CATALOG, ROOT / "cache/terrain")
        prior = ROOT / "outputs/terrain/prepared-landscapes-20261013-zYn3Vs/regression-tests.xml"
        tests = [t.get("name") for t in ET.parse(prior).getroot().findall(".//testcase")]
        tests += ["terrain_capture_preparation_cpu", "terrain_landscape_hardening_review_helpers"]
        base.write(out / "protocol.json", {"runtime": base.render.runtime(), "inputs": base.identities(),
            "harness_sha256": base.render.digest(Path(base.__file__)), "driver_sha256": base.render.digest(Path(__file__)),
            "support_inputs": support_inputs(),
            "required_tests": tests, "default_promoted": False,
            "scope": "Headless Vulkan only; no producer/solver. Native GUI/manual promotion deferred."})
    else:
        verify(out)
        if args.phase in ("compatibility", "report"):
            globals()[args.phase](out)
        else:
            getattr(base, args.phase)(out)
        verify(out)


if __name__ == "__main__":
    main()
