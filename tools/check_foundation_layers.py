#!/usr/bin/env python3
"""Enforce the foundation's physical include boundaries, including private adapters."""
import argparse
import json
from pathlib import Path
import re

# Architecture policy, independent of the build's transitive link closure.
ALLOWED = {
    "core": set(), "asset": {"core"}, "animation": {"core", "asset"},
    "terrain": {"core", "asset"}, "vulkan": {"core"},
    "render": {"core", "vulkan"}, "scene": {"core", "render"},
    "input": {"core", "scene"},
    "engine": {"core", "asset", "animation", "terrain", "vulkan", "render", "scene"},
    "host": {"core", "asset", "animation", "terrain", "vulkan", "render", "scene", "engine", "input"},
}
ADAPTERS = {("include/cubey/scene/render_recording.h", "vulkan")}
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>\"]+)[>\"]', re.MULTILINE)


def module(path):
    parts = path.parts
    if len(parts) < 3 or parts[:2] not in (("src", "cubey"), ("include", "cubey")):
        return None
    return "core" if parts[2] == "procedural" else parts[2]


def check(root):
    root = root.resolve()
    errors = []
    files = sorted(p for base in (root / "include/cubey", root / "src/cubey")
                   for p in base.rglob("*") if p.suffix in {".cpp", ".h", ".hpp"})
    if not files:
        errors.append("no foundation source/header files found")
    for path in files:
        relative = path.relative_to(root)
        owner = module(relative)
        if owner not in ALLOWED:
            errors.append(f"{relative}: unknown foundation layer {owner}")
            continue
        source = path.read_text()
        for match in INCLUDE.finditer(source):
            name = match.group(1)
            line = source[:match.start()].count("\n") + 1
            prefix = f"{relative}:{line}: "
            if owner in {"core", "asset", "terrain", "animation"} and name.startswith(("vulkan/", "GLFW/")):
                errors.append(prefix + f"CPU layer {owner} includes native GPU/host header {name}")
            if owner == "scene" and name.startswith("vulkan/") and (relative.as_posix(), "vulkan") not in ADAPTERS:
                errors.append(prefix + f"scene code outside the recording adapter includes native Vulkan {name}")
            if owner not in {"core", "asset", "terrain", "animation", "host"} and name.startswith("GLFW/"):
                errors.append(prefix + f"only host may include GLFW: {name}")
            if name.startswith("cubey/"):
                target = Path("include") / name
            else:
                candidates = [(path.parent / name).resolve(), (root / name).resolve()]
                target = next((p.relative_to(root) for p in candidates
                               if p.is_relative_to(root) and (p.exists() or (len(p.parts) > len(root.parts) and p.parts[len(root.parts)] in {"src", "projects", "examples", "studies"}))), None)
            if target is None:
                continue
            if target.parts[0] in {"projects", "examples", "studies"}:
                errors.append(prefix + f"foundation includes product/study implementation {name}")
                continue
            if relative.parts[0] == "include" and target.parts[:2] == ("src", "cubey"):
                errors.append(prefix + f"public header includes private implementation {name}")
            dependency = module(target)
            if dependency is not None and dependency != owner and dependency not in ALLOWED[owner] and (relative.as_posix(), dependency) not in ADAPTERS:
                errors.append(prefix + f"forbidden dependency {owner} -> {dependency}: {name}")
    return files, errors


def check_target_graph(path):
    graph = json.loads(path.read_text())
    errors = []
    if not graph:
        errors.append("no configured foundation targets found")
    for owner, dependencies in graph.items():
        if owner not in ALLOWED:
            errors.append(f"unknown configured foundation layer {owner}")
            continue
        for dependency in dependencies:
            if dependency != owner and dependency not in ALLOWED[owner]:
                errors.append(f"configured target has forbidden dependency {owner} -> {dependency}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--target-graph", type=Path, help="Configured CMake foundation links")
    args = parser.parse_args()
    files, errors = check(args.root)
    if args.target_graph is not None:
        errors.extend(check_target_graph(args.target_graph))
    if errors:
        print("\n".join(errors))
        return 1
    print(f"PASS: foundation include boundaries ({len(files)} source/header files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
