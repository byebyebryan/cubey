"""Prepare site derivatives from the qualified three-renderer material captures."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[2]
SITE = ROOT / 'site'
RECEIPT = 'site/media/receipts/raytracing-materials.json'
LABELS = {'mesh': 'Forward PBR', 'sdf': 'SDF ray marching', 'rt': 'Hardware RT'}


def read(path):
    return json.loads(path.read_text())


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--qualified-bundle', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--import-media', action='store_true')
    args = parser.parse_args()
    native, bundle, output = args.native.resolve(), args.qualified_bundle.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    manifest = read(SITE / 'media/manifest.json')
    accepted = read(bundle / 'artifact-manifest.json')
    films = read(native / 'films/receipt.json')
    stills = read(native / 'stills/receipt.json')
    if not films['passed'] or not stills['passed'] or films['identity']['source'] != accepted['renderer_source']:
        raise RuntimeError('Native qualification/source mismatch')
    source = accepted['renderer_source']
    if stills['identity']['source'] != source:
        raise RuntimeError('Still and film source revisions differ')
    catalog = read(ROOT / 'assets/materials/catalog.json')['entries']
    existing = {asset['id'] for asset in manifest['assets']}
    assets, records = [], {}
    credit = {'label': 'Cubey original media · MIT', 'url': 'https://github.com/byebyebryan/cubey/blob/main/LICENSE', 'author': 'Bryan'}

    def prepare(identifier, original, caption, video=False):
        if identifier in existing or identifier in records:
            raise RuntimeError(f'Refusing reused media identity: {identifier}')
        native_hash = sha(original)
        if accepted['files'].get('assets/' + original.name) != native_hash:
            raise RuntimeError(f'Master differs from the qualified comparison: {original}')
        target = output / (identifier + ('.mp4' if video else '.webp'))
        options = (
            ['-vf', 'scale=1280:720:flags=lanczos', '-an', '-c:v', 'libx264', '-preset', 'medium',
             '-crf', '24', '-pix_fmt', 'yuv420p', '-movflags', '+faststart'] if video else
            ['-vf', 'scale=960:540:flags=lanczos', '-frames:v', '1', '-c:v', 'libwebp', '-quality', '82']
        )
        command = ['ffmpeg', '-v', 'error', '-n', '-i', str(original), *options, str(target)]
        subprocess.run(command, check=True)
        asset = {
            'id': identifier, 'file': '/media/' + target.name, 'kind': 'video' if video else 'image',
            'width': 1280 if video else 960, 'height': 720 if video else 540,
            'sha256': sha(target), 'bytes': target.stat().st_size, 'caption': caption, 'credit': credit,
            'provenance': {'source': str(original.relative_to(ROOT)), 'sourceSha256': native_hash,
                           'sourceRevision': source, 'recipe': RECEIPT,
                           'note': 'Web derivative of a qualified 1920×1080 native capture. Renderer transport differs; shared recipes are unchanged.'},
        }
        if asset['bytes'] > (8_000_000 if video else 250_000):
            raise RuntimeError(f'Media budget exceeded: {identifier}')
        record = {'nativeSource': asset['provenance']['source'], 'nativeSha256': native_hash,
                  'webSha256': asset['sha256'], 'command': command}
        if video:
            probe = json.loads(subprocess.check_output([
                'ffprobe', '-v', 'error', '-count_frames', '-show_entries',
                'stream=codec_type,codec_name,pix_fmt,width,height,r_frame_rate,nb_read_frames:format=duration',
                '-of', 'json', str(target)], text=True))
            streams = probe['streams']
            if (len(streams) != 1 or streams[0].get('codec_name') != 'h264' or
                    streams[0].get('pix_fmt') != 'yuv420p' or streams[0].get('r_frame_rate') != '30/1' or
                    streams[0].get('nb_read_frames') != '240' or float(probe['format']['duration']) != 8):
                raise RuntimeError(f'Film cadence/codec changed: {identifier}')
            record['ffprobe'] = probe
        assets.append(asset)
        records[identifier] = record

    for renderer, label in LABELS.items():
        raw = 'forward' if renderer == 'mesh' else renderer
        for material in catalog:
            prepare(f'materials-{renderer}-{material["id"]}', native / 'stills' / f'{raw}-{material["id"]}.png',
                    f'{material["label"]} · {label} · shared clear atmosphere')
        for phase in ('dusk', 'dawn'):
            prefix = f'materials-{renderer}-{phase}'
            caption = f'Twelve shared materials · {label} · {phase} under the shared clear atmosphere.'
            prepare(prefix, native / 'films' / f'{raw}-{phase}-poster.png', caption)
            prepare(prefix + '-film', native / 'films' / f'{raw}-{phase}.mp4', caption, video=True)
            film_record = next(row for row in films['results'] if row['name'] == f'{raw}-{phase}')
            if film_record['sha256'] != records[prefix + '-film']['nativeSha256'] or not film_record['passed']:
                raise RuntimeError(f'Film qualification identity changed: {prefix}')

    receipt = {
        'format': 'cubey.site.raytracing-materials', 'version': 1, 'sourceRevision': source,
        'scope': 'Native 1080p30 masters, 720p30 film and 960×540 still derivatives. Clear atmosphere, environment after exit, no local specimen shadows or diffuse GI. No performance claim.',
        'qualifiedManifestSha256': sha(bundle / 'artifact-manifest.json'),
        'qualification': {name: sha(ROOT / name) for name in (
            'docs/evidence/forward-transmission-2026-11/f2.json',
            'docs/evidence/forward-transmission-2026-11/review.json')},
        'canonicalRecipes': {name: str(value) for name, value in stills['results'][0]['canonical_fingerprints'].items()},
        'assets': records,
    }
    manifest['assets'].extend(assets)
    manifest['selections']['mesh-materials'] = {
        'poster': 'materials-mesh-dusk', 'film': 'materials-mesh-dusk-film',
        'preview': 'preview-materials-mesh-dusk',
    }
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    if args.import_media:
        for asset in assets:
            shutil.copy2(output / Path(asset['file']).name, SITE / 'public' / asset['file'].lstrip('/'))
        (SITE / 'media/manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        (ROOT / RECEIPT).write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({'assets': len(assets), 'bytes': sum(asset['bytes'] for asset in assets), 'source': source}))


if __name__ == '__main__':
    main()
