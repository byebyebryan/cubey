#!/usr/bin/env python3
"""Check supported project front doors, layout and local navigation (stdlib)."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re

from repo_docs import link_errors, owned_markdown, slug, without_fenced_code

ENTRY_SCRIPTS = {
    "projects/fluid/fluid_25d/run_terrain_rain_demo.py",  # compatibility only
    "projects/terrain_water/run_terrain_rain_demo.py",
    "projects/terrain_water/terrain_demo_session.py",
}
ROOT_DOCS = {"README.md", "THIRD_PARTY_NOTICES.md", "HISTORY.md",
             "current_reference.md", "demo_setup_v1.md", "integration.md"}
STUDY_BUILDS = {
    "cloud_ref": ("studies/cloud/terrainengine", "CUBEY_BUILD_CLOUD_STUDIES"),
    "planet": ("studies/planet", "CUBEY_BUILD_PLANET_STUDIES"),
}


def project_errors(root, folder):
    errors = []
    if not (folder / "README.md").is_file():
        errors.append(f"{folder.relative_to(root)}: missing README.md")
    for item in folder.iterdir():
        if not item.is_file():
            continue
        name = item.relative_to(root).as_posix()
        if item.suffix in {".py", ".sh", ".mjs"} and name not in ENTRY_SCRIPTS:
            errors.append(f"{name}: use owned tools/tests or indexed studies")
        if item.suffix == ".md" and item.name not in ROOT_DOCS:
            errors.append(f"{name}: put detail under docs or dated work under studies")
    return errors


def opt_in_study_errors(root, project, source, option):
    """Retained applications require an explicit study build, never a normal target."""
    errors = []
    if (root / f"projects/{project}/CMakeLists.txt").exists():
        errors.append(f"{project} belongs in {source}, not projects")
    projects = (root / "projects/CMakeLists.txt").read_text()
    if re.search(rf"add_subdirectory\(\s*{re.escape(project)}\s*\)", projects):
        errors.append(f"normal projects must not register {project}")
    cmake = (root / "CMakeLists.txt").read_text()
    if not re.search(rf'option\(\s*{option}\s*"[^"]*"\s*OFF\s*\)', cmake):
        errors.append(f"{project} studies must default OFF")
    if not re.search(rf"if\s*\(\s*{option}\s*\)\s*"
                     rf"add_subdirectory\({re.escape(source)}\)\s*endif\(\)", cmake):
        errors.append(f"{project} study registration must be gated by {option}")
    return errors


def relocation_errors(root, mapping):
    errors = []
    for old, new in mapping.items():
        if old == new or not (root / new).is_file():
            errors.append(f"missing/invalid relocation target: {old} -> {new}")
        elif new in mapping:
            errors.append(f"relocation target is not canonical: {old} -> {new}")
    # Successive cleanups may give one file several former names. These aliases
    # must point directly at its current file, not at an intermediate relocation.
    return errors


def audit(root):
    root = root.resolve()
    errors, rows, front_doors = [], [], set()
    # Only actual application roots, never ignored private-build dependencies.
    mains = list((root / "projects").glob("*/main.cpp"))
    mains += list((root / "projects/fluid").glob("*/main.cpp"))
    for main in sorted(mains):
        folder = main.parent
        if not (folder / "CMakeLists.txt").is_file():
            continue
        errors.extend(project_errors(root, folder))
        front_doors.add(folder / "README.md")
        front_doors.update((folder / "tools").glob("README.md"))
        files = [p for p in folder.iterdir() if p.is_file()]
        rows.append({"project": folder.relative_to(root).as_posix(),
                     "root_files": len(files),
                     "root_scripts": sum(p.suffix in {".py", ".sh", ".mjs"} for p in files)})
    # Discover documentation front doors from tracked sources, not a second
    # application/study roster. The full docs gate also checks historical notes.
    front_doors.update(p for p in owned_markdown(root)
                       if p.name == "README.md" or p.parent in {
                           root / "docs", root / "docs/architecture"})
    for p in sorted(front_doors):
        if not p.is_file():
            errors.append(f"{p.relative_to(root)}: missing front door")
        else:
            errors.extend(link_errors(root, p))
    for project, (source, option) in STUDY_BUILDS.items():
        errors.extend(opt_in_study_errors(root, project, source, option))
    builtin = root / "projects/fluid/fluid_25d/README.md"
    if builtin.is_file():
        text = builtin.read_text()
        if "github.com/user-attachments/" in text or "docs/media/showcase/fluid-25d" in text:
            errors.append("builtin Fluid 2.5D must not embed the Terrain Water showcase")
    moves = root / "studies/relocations.json"
    if moves.is_file():
        mapping = json.loads(moves.read_text())
        errors.extend(relocation_errors(root, mapping))
    snapshot = root / "studies/fluid_25d/harness/README_HISTORY.md"
    if not snapshot.is_file() or hashlib.sha256(snapshot.read_bytes()).hexdigest() != "126578a5596541e608f1d6588b4994ef3eade742147d62ddfa7c703f0fca290c":
        errors.append("sealed README narrative changed")
    presentation = root / "studies/fluid_25d/harness/presentation_history.md"
    marker = b"<!-- preserved-presentation-body -->\n\n"
    if not presentation.is_file():
        errors.append("sealed presentation narrative missing")
    else:
        raw = presentation.read_bytes()
        if raw.count(marker) != 1 or hashlib.sha256(raw.split(marker, 1)[-1]).hexdigest() != "408a21304edcfd773dc853741ded79658049680368fb5da47a9e591de02d181b":
            errors.append("sealed presentation narrative changed")
    return {"passed": not errors, "errors": errors, "projects": rows,
            "front_doors_checked": len(front_doors)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument("--json", action="store_true")
    a = p.parse_args()
    result = audit(a.root)
    print(json.dumps(result, indent=2) if a.json else
          ("PASS: project layout and current navigation" if result["passed"] else "\n".join(result["errors"])))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
