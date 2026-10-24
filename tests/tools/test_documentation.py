"""Negative fixtures for shared docs/media rules and parser-only recipe execution."""
from pathlib import Path
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import check_documentation as check
import repo_docs as docs


class DocumentationRules(unittest.TestCase):
    def test_encoded_links_reference_titles_html_and_duplicate_headings(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "guide with spaces.md").write_text("# Same\n# Same\n<a id='explicit'></a>\n")
            source = root / "README.md"
            source.write_text('''[encoded](guide%20with%20spaces.md#same-1 "Title")
[reference][guide]
[guide]: <guide with spaces.md#explicit> "Title"
<a href="guide%20with%20spaces.md#same">HTML</a>
[bad](guide%20with%20spaces.md#same-2)
''')
            self.assertEqual(docs.link_errors(root, source), [
                "README.md: missing anchor guide%20with%20spaces.md#same-2"])

    def test_escaping_symlinks_rejected_and_generated_links_optional(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            root = folder / "repo"
            root.mkdir()
            (folder / "outside.md").write_text("# Outside")
            (root / "escape.md").symlink_to(folder / "outside.md")
            source = root / "README.md"
            source.write_text("[escape](escape.md) [generated](build-asan/a.md) [output](outputs/missing.html)")
            self.assertEqual(docs.link_errors(root, source), [
                "README.md: link leaves repository: escape.md"])

    def test_navigation_follows_directory_readmes_and_reports_unindexed_pages(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "docs").mkdir()
            (root / "README.md").write_text("[docs](docs)")
            (root / "docs/README.md").write_text("# Docs")
            (root / "orphan.md").write_text("# Orphan")
            report = docs.navigation(root, list(root.rglob("*.md")))
            self.assertEqual(report["errors"], [])
            self.assertEqual(report["unreachable"], ["orphan.md"])
            (root / "docs/README.md").write_text("[retained note](../orphan.md)")
            self.assertEqual(docs.navigation(root, list(root.rglob("*.md")))["unreachable"], [])

    def test_empty_navigation_inventory_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertTrue(docs.navigation(Path(tmp), [])["errors"])

    def test_shell_examples_ignore_other_languages_and_preserve_literals(self):
        text = "```cpp\nchar *message = \"unclosed;\n```\n```bash\nrtk proxy build/dev/projects/a/a --headless \\\n --width 3 # comment\n```\n"
        self.assertEqual(list(check.shell_commands(text))[0][1],
                         ["build/dev/projects/a/a", "--headless", "--width", "3"])

    def test_inventory_uses_tracked_files_without_walking_private_dependencies(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["git", "init", str(root)], check=True, capture_output=True)
            (root / "README.md").write_text("# Root")
            (root / "untracked.md").write_text("# Draft")
            (root / "private-builds").mkdir()
            (root / "private-builds/dependency.md").write_text("# Not owned")
            (root / "third_party").mkdir()
            (root / "third_party/README.md").write_text("# Dependency")
            subprocess.run(["git", "-C", str(root), "add", "README.md", "third_party/README.md"],
                           check=True, capture_output=True)
            self.assertEqual(docs.owned_markdown(root), [root / "README.md"])

    @staticmethod
    def asset(root, name, payload, **fields):
        (root / name).write_bytes(payload)
        return {"path": name, "bytes": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(), **fields}

    def test_media_rejects_hash_size_dimensions_and_invalid_atom_bounds(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            png = b"\x89PNG\r\n\x1a\n" + b"\0" * 8 + struct.pack(">II", 2, 3)
            descriptor = self.asset(root, "poster.png", png, width=2, height=3)
            self.assertEqual(docs.media_errors(root, descriptor), [])
            broken = dict(descriptor, bytes=1, sha256="0" * 64, width=4)
            self.assertEqual(len(docs.media_errors(root, broken)), 3)
            moov, mdat = struct.pack(">I4s", 8, b"moov"), struct.pack(">I4s", 8, b"mdat")
            self.assertEqual(docs.media_errors(root, self.asset(root, "clip.mp4", moov + mdat)), [])
            self.assertIn("MP4 is not fast-start: clip.mp4",
                          docs.media_errors(root, self.asset(root, "clip.mp4", mdat + moov)))
            invalid = moov + struct.pack(">I4s", 999, b"mdat")
            self.assertIn("invalid MP4 atom size: clip.mp4",
                          docs.media_errors(root, self.asset(root, "clip.mp4", invalid)))
            extended = struct.pack(">I4sQ", 1, b"moov", 16) + mdat
            self.assertEqual(docs.media_errors(root, self.asset(root, "clip.mp4", extended)), [])

    def test_retained_gallery_checks_assets_and_package_player_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            folder = root / "docs/media/showcase"
            folder.mkdir(parents=True)
            entries = []
            for name in ("first", "second"):
                video = struct.pack(">I4sI4s", 8, b"moov", 8, b"mdat")
                png = b"\x89PNG\r\n\x1a\n" + b"\0" * 8 + struct.pack(">II", 2, 3)
                publication = self.asset(root, f"{name}.mp4", video)
                publication["github_attachment_url"] = f"https://example.org/{name}"
                entries.append({"id": name, "publication": publication,
                                "poster": self.asset(root, f"{name}.png", png, width=2, height=3)})
            (folder / "manifest.json").write_text(json.dumps({"entries": entries}))
            urls = [entry["publication"]["github_attachment_url"] for entry in entries]
            (root / "README.md").write_text("# Cubey\n[Showcase](https://example.org/site/)")
            (folder / "README.md").write_text("\n".join(urls))
            self.assertEqual(check.check_media(root)["errors"], [])
            (folder / "README.md").write_text("\n".join(reversed(urls)))
            self.assertIn("media package native-player order differs from the manifest", check.check_media(root)["errors"])
            (folder / "README.md").write_text(urls[1])
            self.assertIn("missing native player in media README: first", check.check_media(root)["errors"])
            (folder / "README.md").write_text("\n".join(urls))
            (root / "first.mp4").write_bytes(b"corrupt")
            self.assertTrue(any("hash mismatch" in error for error in check.check_media(root)["errors"]))
            entries[0]["publication"]["github_attachment_url"] = ""
            (folder / "manifest.json").write_text(json.dumps({"entries": entries}))
            self.assertIn("invalid native-player URL: first", check.check_media(root)["errors"])

    def fake_app(self, root):
        source = root / "projects/ocean"
        source.mkdir(parents=True)
        (source / "main.cpp").write_text("// shared run_configured_app template early-exit")
        binary = root / "ocean"
        binary.write_text(f"#!{sys.executable}\n" + '''import sys
from pathlib import Path
if sys.argv[1:] == ["--help"]:
    raise SystemExit(0)
assert "--write-config-template" in sys.argv, "renderer would start"
if "--unknown" in sys.argv:
    raise SystemExit(2)
if "--config" in sys.argv:
    assert Path(sys.argv[sys.argv.index("--config") + 1]).is_file()
Path(sys.argv[sys.argv.index("--write-config-template") + 1]).write_text("{}")
''')
        binary.chmod(0o755)
        (root / "docs").mkdir()
        (root / "README.md").write_text("# Root")
        (root / "docs/getting-started.md").write_text('''```sh
rtk proxy build/release/projects/ocean/ocean --config ocean.json --set ocean.map_size=512
```
''')
        (source / "README.md").write_text('''```sh
./build/dev/projects/ocean/ocean --headless \\
  --output never-render.png
./build/dev/projects/ocean/ocean --help
build/dev/projects/ocean/cubey_project_ocean_tests --test-only
```
''')
        return [{"source": "projects/ocean", "binary": str(binary)}]

    def test_native_recipes_only_parse_and_use_configured_binary_paths(self):
        with tempfile.TemporaryDirectory(prefix="docs space ") as tmp:
            root = Path(tmp)
            apps = self.fake_app(root)
            result = check.check_recipes(root, apps)
            self.assertEqual(result, {"recipes_checked": 3, "recipes_selected": 3, "errors": []})
            self.assertFalse((root / "never-render.png").exists())

    def test_native_recipe_failures_are_not_silent_skips(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            apps = self.fake_app(root)
            guide = root / "projects/ocean/README.md"
            guide.write_text("```sh\nbuild/dev/projects/ocean/ocean --unknown\n```\n")
            self.assertIn("parser exit 2", check.check_recipes(root, apps)["errors"][0])
            guide.write_text("```sh\nbuild/dev/projects/ocean/ocean --output $(command)\n```\n")
            with self.assertRaisesRegex(ValueError, "nonliteral"):
                check.native_recipes(root, apps)
            guide.write_text("```sh\nbuild/dev/projects/ocean/ocean; echo chained\n```\n")
            with self.assertRaisesRegex(ValueError, "nonliteral"):
                check.native_recipes(root, apps)
            guide.write_text("```sh\nbuild/dev/projects/ocean/ocean\n```\n")
            (root / "projects/ocean/main.cpp").write_text("// no qualified early-exit")
            with self.assertRaisesRegex(ValueError, "unqualified"):
                check.native_recipes(root, apps)

    def test_missing_native_binaries_and_empty_selection_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            apps = self.fake_app(root)
            Path(apps[0]["binary"]).unlink()
            self.assertTrue(all("missing built executable" in error
                                for error in check.check_recipes(root, apps)["errors"]))
            self.assertEqual(check.check_recipes(root, [])["errors"],
                             ["no native documentation recipes selected"])


if __name__ == "__main__":
    unittest.main()
