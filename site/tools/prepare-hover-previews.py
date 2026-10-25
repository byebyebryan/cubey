"""Encode the small hover-preview pilot; optionally import its selected derivatives."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SITE = ROOT / 'site'
PLAN = [('water-3d', .8, 26), ('fire-3d', 2, 26), ('clouds', 1, 26),
        ('mesh-materials', 1.5, 26), ('ocean-surface', .5, 28)]
RECEIPT = 'site/media/receipts/hover-previews.json'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--import-media', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = SITE / 'media/manifest.json'
    media = json.loads(manifest.read_text())
    assets = {a['id']: a for a in media['assets']}
    receipt = {'scope': 'Silent excerpts of existing native films for pointer hover. '
                        'No new native capture or performance claim.', 'assets': {}}
    pending = []
    for entry, start, quality in PLAN:
        parent = assets[media['selections'][entry]['film']]
        source = SITE / 'public' / parent['file'].lstrip('/')
        if sha(source) != parent['sha256']:
            raise SystemExit('Parent film identity changed: ' + entry)
        identifier = 'preview-' + entry
        output = args.output / (identifier + '.mp4')
        command = ['ffmpeg', '-v', 'error', '-n', '-ss', str(start), '-i', str(source),
                   '-t', '3.5', '-an', '-vf', 'scale=640:360:flags=lanczos,fps=18',
                   '-c:v', 'libx264', '-preset', 'slow', '-crf', str(quality),
                   '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(output)]
        subprocess.run(command, check=True)
        probe = json.loads(subprocess.check_output(
            ['ffprobe', '-v', 'error', '-show_streams', '-show_format', '-of', 'json', str(output)]))
        stream = probe['streams'][0]
        if (len(probe['streams']) != 1 or stream['codec_name'] != 'h264' or
                stream['width'] != 640 or stream['height'] != 360 or
                output.stat().st_size > 400_000):
            raise SystemExit('Preview exceeds the pilot delivery contract: ' + entry)
        asset = {'id': identifier, 'file': '/media/' + output.name, 'kind': 'video',
                 'width': 640, 'height': 360, 'sha256': sha(output),
                 'bytes': output.stat().st_size,
                 'caption': 'Short silent preview. ' + parent['caption'],
                 'credit': parent['credit'],
                 'provenance': {'source': str(source.relative_to(ROOT)),
                                'sourceSha256': parent['sha256'],
                                'sourceRevision': parent['provenance'].get('sourceRevision'),
                                'parentAsset': parent['id'], 'recipe': RECEIPT,
                                'note': '640x360, 18 fps, 3.5-second excerpt of the selected '
                                        'web film. Full playback retains the original file.'}}
        receipt['assets'][identifier] = {
            'parentAsset': parent['id'], 'parentSha256': parent['sha256'],
            'startSeconds': start, 'durationSeconds': float(probe['format']['duration']),
            'sha256': asset['sha256'], 'bytes': asset['bytes'],
            'command': [s.replace(str(ROOT) + '/', '') for s in command]}
        if args.import_media:
            pending.append(output)
            assets[identifier] = asset
            media['selections'][entry]['preview'] = identifier
        print(json.dumps({'entry': entry, 'bytes': asset['bytes']}), flush=True)
    if args.import_media:
        for output in pending:
            shutil.copy2(output, SITE / 'public/media' / output.name)
        media['assets'] = sorted(assets.values(), key=lambda a: a['id'])
        manifest.write_text(json.dumps(media, indent=2) + '\n')
        (ROOT / RECEIPT).write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
