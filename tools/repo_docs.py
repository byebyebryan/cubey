"""Shared stdlib navigation and committed-media checks for repository tooling."""
from __future__ import annotations

from collections import Counter
from html import unescape
from html.parser import HTMLParser
from pathlib import Path
import hashlib
import re
import struct
import subprocess
from urllib.parse import unquote, urlsplit

# These snapshots intentionally preserve links from their original locations.
# check_project_layout protects their bytes independently of navigation.
SEALED_NARRATIVES = {
    "studies/fluid_25d/harness/README_HISTORY.md",
    "studies/fluid_25d/harness/presentation_history.md",
}
GENERATED_ROOTS = {"outputs", "cache", "build"}


def slug(text):
    text = re.sub(r"<[^>]*>", "", unescape(text))
    return re.sub(r"\s", "-", re.sub(r"[^\w\s-]", "", text.lower().strip()))


def without_fenced_code(text):
    lines, fence, length = [], None, 0
    for line in text.splitlines(keepends=True):
        marker = re.match(r"^ {0,3}(`{3,}|~{3,})(.*)$", line)
        if fence:
            if marker and marker[1][0] == fence and len(marker[1]) >= length and not marker[2].strip():
                fence = None
            continue
        if marker:
            fence, length = marker[1][0], len(marker[1])
        else:
            lines.append(line)
    return "".join(lines)


class _HTMLLinks(HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = []
        self.anchors = set()

    def handle_starttag(self, tag, attrs):
        values = dict(attrs)
        for key in ("id", "name"):
            if values.get(key):
                self.anchors.add(values[key])
        field = "href" if tag == "a" else "src" if tag in {"img", "video", "source"} else None
        if field and values.get(field):
            self.links.append(values[field])


def anchors(text):
    prose = without_fenced_code(text)
    html = _HTMLLinks()
    html.feed(prose)
    result, counts = set(html.anchors), Counter()
    for heading in re.findall(r"^ {0,3}#{1,6}\s+(.+?)\s*#*\s*$", prose, re.M):
        base = slug(heading)
        candidate = base
        while candidate in result:
            counts[base] += 1
            candidate = f"{base}-{counts[base]}"
        result.add(candidate)
    return result


def local_links(source):
    prose = without_fenced_code(source.read_text())
    prose = re.sub(r"(`+)(?!`).*?\1(?!`)", "", prose, flags=re.S)
    # Inline links/images and reference definitions; optional quoted titles are
    # outside the captured target. This is the repo's Markdown subset, not a renderer.
    targets = re.findall(r'\]\((<[^>\n]+>|[^\s()]+)(?:\s+["\'][^\n]*?["\'])?\)', prose)
    targets += re.findall(r"^ {0,3}\[[^\]]+\]:\s*(<[^>\n]+>|\S+)", prose, re.M)
    html = _HTMLLinks()
    html.feed(prose)
    targets += html.links
    for target in targets:
        target = unescape(target.strip("<>"))
        parsed = urlsplit(target)
        if parsed.scheme or parsed.netloc:
            continue
        yield unquote(parsed.path), unquote(parsed.fragment), target


def destination(root, source, relative):
    path = (source.parent / relative).resolve() if relative else source.resolve()
    inside = path.relative_to(root.resolve())  # also rejects escaping symlinks
    return path, inside


def link_errors(root, source):
    errors = []
    for relative, anchor, target in local_links(source):
        try:
            path, inside = destination(root, source, relative)
        except ValueError:
            errors.append(f"{source.relative_to(root)}: link leaves repository: {target}")
            continue
        if inside.parts and (inside.parts[0] in GENERATED_ROOTS or inside.parts[0].startswith("build-")):
            continue
        if not path.exists():
            errors.append(f"{source.relative_to(root)}: missing link {target}")
        elif anchor and path.suffix == ".md" and anchor not in anchors(path.read_text()):
            errors.append(f"{source.relative_to(root)}: missing anchor {target}")
    return errors


def owned_markdown(root):
    # Do not walk ignored private-build dependencies or optional generated data.
    result = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--", "*.md"],
                            check=True, capture_output=True, text=True)
    return sorted(root / name for name in result.stdout.split("\0")
                  if name and not name.startswith("third_party/"))


def navigation(root, sources):
    root = root.resolve()
    names = {source.relative_to(root).as_posix(): source for source in sources}
    graph, errors = {}, []
    for name, source in names.items():
        graph[name] = set()
        if name not in SEALED_NARRATIVES:
            errors.extend(link_errors(root, source))
        for relative, _, _ in local_links(source):
            try:
                path, _ = destination(root, source, relative)
                if path.is_dir():
                    path /= "README.md"
                target = path.relative_to(root).as_posix()
            except ValueError:
                continue
            if target in names:
                graph[name].add(target)
    if "README.md" not in names:
        errors.append("tracked root README.md missing from documentation inventory")
    reached, pending = {"README.md"}, ["README.md"]
    while pending:
        for target in graph.get(pending.pop(), set()) - reached:
            reached.add(target)
            pending.append(target)
    return {"pages_checked": len(names), "errors": errors,
            "unreachable": sorted(set(names) - reached)}


def media_errors(root, descriptor):
    """Check one current asset, not historical hashes of superseded file paths."""
    errors = []
    relative = descriptor["path"]
    path, _ = destination(root, root / "README.md", relative)
    if not path.is_file():
        return [f"missing media: {relative}"]
    data = path.read_bytes()
    if len(data) != descriptor["bytes"]:
        errors.append(f"media size mismatch: {relative}")
    if hashlib.sha256(data).hexdigest() != descriptor["sha256"]:
        errors.append(f"media hash mismatch: {relative}")
    if path.suffix == ".png":
        if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n":
            errors.append(f"invalid PNG: {relative}")
        elif "width" in descriptor and struct.unpack_from(">II", data, 16) != (descriptor["width"], descriptor["height"]):
            errors.append(f"PNG dimensions mismatch: {relative}")
    elif path.suffix == ".mp4":
        offset, atoms = 0, []
        while offset < len(data):
            if offset + 8 > len(data):
                errors.append(f"truncated MP4 atom: {relative}")
                break
            size, kind = struct.unpack_from(">I4s", data, offset)
            header = 8
            if size == 1:
                if offset + 16 > len(data):
                    errors.append(f"truncated MP4 extended atom: {relative}")
                    break
                size, header = struct.unpack_from(">Q", data, offset + 8)[0], 16
            size = size or len(data) - offset
            if size < header or offset + size > len(data):
                errors.append(f"invalid MP4 atom size: {relative}")
                break
            atoms.append(kind)
            offset += size
        if b"moov" not in atoms or b"mdat" not in atoms or atoms.index(b"moov") > atoms.index(b"mdat"):
            errors.append(f"MP4 is not fast-start: {relative}")
    return errors
