#!/usr/bin/env python3
"""Read-only, dependency-free browser for Cubey's local output reports.

Discover index.html pages on each index request. Add navigation only to HTTP
responses, never to sealed artifacts. Bind to loopback or a trusted Tailscale
address: this development server has no authentication or TLS.
"""

import argparse
import html
import io
import os
import re
import shutil
from datetime import datetime, timezone
from functools import partial
from html.parser import HTMLParser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import quote, unquote, urlsplit

DEFAULT_ROOT = Path(__file__).resolve().parents[1] / "outputs"


class TitleParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.in_title = False
        self.parts = []

    def handle_starttag(self, tag, attrs):
        if tag == "title":
            self.in_title = True

    def handle_endtag(self, tag):
        if tag == "title":
            self.in_title = False

    def handle_data(self, data):
        if self.in_title:
            self.parts.append(data)


def discover_reports(root):
    """Only report-level pages, not HTML buried in archived source trees."""
    reports = []
    for directory, children, files in os.walk(root, followlinks=False):
        directory = Path(directory)
        relative = directory.relative_to(root)
        children[:] = sorted(
            name
            for name in children
            if not name.startswith(".")
            and not (directory / name).is_symlink()
            and len(relative.parts) < 3
        )
        for name in ("index.html", "index.htm"):
            if name not in files or directory == root:
                continue
            path = directory / name
            if path.is_symlink():
                continue
            try:
                parser = TitleParser()
                with path.open(encoding="utf-8", errors="replace") as source:
                    parser.feed(source.read(131072))
                modified = path.stat().st_mtime
            except OSError:
                continue  # A producer can publish/remove a page during discovery.
            route = path.relative_to(root).as_posix()
            title = " ".join("".join(parser.parts).split())[:200]
            project = relative.parts[0] if len(relative.parts) >= 2 else "other"
            reports.append(
                {
                    "title": title or directory.name,
                    "path": route,
                    "url": "/" + quote(route, safe="/"),
                    "project": project,
                    "modified": modified,
                }
            )
            break
    return sorted(reports, key=lambda report: (-report["modified"], report["path"]))


INDEX_STYLE = """
*{box-sizing:border-box}body{margin:0;background:#101820;color:#e6edf3;
font:16px/1.5 system-ui,sans-serif}main{max-width:1120px;margin:0 auto;padding:48px 24px}
a{color:#9ed6ff}h1{font-size:clamp(30px,5vw,48px);margin:4px 0 12px;line-height:1.15}
.eyebrow{color:#8ea1b2;font-size:13px;letter-spacing:.12em;text-transform:uppercase}
.intro{max-width:700px;color:#acbac7;margin:0 0 28px}.toolbar{position:sticky;top:0;
background:#101820f5;padding:16px 0;z-index:1}input{width:100%;padding:13px 16px;
background:#1b2835;border:1px solid #40566b;border-radius:9px;color:inherit;font:inherit}
input:focus-visible,button:focus-visible,a:focus-visible{outline:3px solid #72c5ff;outline-offset:3px}
.filters{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}button{border:1px solid #40566b;
border-radius:20px;padding:6px 14px;background:transparent;color:#b6c5d2;font:inherit;cursor:pointer}
button[aria-pressed=true]{background:#254865;border-color:#72c5ff;color:white}
.count{color:#8ea1b2;font-size:14px}.reports{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,320px),1fr));gap:16px}
article{border:1px solid #304252;border-radius:12px;background:#192531;padding:20px;
display:flex;flex-direction:column;gap:10px;min-width:0}article:first-child{border-color:#63b9f2}
article h2{font-size:19px;line-height:1.3;margin:0}article h2 a{text-decoration:none}
article h2 a:hover{text-decoration:underline}.meta{display:flex;justify-content:space-between;
gap:8px;color:#a8bdcf;font-size:13px}.path{font:12px/1.6 ui-monospace,monospace;
color:#8ea1b2;overflow-wrap:anywhere;margin:0}.updated{font-size:12px;color:#8ea1b2;margin-top:auto}
footer{border-top:1px solid #304252;margin-top:32px;padding-top:18px;color:#8ea1b2;font-size:13px}
[hidden]{display:none!important}
"""


def render_index(reports):
    projects = sorted({report["project"] for report in reports})
    buttons = '<button type="button" data-project="" aria-pressed="true">All</button>'
    buttons += "".join(
        f'<button type="button" data-project="{html.escape(project)}" '
        f'aria-pressed="false">{html.escape(project.title())}</button>'
        for project in projects
    )
    cards = []
    for position, report in enumerate(reports):
        stamp = datetime.fromtimestamp(report["modified"], timezone.utc)
        newest = " · Newest" if position == 0 else ""
        search = html.escape((report["title"] + " " + report["path"]).lower())
        cards.append(f'''<article data-project="{html.escape(report["project"])}" data-search="{search}">
<div class="meta"><span>{html.escape(report["project"].title())}{newest}</span><span>HTML report</span></div>
<h2><a href="{report["url"]}">{html.escape(report["title"])}</a></h2>
<p class="path">{html.escape(report["path"])}</p>
<div class="updated">Updated <time datetime="{stamp.isoformat()}">{stamp:%Y-%m-%d %H:%M} UTC</time></div>
</article>''')
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cubey reports</title><style>{INDEX_STYLE}</style></head><body><main>
<div class="eyebrow">Cubey · Local review desk</div><h1>Reports &amp; results</h1>
<p class="intro">Rendering studies, comparisons and validation evidence. Newest pages first;
refresh to discover new reports. These are study artifacts, not automatic visual acceptance.</p>
<div class="toolbar"><label for="search" class="eyebrow">Find a report</label>
<input id="search" type="search" placeholder="Search titles, projects or dates…" autocomplete="off">
<nav class="filters" aria-label="Filter by project">{buttons}</nav></div>
<p class="count" id="count" aria-live="polite">{len(reports)} reports</p>
<section class="reports" aria-label="Reports">{"".join(cards)}</section>
<p id="empty" {"hidden" if reports else ""}>No reports found. Publish an index.html under outputs/&lt;project&gt;/&lt;report&gt;/.</p>
<footer>Read-only outputs browser · Updated means page modification time · Original evidence files remain untouched</footer>
</main><script>
const search=document.querySelector('#search'),cards=[...document.querySelectorAll('article')];
let project='';
function filter(){{let visible=0;const query=search.value.trim().toLowerCase();
for(const card of cards){{card.hidden=!!((project&&card.dataset.project!==project)||!card.dataset.search.includes(query));
if(!card.hidden)visible++;}}
document.querySelector('#count').textContent=visible+' of '+cards.length+' reports';
document.querySelector('#empty').hidden=visible!==0;}}
search.addEventListener('input',filter);
document.querySelectorAll('[data-project][aria-pressed]').forEach(button=>button.addEventListener('click',()=>{{
project=button.dataset.project;document.querySelectorAll('[aria-pressed]').forEach(b=>b.setAttribute('aria-pressed',b===button?'true':'false'));filter();}}));
</script></body></html>""".encode()


NAVIGATION = b"""
<nav id="cubey-report-navigation" aria-label="Report navigation">
<a href="/">\xe2\x86\x90 All reports</a><span aria-hidden="true"> \xc2\xb7 </span><a href="/latest">Latest report</a>
</nav><style>#cubey-report-navigation{position:fixed;bottom:16px;right:16px;z-index:99999;
padding:10px 16px;border:1px solid #688098;border-radius:9px;background:#152432f2;
box-shadow:0 3px 20px #0006;color:#d4e3ef;font:14px/1.5 system-ui,sans-serif}
#cubey-report-navigation a{color:#a8d9ff;text-decoration:none}
#cubey-report-navigation a:hover{text-decoration:underline}
#cubey-report-navigation a:focus-visible{outline:2px solid #a8d9ff;outline-offset:3px}</style>
"""


def add_navigation(data):
    closing = re.search(rb"</body\s*>", data, flags=re.IGNORECASE)
    offset = closing.start() if closing else len(data)
    return data[:offset] + NAVIGATION + data[offset:]


def byte_range(value, size):
    """One inclusive byte range, including suffix ranges, for video seeking."""
    match = re.fullmatch(r"bytes=(\d*)-(\d*)", value.strip())
    if not match or not size or not any(match.groups()):
        raise ValueError("invalid byte range")
    first, last = match.groups()
    if not first:
        length = int(last)
        if not length:
            raise ValueError("empty suffix range")
        return max(0, size - length), size - 1
    start = int(first)
    end = min(int(last), size - 1) if last else size - 1
    if start >= size or start > end:
        raise ValueError("unsatisfiable byte range")
    return start, end


class ReportHandler(SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def __init__(self, *args, directory, **kwargs):
        self.root = Path(directory).resolve()
        super().__init__(*args, directory=str(self.root), **kwargs)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        super().end_headers()

    def bytes_response(self, data, content_type="text/html; charset=utf-8"):
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        return io.BytesIO(data)

    def public_path(self, path):
        resolved = path.resolve()
        return resolved.is_relative_to(self.root) and not any(
            part.startswith(".") for part in resolved.relative_to(self.root).parts
        )

    def send_head(self):
        self.remaining = None
        try:
            route = unquote(urlsplit(self.path).path, errors="strict")
            parts = route.split("/")
            if any(part.startswith(".") for part in parts if part):
                self.send_error(403, "Hidden paths and traversal are not served")
                return None
            path = (self.root / route.lstrip("/")).resolve()
            if not self.public_path(path):
                self.send_error(403, "Path is outside public outputs")
                return None
        except (ValueError, OSError, RuntimeError):
            self.send_error(400, "Invalid path")
            return None
        if route in ("/", "/index.html"):
            return self.bytes_response(render_index(discover_reports(self.root)))
        if route == "/latest":
            reports = discover_reports(self.root)
            if not reports:
                self.send_error(404, "No reports yet")
                return None
            self.send_response(302)
            self.send_header("Location", reports[0]["url"])
            self.send_header("Content-Length", "0")
            self.end_headers()
            return None
        if path.is_dir():
            if not route.endswith("/"):
                self.send_response(301)
                self.send_header(
                    "Location",
                    "/" + quote(path.relative_to(self.root).as_posix(), safe="/") + "/",
                )
                self.send_header("Content-Length", "0")
                self.end_headers()
                return None
            path = next(
                (
                    path / name
                    for name in ("index.html", "index.htm")
                    if (path / name).is_file()
                ),
                path,
            )
            if not self.public_path(path):
                self.send_error(403, "Path is outside public outputs")
                return None
        try:
            source = path.open("rb")
        except OSError:
            self.send_error(404, "No report or artifact at this path")
            return None
        if path.suffix.lower() in (".html", ".htm"):
            with source:
                return self.bytes_response(add_navigation(source.read()))
        stat = os.fstat(source.fileno())
        start, end = 0, stat.st_size - 1
        if self.headers.get("Range"):
            try:
                start, end = byte_range(self.headers["Range"], stat.st_size)
            except ValueError:
                source.close()
                self.send_response(416)
                self.send_header("Content-Range", f"bytes */{stat.st_size}")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return None
            self.send_response(206)
            self.send_header("Content-Range", f"bytes {start}-{end}/{stat.st_size}")
        else:
            self.send_response(200)
        self.remaining = max(0, end - start + 1)
        source.seek(start)
        self.send_header("Content-Type", self.guess_type(str(path)))
        self.send_header("Content-Length", str(self.remaining))
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Last-Modified", self.date_time_string(stat.st_mtime))
        self.end_headers()
        return source

    def copyfile(self, source, destination):
        try:
            if self.remaining is None:
                shutil.copyfileobj(source, destination)
            else:
                while self.remaining:
                    data = source.read(min(65536, self.remaining))
                    if not data:
                        break
                    destination.write(data)
                    self.remaining -= len(data)
        except (BrokenPipeError, ConnectionResetError):
            pass  # Browsers commonly abort a request when seeking video.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=DEFAULT_ROOT)
    parser.add_argument(
        "--bind", default="127.0.0.1", help="Loopback or trusted Tailscale address"
    )
    parser.add_argument("--port", type=int, default=8001)
    args = parser.parse_args()
    root = args.directory.resolve()
    if not root.is_dir():
        parser.error(f"outputs directory does not exist: {root}")
    server = ThreadingHTTPServer(
        (args.bind, args.port), partial(ReportHandler, directory=root)
    )
    print(f"Cubey reports: http://{args.bind}:{server.server_port}/", flush=True)
    print(
        f"Read-only root: {root}; no authentication; trusted network only", flush=True
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
