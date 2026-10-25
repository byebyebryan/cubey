"""One-time editorial import; film imports also prepare their hover preview."""
from pathlib import Path
import argparse
import copy
import hashlib
import importlib.util
import json
import shutil
import tempfile
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SITE = ROOT / 'site'
MANIFEST = SITE / 'media/manifest.json'
PUBLIC = SITE / 'public/media'
PREPARER = SITE / 'tools/prepare-hover-previews.py'


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def load_preparer():
    spec = importlib.util.spec_from_file_location('cubey_hover_previews', PREPARER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('id')
parser.add_argument('source', type=Path)
parser.add_argument('--entry')
parser.add_argument('--role', choices=['poster', 'film', 'gallery'])
parser.add_argument('--caption', required=True)
parser.add_argument('--credit', default='Cubey original media · MIT')
parser.add_argument('--credit-url', default='https://github.com/byebyebryan/cubey/blob/main/LICENSE')
parser.add_argument('--author', default='Bryan')
parser.add_argument('--source-url')
parser.add_argument('--revision', default='')
parser.add_argument('--recipe', default='')
parser.add_argument('--note', default='')
args = parser.parse_args()

src = args.source.resolve()
if not src.is_file():
    raise SystemExit('Missing selected input: ' + str(src))
if args.role == 'film' and not args.entry:
    raise SystemExit('--role film requires --entry so its preview can be selected automatically')

manifest = json.loads(MANIFEST.read_text())
video = src.suffix.lower() == '.mp4'
if args.role == 'film' and not video:
    raise SystemExit('--role film requires an MP4 input')
dest = PUBLIC / (args.id + ('.mp4' if video else '.webp'))

credit = {'label': args.credit, 'url': args.credit_url, 'author': args.author}
if args.source_url:
    credit['source'] = args.source_url
provenance = {
    'source': str(src.relative_to(ROOT)) if src.is_relative_to(ROOT) else str(src),
    'sourceSha256': sha(src),
    'sourceRevision': args.revision or None,
    'recipe': args.recipe,
    'note': args.note,
}

if video:
    preparer = load_preparer()
    try:
        inspected = preparer.probe(src)
    except (RuntimeError, ValueError) as error:
        raise SystemExit(str(error)) from error
    if inspected['duration'] <= 0 or inspected['width'] <= 0 or inspected['height'] <= 0:
        raise SystemExit('Film has invalid duration or dimensions: ' + str(src))
    width, height = inspected['width'], inspected['height']
else:
    try:
        info = json.loads(subprocess.check_output(
            ['node', str(SITE / 'tools/prepare-image.mjs'), str(src), str(dest)]))
    except subprocess.CalledProcessError as error:
        raise SystemExit('Image preparation failed: ' + str(error)) from error
    width, height = info['width'], info['height']

asset = {
    'id': args.id,
    'file': '/media/' + dest.name,
    'kind': 'video' if video else 'image',
    'width': width,
    'height': height,
    'sha256': sha(src),
    'bytes': src.stat().st_size,
    'caption': args.caption,
    'credit': credit,
    'provenance': provenance,
}

if video and args.entry and args.role == 'film':
    candidate = copy.deepcopy(manifest)
    candidate['assets'] = [item for item in candidate['assets'] if item['id'] != args.id] + [asset]
    selection = candidate['selections'].setdefault(args.entry, {})
    selection['film'] = args.id
    candidate['assets'].sort(key=lambda item: item['id'])
    with tempfile.TemporaryDirectory(prefix='cubey-film-import-') as stage_name:
        stage = Path(stage_name)
        staged_film = stage / dest.name
        shutil.copy2(src, staged_film)
        if sha(staged_film) != asset['sha256']:
            raise SystemExit('Film changed while staging; no selected media was updated')
        try:
            candidate, receipt, outputs = preparer.prepare(
                candidate, stage, [args.entry], source_paths={args.id: staged_film})
            preparer.commit(candidate, receipt, outputs, extra_files=[(staged_film, dest)])
        except (RuntimeError, OSError, ValueError) as error:
            raise SystemExit(str(error)) from error
    preview_id = candidate['selections'][args.entry]['preview']
    preview = next(item for item in candidate['assets'] if item['id'] == preview_id)
    print(json.dumps({'id': args.id, 'kind': 'video', 'bytes': asset['bytes'],
                      'entry': args.entry, 'role': args.role,
                      'preview': preview_id, 'previewBytes': preview['bytes']}))
    raise SystemExit(0)

# Poster/gallery imports retain their existing editorial workflow.
if video:
    shutil.copy2(src, dest)
else:
    # prepare-image writes the selected WebP and its responsive thumbnail.
    pass

if not video:
    asset['sha256'] = sha(dest)
    asset['bytes'] = dest.stat().st_size
    if width > 640:
        thumbnail = PUBLIC / (args.id + '-640.webp')
        asset.update(thumbnail='/media/' + thumbnail.name, thumbnailSha256=sha(thumbnail),
                     thumbnailBytes=thumbnail.stat().st_size)

manifest['assets'] = [item for item in manifest['assets'] if item['id'] != args.id] + [asset]
if args.entry:
    selection = manifest['selections'].setdefault(args.entry, {})
    if args.role == 'gallery':
        selection.setdefault('gallery', []).append(args.id)
    elif args.role:
        selection[args.role] = args.id
manifest['assets'].sort(key=lambda item: item['id'])
MANIFEST.write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps({'id': args.id, 'kind': asset['kind'], 'bytes': asset['bytes'],
                  'entry': args.entry, 'role': args.role}))
