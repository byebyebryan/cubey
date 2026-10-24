#!/usr/bin/env python3
"""Audit tracked Markdown, committed gallery integrity, or native recipe parsing."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

from repo_docs import media_errors, navigation, owned_markdown


def check_media(root):
    manifest = json.loads((root / "docs/media/showcase/manifest.json").read_text())
    entries = manifest["entries"]
    errors, paths, ids = [], set(), set()
    package = (root / "docs/media/showcase/README.md").read_text()
    positions = []
    if not entries:
        errors.append("empty showcase manifest")
    for entry in entries:
        if entry["id"] in ids:
            errors.append(f"duplicate showcase id: {entry['id']}")
        ids.add(entry["id"])
        for role in ("publication", "poster", "master"):
            if role not in entry:
                if role != "master":
                    errors.append(f"missing {role}: {entry['id']}")
                continue
            descriptor = entry[role]
            if descriptor["path"] in paths:
                errors.append(f"duplicate current media path: {descriptor['path']}")
            paths.add(descriptor["path"])
            errors.extend(media_errors(root, descriptor))
        url = entry["publication"]["github_attachment_url"]
        if not isinstance(url, str) or not url.startswith("https://"):
            errors.append(f"invalid native-player URL: {entry['id']}")
            continue
        if url not in package:
            errors.append(f"missing native player in media README: {entry['id']}")
        positions.append(package.find(url))
    if positions != sorted(positions):
        errors.append("media package native-player order differs from the manifest")
    return {"entries_checked": len(entries), "media_checked": len(paths), "errors": errors}


def shell_commands(text):
    """Read simple literal shell examples, without shell evaluation/expansion."""
    fence, length, buffer, start, shell = None, 0, "", 0, False
    for number, line in enumerate(text.splitlines(), 1):
        marker = re.match(r"^ {0,3}(`{3,}|~{3,})(.*)$", line)
        if marker:
            if fence is None:
                fence, length = marker[1][0], len(marker[1])
                shell = marker[2].strip() in {"", "sh", "bash", "shell"}
            elif marker[1][0] == fence and len(marker[1]) >= length and not marker[2].strip():
                fence, buffer = None, ""
            continue
        if fence is None or not shell:
            continue
        if not buffer:
            start = number
        continuation = line.rstrip().endswith("\\")
        buffer += line.rstrip().removesuffix("\\") + " "
        if continuation:
            continue
        try:
            lexer = shlex.shlex(buffer, posix=True, punctuation_chars=";&|<>")
            lexer.whitespace_split = True
            tokens = list(lexer)
        except ValueError as error:
            raise ValueError(f"invalid shell example at line {start}: {error}") from error
        buffer = ""
        if tokens[:2] == ["rtk", "proxy"]:
            tokens = tokens[2:]
        if tokens:
            yield start, tokens


def native_recipes(root, applications):
    apps = {(app["source"], Path(app["binary"]).name): app for app in applications}
    docs = {root / "README.md", root / "docs/getting-started.md"}
    docs.update(root / app["source"] / "README.md" for app in applications)
    recipes = []
    for doc in sorted(docs):
        if not doc.is_file():
            continue  # focused examples need not have project-style guides
        for line, tokens in shell_commands(doc.read_text()):
            match = re.fullmatch(r"(?:\./)?build/[^/]+/(.+)/([^/]+)", tokens[0])
            if not match or (match[1], match[2]) not in apps:
                continue  # other presets, test binaries and preparation tools
            # A template early-exit must be part of the native entry point before
            # this tool may execute it. Never infer safety from a binary name alone.
            app = apps[match[1], match[2]]
            main = root / app["source"] / "main.cpp"
            if "run_configured_app" not in main.read_text():
                raise ValueError(f"unqualified template early-exit: {main}")
            if any(token in {"|", "||", "&", "&&", ";", "<", ">", ">>", "<<"} or "$" in token or "`" in token
                   for token in tokens):
                raise ValueError(f"nonliteral native recipe: {doc}:{line}")
            recipes.append({"file": doc.relative_to(root).as_posix(), "line": line,
                            "binary": app["binary"], "args": tokens[1:]})
    return recipes


def check_recipes(root, applications):
    recipes = native_recipes(root, applications)
    errors, checked = [], 0
    if not recipes:
        errors.append("no native documentation recipes selected")
    with tempfile.TemporaryDirectory(prefix="cubey-doc-recipes-") as tmp:
        folder = Path(tmp)
        configs = {}
        for index, recipe in enumerate(recipes):
            binary, args = recipe["binary"], list(recipe["args"])
            label = f"{recipe['file']}:{recipe['line']}"
            if not Path(binary).is_file():
                errors.append(f"{label}: missing built executable: {binary}")
                continue
            # The setup guide's ocean.json is a user-owned example, not a
            # checkout asset. Substitute a fresh target-specific template.
            if args[:2] == ["--config", "ocean.json"] and recipe["file"] == "docs/getting-started.md":
                if binary not in configs:
                    config = folder / f"config-{index}.json"
                    generated = subprocess.run([binary, "--write-config-template", str(config)],
                                               cwd=root, capture_output=True, text=True, timeout=10)
                    if generated.returncode:
                        errors.append(f"{label}: template preparation failed: {generated.stderr.strip()}")
                        continue
                    configs[binary] = str(config)
                args[1] = configs[binary]
            if args != ["--help"]:
                args += ["--write-config-template", str(folder / f"parsed-{index}.json")]
            result = subprocess.run([binary, *args], cwd=root, capture_output=True, text=True, timeout=10)
            checked += 1
            if result.returncode:
                errors.append(f"{label}: parser exit {result.returncode}: {result.stderr.strip()}")
            elif args != ["--help"] and not (folder / f"parsed-{index}.json").is_file():
                errors.append(f"{label}: native template early-exit produced no template")
    return {"recipes_checked": checked, "recipes_selected": len(recipes), "errors": errors}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--check", choices=("navigation", "media", "recipes"), default="navigation")
    parser.add_argument("--app-manifest", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        if args.check == "navigation":
            result = navigation(root, owned_markdown(root))
            result["errors"] += [f"unindexed Markdown: {name}" for name in result["unreachable"]]
        elif args.check == "media":
            result = check_media(root)
        else:
            if args.app_manifest is None:
                parser.error("--check recipes requires --app-manifest")
            result = check_recipes(root, json.loads(args.app_manifest.read_text())["applications"])
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        result = {"errors": [str(error)]}
    result["passed"] = not result["errors"]
    print(json.dumps(result, indent=2) if args.json else
          (f"PASS: documentation {args.check}" if result["passed"] else "\n".join(result["errors"])))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
