#!/usr/bin/env python3
"""Enforce foundation includes and configured links, including private adapters."""
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
    """Check every configured variant, traversing non-foundation wrappers."""
    try:
        graph = json.loads(path.read_text())
        if graph.get("version") != 2:
            return ["foundation target graph has no supported configured-link schema"]
        modules = graph["modules"]
        aliases = graph["aliases"]
        unresolved = set(graph["unresolved_targets"])
        external = set(graph["external_targets"])
        configurations = graph["configurations"]
        if not modules or not configurations:
            return ["no configured foundation targets/configurations found"]
        errors = []
        for module in modules.values():
            if module not in ALLOWED:
                errors.append(f"unknown configured foundation layer {module}")
        for configuration, files in configurations.items():
            links = {}
            for target, relative in files.items():
                lines = (path.parent / relative).read_text().splitlines()
                if len(lines) != 2 or any("$<" in line for line in lines):
                    errors.append(f"{configuration}: unevaluated/invalid links for {target}")
                    continue
                links[target] = list(dict.fromkeys(
                    name.replace(r"\;", ";") for line in lines
                    for name in re.split(r"(?<!\\);", line)
                    if name and not name.startswith("::@")))
            for target, owner in modules.items():
                if owner not in ALLOWED:
                    continue
                if target not in links:
                    errors.append(f"{configuration}: missing configured target {target}")
                    continue
                pending = [(target, [])]
                visited = set()
                while pending:
                    node, route = pending.pop()
                    if node in visited:
                        continue
                    visited.add(node)
                    for dependency in links[node]:
                        actual = aliases.get(dependency, dependency)
                        layer = modules.get(actual)
                        if layer is not None:
                            if layer != owner and layer not in ALLOWED[owner]:
                                via = " via " + " -> ".join(route) if route else ""
                                errors.append(f"{configuration}: configured target has forbidden "
                                              f"dependency {owner} -> {layer}{via}")
                            # Each foundation boundary is checked independently;
                            # its permitted lower layers are not direct edges of
                            # the current owner. Transparent wrappers are.
                        elif actual in external:
                            continue
                        elif actual in links:
                            pending.append((actual, [*route, actual]))
                        elif actual in unresolved:
                            errors.append(f"{configuration}: cannot inspect directory-local target "
                                          f"{actual}; expose imported foundation dependencies globally")
                        elif actual.startswith("cubey::"):
                            errors.append(f"{configuration}: unresolved foundation target {actual}")
        return sorted(set(errors))
    except (OSError, ValueError, TypeError, KeyError, AttributeError) as error:
        return [f"invalid configured foundation target graph: {error}"]

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
    boundaries = "include/configured link" if args.target_graph else "include"
    print(f"PASS: foundation {boundaries} boundaries ({len(files)} source/header files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
