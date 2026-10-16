#!/usr/bin/env python3
"""Package the approved A clip. No GPU, solver, download, push or GitHub upload."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
MEDIA = ROOT / "docs/media/showcase"
OLD_VIDEO = "936661a15f5b49b915d5a09be8a0ae061e4a14b6a8a0b254f28268c285bb7bc2"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write(path, value):
    with path.open("x") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")


def probe(path):
    data = json.loads(subprocess.check_output(["rtk", "proxy", "ffprobe", "-v", "error", "-count_frames",
        "-show_streams", "-show_format", "-of", "json", str(path)]))
    streams = data["streams"]
    if len(streams) != 1 or streams[0]["codec_type"] != "video":
        raise ValueError("expected a silent single-stream video")
    return {k: streams[0].get(k) for k in ("codec_name", "profile", "pix_fmt", "width", "height",
        "r_frame_rate", "avg_frame_rate", "nb_read_frames", "duration")}


def check_video(info, width, height):
    if (info["width"], info["height"], info["nb_read_frames"], info["r_frame_rate"],
        info["avg_frame_rate"], info["codec_name"], info["profile"], info["pix_fmt"]) != (
            width, height, "480", "60/1", "60/1", "h264", "High", "yuv420p") or abs(float(info["duration"]) - 8) > .001:
        raise ValueError("publication video does not meet the 8s/480-frame/60fps contract")


def decoded_hash(path):
    return subprocess.check_output(["rtk", "proxy", "ffmpeg", "-v", "error", "-i", str(path),
        "-an", "-pix_fmt", "yuv420p", "-c:v", "rawvideo", "-f", "hash", "-hash", "sha256", "-"], text=True).strip()


def package(source, approved, out, install=False):
    protocol = json.loads((source / "protocol.json").read_bytes())
    capture = json.loads((source / "capture.json").read_bytes())
    raw = source / "source-1080p.mp4"
    if protocol["source_status_sha256"] != hashlib.sha256(b"").hexdigest() or digest(raw) != capture["sha256"]:
        raise ValueError("source was dirty or source film changed")
    if digest(approved) != protocol["approved_film_sha256"]:
        raise ValueError("approved comparison film changed")
    raw_pixels, approved_pixels = decoded_hash(raw), decoded_hash(approved)
    if raw_pixels != approved_pixels:
        raise ValueError("fresh clean-source frames differ from the owner-approved film")
    source_info = probe(raw)
    if (source_info["width"], source_info["height"], source_info["nb_read_frames"]) != (1920,1080,"480"):
        raise ValueError("incomplete native source")
    log = (source / "capture.log").read_text()
    if ("VUID-" in log or "Validation Error" in log or "GPU bed/depth/decoded-velocity exact" not in log or
        "hydraulic_dispatches=0" not in log or "mode=fitted" not in log):
        raise ValueError("source validation, depth fit or field audit failed")
    old_manifest = json.loads((MEDIA / "manifest.json").read_bytes())
    current = next(e for e in old_manifest["entries"] if e["id"] == "gltf-terrain")
    before_promotion = current["publication"]["sha256"] == OLD_VIDEO
    old = current if before_promotion else json.loads((ROOT / current["legacy"]["recipe"]).read_bytes())["entry"]
    if old["publication"]["sha256"] != OLD_VIDEO or (install and not before_promotion):
        raise ValueError("not the expected pre-promotion gallery; refusing replacement")
    if install:
        for descriptor in (old["publication"], old["poster"]):
            if digest(ROOT / descriptor["path"]) != descriptor["sha256"]:
                raise ValueError("old gallery has unreviewed changes")
    out.mkdir()
    master = out / "gltf-terrain-master-1080p.mp4"
    gallery = out / "gltf-terrain.mp4"
    poster = out / "gltf-terrain.png"
    master_cmd = ["rtk", "proxy", "ffmpeg", "-v", "error", "-n", "-i", str(raw), "-an",
        "-vf", "setpts=N/(60*TB)", "-r", "60", "-fps_mode", "cfr", "-frames:v", "480",
        "-c:v", "libx264", "-preset", "medium", "-crf", "20", "-pix_fmt", "yuv420p",
        "-movflags", "+faststart", str(master)]
    subprocess.run(master_cmd, check=True)
    gallery_cmd = ["rtk", "proxy", "ffmpeg", "-v", "error", "-n", "-i", str(master), "-an",
        "-vf", "scale=1280:720:flags=lanczos,setpts=N/(60*TB)", "-r", "60", "-fps_mode", "cfr",
        "-c:v", "libx264", "-preset", "medium", "-crf", "24", "-pix_fmt", "yuv420p",
        "-movflags", "+faststart", str(gallery)]
    subprocess.run(gallery_cmd, check=True)
    poster_cmd = ["rtk", "proxy", "ffmpeg", "-v", "error", "-n", "-i", str(gallery),
                  "-ss", "4", "-frames:v", "1", "-vf", "format=rgb24", str(poster)]
    subprocess.run(poster_cmd, check=True)
    master_info, gallery_info = probe(master), probe(gallery)
    check_video(master_info, 1920, 1080)
    check_video(gallery_info, 1280, 720)
    document = {
        "schema": "cubey.gltf.saved-water-showcase.v1", "selection": "Owner-approved A: original mountain, 32m saved water, morning",
        "source_protocol": protocol, "source_capture": capture, "source_probe": source_info,
        "approved_frame_parity": {"algorithm": "SHA256 of decoded raw yuv420p frames", "fresh": raw_pixels,
                                  "approved": approved_pixels, "all_480_frames_exact": True},
        "master": {"command": master_cmd, "probe": master_info, "native_resolution": True},
        "gallery": {"command": gallery_cmd, "probe": gallery_info, "derived_from": master.name},
        "poster": {"command": poster_cmd, "timestamp_seconds": 4},
        "scene": {"landscape": "mountain-local", "depth": "fitted", "water_state": "held 12h capture at 512mm/h storm forcing",
                  "hydraulic_dispatches": 0, "animated": "camera and render-only surface cues", "clouds": False,
                  "solar_hours": 10, "orbit_degrees": 20},
        "license": old["license"],
        "media": {p.name: {"path": "docs/media/showcase/" + p.name, "bytes": p.stat().st_size, "sha256": digest(p)}
                  for p in (master, gallery, poster)},
        "publication": "Owner approved and packaged locally; GitHub attachment and push are separate actions",
        "limitations": ["Fixed/small-orbit opaque/masked foreground above the terrain", "Saved state, not live hydrology or lake formation",
                        "Encoded 60fps does not assert native GUI performance", "Residual subpixel aliasing is not claimed eliminated"]}
    write(out / "gltf-terrain-recipe.json", document)
    write(out / "gltf-terrain-legacy-v1-recipe.json", {
        "schema": "cubey.gltf.showcase-legacy.v1", "status": "Historical dry-terrain clip, superseded by saved-water A",
        "repository_media_revision": protocol["source_revision"],
        "notice": "Paths/hashes in entry describe that historical revision, not current gallery files. The original GitHub attachment is retained unchanged.",
        "entry": old})
    if install:
        for name in (master.name, "gltf-terrain-recipe.json", "gltf-terrain-legacy-v1-recipe.json"):
            if (MEDIA / name).exists():
                raise ValueError("will not overwrite an existing master/recipe: " + name)
        for path in out.iterdir():
            shutil.copy2(path, MEDIA / path.name)
    print(json.dumps({"media": document["media"], "source_revision": protocol["source_revision"],
                      "decoded_frames_exact": True, "gallery_probe": gallery_info}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--approved-film", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--install", action="store_true", help="Replace only the reviewed glTF gallery video/poster; add master and recipes")
    args = parser.parse_args()
    package(args.source.resolve(strict=True), args.approved_film.resolve(strict=True), args.out.absolute(), args.install)
