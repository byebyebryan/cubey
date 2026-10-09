#!/usr/bin/env python3
"""HTTP and discovery regression tests; no GPU, tmux or external network."""

import hashlib
import os
import tempfile
import threading
import unittest
from functools import partial
from http.client import HTTPConnection
from http.server import ThreadingHTTPServer
from pathlib import Path

from report_server import ReportHandler, byte_range, discover_reports


class QuietHandler(ReportHandler):
    def log_message(self, *args):
        pass


class ReportServerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "outputs"
        self.root.mkdir()
        self.report = self.publish(
            "terrain/study/index.html",
            "<title>Terrain &amp; climate</title><h1>Verdict</h1>",
        )
        self.video = self.publish("terrain/study/movie.mp4", bytes(range(100)))
        self.server = ThreadingHTTPServer(
            ("127.0.0.1", 0), partial(QuietHandler, directory=self.root)
        )
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.addCleanup(self.stop_server)

    def stop_server(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)

    def publish(self, relative, data):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data if isinstance(data, bytes) else data.encode())
        return path

    def request(self, path, method="GET", headers=None):
        connection = HTTPConnection("127.0.0.1", self.server.server_port, timeout=3)
        try:
            connection.request(method, path, headers=headers or {})
            response = connection.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            connection.close()

    def test_index_title_search_controls_and_live_discovery(self):
        status, headers, body = self.request("/")
        self.assertEqual(status, 200)
        self.assertIn(b"Terrain &amp; climate", body)
        self.assertIn(b'type="search"', body)
        self.assertIn(b'data-project="terrain"', body)
        self.assertEqual(headers["Cache-Control"], "no-store")
        added = self.publish("fluid/new report/index.html", "<title>New runoff</title>")
        os.utime(added, (self.report.stat().st_mtime + 10,) * 2)
        self.assertIn(b"New runoff", self.request("/")[2])
        status, headers, _ = self.request("/latest")
        self.assertEqual(status, 302)
        self.assertEqual(headers["Location"], "/fluid/new%20report/index.html")
        self.assertEqual(self.request(headers["Location"])[0], 200)

    def test_navigation_is_response_only_and_relative_assets_work(self):
        before = hashlib.sha256(self.report.read_bytes()).hexdigest()
        status, _, body = self.request("/terrain/study/index.html")
        self.assertEqual(status, 200)
        self.assertIn(b'id="cubey-report-navigation"', body)
        self.assertIn(b'href="/"', body)
        self.assertEqual(hashlib.sha256(self.report.read_bytes()).hexdigest(), before)
        self.assertEqual(self.request("/terrain/study/")[2], body)
        self.assertEqual(
            self.request("/terrain/study/movie.mp4")[2], self.video.read_bytes()
        )
        status, headers, _ = self.request("/terrain/study")
        self.assertEqual(status, 301)
        self.assertEqual(headers["Location"], "/terrain/study/")

    def test_title_and_path_escape_untrusted_html(self):
        self.publish(
            'fluid/quote"report/index.html',
            "<title>&lt;script&gt;alert(1)&lt;/script&gt;</title>",
        )
        body = self.request("/")[2]
        self.assertIn(b"&lt;script&gt;alert(1)&lt;/script&gt;", body)
        self.assertNotIn(b"<script>alert(1)</script>", body)
        self.assertIn(b"quote%22report", body)
        self.assertIn(b"quote&quot;report", body)

    def test_no_traversal_hidden_files_external_symlinks_or_directory_listing(self):
        outside = self.root.parent / "outside.html"
        outside.write_text("secret", encoding="utf-8")
        self.publish(".hidden/data.txt", "secret")
        (self.root / "escape").symlink_to(outside)
        (self.root / "hidden-alias").symlink_to(self.root / ".hidden/data.txt")
        (self.root / "escape-directory").symlink_to(
            self.root.parent, target_is_directory=True
        )
        self.publish("plain/data.txt", "artifact")
        for path in (
            "/../outside.html",
            "/%2e%2e/outside.html",
            "/.hidden/data.txt",
            "/hidden-alias",
            "/escape",
            "/escape-directory/",
        ):
            self.assertEqual(self.request(path)[0], 403, path)
        self.assertEqual(self.request("/plain/")[0], 404)
        self.assertEqual(self.request("/missing")[0], 404)
        self.assertEqual(self.request("/%00")[0], 400)

    def test_discovery_ignores_archived_depth_and_symlinks(self):
        self.publish("fluid/study/review/index.html", "<title>Nested review</title>")
        self.publish(
            "fluid/study/final-source/projects/terrain/index.html",
            "<title>Archived app</title>",
        )
        self.publish(".internal/index.html", "<title>Hidden</title>")
        (self.root / "linked").symlink_to(self.report.parent, target_is_directory=True)
        paths = {report["path"] for report in discover_reports(self.root)}
        self.assertEqual(
            paths, {"terrain/study/index.html", "fluid/study/review/index.html"}
        )

    def test_video_byte_ranges_and_head(self):
        for value, expected in (
            ("bytes=10-19", bytes(range(10, 20))),
            ("bytes=95-", bytes(range(95, 100))),
            ("bytes=-5", bytes(range(95, 100))),
        ):
            status, headers, body = self.request(
                "/terrain/study/movie.mp4", headers={"Range": value}
            )
            self.assertEqual(status, 206)
            self.assertEqual(body, expected)
            self.assertEqual(int(headers["Content-Length"]), len(expected))
            self.assertIn("Content-Range", headers)
        status, headers, body = self.request(
            "/terrain/study/movie.mp4", method="HEAD", headers={"Range": "bytes=10-19"}
        )
        self.assertEqual(status, 206)
        self.assertEqual(headers["Content-Length"], "10")
        self.assertEqual(body, b"")
        status, headers, body = self.request(
            "/terrain/study/movie.mp4", headers={"Range": "bytes=100-"}
        )
        self.assertEqual(status, 416)
        self.assertEqual(headers["Content-Range"], "bytes */100")
        self.assertEqual(body, b"")
        status, headers, body = self.request("/terrain/study/index.html", method="HEAD")
        self.assertEqual(status, 200)
        self.assertGreater(int(headers["Content-Length"]), self.report.stat().st_size)
        self.assertEqual(body, b"")

    def test_empty_index_and_latest(self):
        self.report.unlink()
        self.assertIn(b"No reports found", self.request("/")[2])
        self.assertEqual(self.request("/latest")[0], 404)

    def test_range_validation(self):
        self.assertEqual(byte_range("bytes=0-999", 100), (0, 99))
        self.assertEqual(byte_range("bytes=-999", 100), (0, 99))
        for value in (
            "bytes=-0",
            "bytes=",
            "bytes=9-2",
            "bytes=-",
            "bytes=0-1,4-5",
            "foo=0-1",
        ):
            with self.assertRaises(ValueError):
                byte_range(value, 100)
        with self.assertRaises(ValueError):
            byte_range("bytes=0-", 0)


if __name__ == "__main__":
    unittest.main()
