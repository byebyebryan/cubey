"""Prepare silent overview previews for every selected film-backed entry."""
from pathlib import Path
import argparse
import copy
import hashlib
import json
import math
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SITE = ROOT / 'site'
MANIFEST = SITE / 'media/manifest.json'
RECEIPT_PATH = SITE / 'media/receipts/hover-previews.json'
OVERRIDES_PATH = SITE / 'media/receipts/hover-preview-trims.json'
RECEIPT = 'site/media/receipts/hover-previews.json'
MAX_BYTES = 400_000
WIDTH = 640
HEIGHT = 360
FPS = 18
DEFAULT_DURATION = 3.5
DEFAULT_CRF = 26
MAX_CRF = 36


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def fast_start(path):
    data = Path(path).read_bytes()
    moov = mdat = -1
    offset = 0
    while offset < len(data):
        if offset + 8 > len(data):
            return False
        size = int.from_bytes(data[offset:offset + 4], 'big')
        kind = data[offset + 4:offset + 8]
        header = 8
        if size == 1:
            if offset + 16 > len(data):
                return False
            size = int.from_bytes(data[offset + 8:offset + 16], 'big')
            header = 16
        elif size == 0:
            size = len(data) - offset
        if size < header or offset + size > len(data):
            return False
        if kind == b'moov' and moov < 0:
            moov = offset
        if kind == b'mdat' and mdat < 0:
            mdat = offset
        offset += size
    return moov >= 0 and mdat >= 0 and moov < mdat


def probe(path):
    try:
        result = subprocess.run(
            ['ffprobe', '-v', 'error', '-show_streams', '-show_format', '-of', 'json', str(path)],
            check=True, capture_output=True, text=True)
    except FileNotFoundError as exc:
        raise RuntimeError('ffprobe is required for preview preparation and film imports') from exc
    except subprocess.CalledProcessError as exc:
        detail = exc.stderr.strip() or 'no ffprobe diagnostic'
        raise RuntimeError(f'Cannot inspect film {path}: {detail}') from exc
    data = json.loads(result.stdout)
    videos = [stream for stream in data.get('streams', []) if stream.get('codec_type') == 'video']
    audios = [stream for stream in data.get('streams', []) if stream.get('codec_type') == 'audio']
    if len(videos) != 1:
        raise RuntimeError(f'Expected one video stream in {path}, found {len(videos)}')
    video = videos[0]
    duration = video.get('duration') or data.get('format', {}).get('duration')
    if not duration:
        raise RuntimeError(f'Film has no readable duration: {path}')
    return {
        'width': int(video.get('width', 0)),
        'height': int(video.get('height', 0)),
        'duration': float(duration),
        'codec': video.get('codec_name'),
        'pixelFormat': video.get('pix_fmt'),
        'frameRate': video.get('avg_frame_rate') or video.get('r_frame_rate'),
        'audioStreams': len(audios),
        'streamCount': len(data.get('streams', [])),
    }


def overrides():
    if not OVERRIDES_PATH.exists():
        return {}
    data = json.loads(OVERRIDES_PATH.read_text())
    entries = data.get('entries', {})
    if not isinstance(entries, dict):
        raise RuntimeError(f'Invalid preview trim configuration: {OVERRIDES_PATH}')
    return entries


def numeric(value, label, minimum=0):
    if (isinstance(value, bool) or not isinstance(value, (int, float)) or
            not math.isfinite(value) or value < minimum):
        raise RuntimeError(f'Invalid {label} preview override: {value!r}')
    return float(value)


def requested_settings(entry, parent, source_info, override):
    unknown = set(override) - {'startSeconds', 'durationSeconds', 'crf'}
    if unknown:
        raise RuntimeError(f'Unknown preview override for {entry}: {", ".join(sorted(unknown))}')
    start = numeric(override.get('startSeconds', 0), f'{entry} start')
    duration_limit = numeric(override.get('durationSeconds', DEFAULT_DURATION), f'{entry} duration', 0.1)
    if duration_limit > DEFAULT_DURATION:
        raise RuntimeError(f'Preview duration for {entry} must be at most {DEFAULT_DURATION:g} seconds')
    crf = int(numeric(override.get('crf', DEFAULT_CRF), f'{entry} CRF', 18))
    if crf > MAX_CRF:
        raise RuntimeError(f'Preview CRF for {entry} must be at most {MAX_CRF}')
    if start >= source_info['duration']:
        raise RuntimeError(f'Preview trim for {entry} starts beyond its {source_info["duration"]:.3f}s film')
    duration = min(duration_limit, source_info['duration'] - start)
    if duration < 1 / FPS:
        raise RuntimeError(f'Preview trim for {entry} is shorter than one {FPS} fps frame')
    direct_scale = source_info['width'] * HEIGHT == source_info['height'] * WIDTH
    fit = 'direct-16x9' if direct_scale else 'contain-16x9'
    return {
        'startSeconds': start,
        'durationSeconds': duration,
        'requestedDurationSeconds': duration_limit,
        'requestedCrf': crf,
        'crf': crf,
        'preset': 'slow',
        'fit': fit,
        'width': WIDTH,
        'height': HEIGHT,
        'frameRate': f'{FPS}/1',
        'pixelFormat': 'yuv420p',
        'codec': 'h264',
        'audioStreams': 0,
        'fastStart': True,
    }


def command_settings(record):
    command = record.get('command') or []
    if not command:
        return None

    def arg(flag, default=None):
        try:
            return command[command.index(flag) + 1]
        except (ValueError, IndexError):
            return default

    vf = arg('-vf', '')
    if not vf:
        return None
    scale = re.search(r'scale=(\d+):(\d+)', vf)
    fps = re.search(r'fps=(\d+)', vf)
    if not scale or not fps:
        return None
    return {
        'startSeconds': float(arg('-ss', '0')),
        'durationSeconds': float(arg('-t', '0')),
        'requestedDurationSeconds': float(arg('-t', '0')),
        'requestedCrf': int(arg('-crf', DEFAULT_CRF)),
        'crf': int(arg('-crf', DEFAULT_CRF)),
        'preset': arg('-preset', ''),
        'fit': 'direct-16x9',
        'width': int(scale.group(1)),
        'height': int(scale.group(2)),
        'frameRate': f'{fps.group(1)}/1',
        'pixelFormat': arg('-pix_fmt', ''),
        'codec': 'h264' if arg('-c:v', '') == 'libx264' else arg('-c:v', ''),
        'audioStreams': 0 if '-an' in command else None,
        'fastStart': '+faststart' in command,
    }


def settings_match(record, expected):
    actual = record.get('settings') or command_settings(record)
    if not actual:
        return False
    for key in ('startSeconds', 'requestedDurationSeconds', 'preset', 'fit', 'width', 'height',
                'frameRate', 'pixelFormat', 'codec', 'audioStreams', 'fastStart'):
        if actual.get(key) != expected.get(key):
            return False
    if abs(float(actual.get('durationSeconds', -100)) - expected['durationSeconds']) > 0.06:
        return False
    return (actual.get('requestedCrf') == expected['requestedCrf'] and
            expected['requestedCrf'] <= int(actual.get('crf', -1)) <= MAX_CRF)


def valid_asset(path, asset, parent, record, expected):
    if not asset or not record or not path.is_file():
        return False
    if (asset.get('kind') != 'video' or asset.get('width') != WIDTH or asset.get('height') != HEIGHT or
            asset.get('bytes', MAX_BYTES + 1) > MAX_BYTES or asset.get('sha256') != sha(path)):
        return False
    provenance = asset.get('provenance', {})
    if (provenance.get('parentAsset') != parent['id'] or
            provenance.get('sourceSha256') != parent.get('sha256') or
            provenance.get('sourceRevision') != parent.get('provenance', {}).get('sourceRevision') or
            asset.get('credit') != parent.get('credit')):
        return False
    if (record.get('parentAsset') != parent['id'] or record.get('parentSha256') != parent.get('sha256') or
            record.get('sha256') != asset.get('sha256') or record.get('bytes') != asset.get('bytes') or
            not settings_match(record, expected)):
        return False
    try:
        delivery = probe(path)
    except (RuntimeError, ValueError, json.JSONDecodeError):
        return False
    return (delivery['codec'] == 'h264' and delivery['width'] == WIDTH and delivery['height'] == HEIGHT and
            delivery['pixelFormat'] == 'yuv420p' and delivery['frameRate'] == f'{FPS}/1' and
            delivery['audioStreams'] == 0 and delivery['streamCount'] == 1 and fast_start(path))


def filter_for(fit):
    if fit == 'direct-16x9':
        return f'scale={WIDTH}:{HEIGHT}:flags=lanczos,fps={FPS}'
    return (f'scale={WIDTH}:{HEIGHT}:force_original_aspect_ratio=decrease:flags=lanczos,'
            f'pad={WIDTH}:{HEIGHT}:(ow-iw)/2:(oh-ih)/2,fps={FPS}')


def ffmpeg_command(source, temp_path, settings):
    return [
        'ffmpeg', '-v', 'error', '-n', '-ss', f'{settings["startSeconds"]:g}', '-i', str(source),
        '-t', f'{settings["durationSeconds"]:g}', '-an', '-vf', filter_for(settings['fit']),
        '-c:v', 'libx264', '-preset', settings['preset'], '-crf', str(settings['crf']),
        '-pix_fmt', settings['pixelFormat'], '-movflags', '+faststart', str(temp_path)]


def encode(source, target, settings, entry):
    last_size = 0
    for crf in range(settings['requestedCrf'], MAX_CRF + 1, 2):
        current = copy.deepcopy(settings)
        current['crf'] = crf
        fd, temp_name = tempfile.mkstemp(prefix=f'.{target.stem}.', suffix='.mp4', dir=target.parent)
        os.close(fd)
        temp_path = Path(temp_name)
        temp_path.unlink()
        command = ffmpeg_command(source, temp_path, current)
        try:
            try:
                subprocess.run(command, check=True, capture_output=True, text=True)
            except FileNotFoundError as exc:
                raise RuntimeError('ffmpeg is required for preview preparation and film imports') from exc
            except subprocess.CalledProcessError as exc:
                detail = exc.stderr.strip() or 'no ffmpeg diagnostic'
                raise RuntimeError(f'Cannot encode preview for {entry}: {detail}') from exc
            if not temp_path.is_file():
                raise RuntimeError(f'ffmpeg did not create a preview for {entry}')
            last_size = temp_path.stat().st_size
            if last_size > MAX_BYTES:
                continue
            delivery = probe(temp_path)
            if (delivery['codec'] != 'h264' or delivery['width'] != WIDTH or delivery['height'] != HEIGHT or
                    delivery['pixelFormat'] != 'yuv420p' or delivery['frameRate'] != f'{FPS}/1' or
                    delivery['audioStreams'] != 0 or delivery['streamCount'] != 1 or not fast_start(temp_path)):
                raise RuntimeError(f'Encoded preview for {entry} violates the H.264 silent fast-start contract')
            os.replace(temp_path, target)
            current['durationSeconds'] = delivery['duration']
            return current, delivery
        finally:
            temp_path.unlink(missing_ok=True)
    raise RuntimeError(f'Cannot meet the {MAX_BYTES}-byte preview budget for {entry}; smallest attempt was {last_size} bytes (CRF {MAX_CRF})')


def copy_if_changed(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.is_file() and sha(source) == sha(target):
        return
    fd, temp_name = tempfile.mkstemp(prefix=f'.{target.name}.', dir=target.parent)
    os.close(fd)
    temp = Path(temp_name)
    try:
        shutil.copy2(source, temp)
        os.replace(temp, target)
    finally:
        temp.unlink(missing_ok=True)


def preview_asset(identifier, parent, asset_path, output_path):
    return {
        'id': identifier,
        'file': '/media/' + asset_path.name,
        'kind': 'video',
        'width': WIDTH,
        'height': HEIGHT,
        'sha256': sha(output_path),
        'bytes': output_path.stat().st_size,
        'caption': 'Short silent preview. ' + parent['caption'],
        'credit': copy.deepcopy(parent['credit']),
        'provenance': {
            'source': 'site/public/media/' + Path(parent['file']).name,
            'sourceSha256': parent['sha256'],
            'sourceRevision': parent.get('provenance', {}).get('sourceRevision'),
            'parentAsset': parent['id'],
            'recipe': RECEIPT,
            'note': 'Silent video derivative of the selected web film; no native capture or render was made.',
        },
    }


def enrich_record(record, parent, asset, delivery, settings):
    updated = copy.deepcopy(record)
    updated.update({
        'parentAsset': parent['id'],
        'parentSha256': parent['sha256'],
        'parentSourceRevision': parent.get('provenance', {}).get('sourceRevision'),
        'durationSeconds': round(delivery['duration'], 6),
        'sha256': asset['sha256'],
        'bytes': asset['bytes'],
        'codec': delivery['codec'],
        'pixelFormat': delivery['pixelFormat'],
        'width': delivery['width'],
        'height': delivery['height'],
        'frameRate': delivery['frameRate'],
        'audioStreams': delivery['audioStreams'],
        'fastStart': True,
        'settings': copy.deepcopy(settings),
    })
    return updated


def prepare(media, output_dir, entry_names=None, source_paths=None):
    """Prepare a candidate ledger and stage all selected clips without committing it."""
    candidate = copy.deepcopy(media)
    source_paths = source_paths or {}
    selected_entries = candidate.get('selections', {})
    requested_entries = set(entry_names or selected_entries.keys())
    unknown_entries = requested_entries - set(selected_entries)
    if unknown_entries:
        raise RuntimeError('Unknown media selection(s): ' + ', '.join(sorted(unknown_entries)))
    output_dir = Path(output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    receipt = json.loads(RECEIPT_PATH.read_text()) if RECEIPT_PATH.exists() else {
        'scope': 'Silent derivatives of selected web films for overview hover. No new native capture or performance claim.',
        'assets': {},
    }
    receipt.setdefault('assets', {})
    overrides_by_entry = overrides()
    asset_map = {asset['id']: asset for asset in candidate['assets']}
    output_map = {}

    for entry in sorted(requested_entries):
        selection = selected_entries[entry]
        film_id = selection.get('film')
        if not film_id:
            continue
        parent = asset_map.get(film_id)
        if not parent or parent.get('kind') != 'video':
            raise RuntimeError(f'Selected film is missing or invalid: {entry}/{film_id}')
        source = Path(source_paths.get(film_id, SITE / 'public' / parent['file'].lstrip('/'))).resolve()
        if not source.is_file() or sha(source) != parent.get('sha256'):
            raise RuntimeError(f'Parent film identity changed: {entry} ({film_id})')
        source_info = probe(source)
        if source_info['width'] <= 0 or source_info['height'] <= 0:
            raise RuntimeError(f'Selected film has invalid dimensions: {entry}/{film_id}')
        override = overrides_by_entry.get(entry, {})
        expected = requested_settings(entry, parent, source_info, override)
        identifier = selection.get('preview') or 'preview-' + entry
        old_asset = asset_map.get(identifier)
        if old_asset and not selection.get('preview'):
            raise RuntimeError(f'Preview asset ID conflicts with an existing asset: {entry}/{identifier}')
        if identifier == film_id:
            raise RuntimeError(f'Preview asset ID conflicts with its parent film: {entry}/{identifier}')
        if old_asset and old_asset.get('kind') != 'video':
            raise RuntimeError(f'Preview asset ID conflicts with a non-video asset: {entry}/{identifier}')
        other_users = [name for name, other in selected_entries.items()
                       if name != entry and other.get('preview') == identifier]
        if other_users:
            raise RuntimeError(f'Preview asset is shared by selections: {identifier} ({", ".join(other_users)})')
        record = receipt['assets'].get(identifier)
        asset_file = Path(old_asset['file']).name if old_asset else identifier + '.mp4'
        target = output_dir / asset_file

        public_asset = SITE / 'public' / old_asset['file'].lstrip('/') if old_asset else None
        if old_asset and valid_asset(public_asset, old_asset, parent, record, expected):
            copy_if_changed(SITE / 'public' / old_asset['file'].lstrip('/'), target)
            delivery = probe(target)
            settings = copy.deepcopy(record.get('settings') or command_settings(record) or expected)
            settings['requestedDurationSeconds'] = expected['requestedDurationSeconds']
            settings['requestedCrf'] = expected['requestedCrf']
            settings['fit'] = expected['fit']
            receipt['assets'][identifier] = enrich_record(record, parent, old_asset, delivery, settings)
            output_map[identifier] = target
            print(json.dumps({'entry': entry, 'preview': identifier, 'bytes': old_asset['bytes'], 'reused': True}), flush=True)
            continue

        settings, delivery = encode(source, target, expected, entry)
        asset = preview_asset(identifier, parent, target, target)
        asset_map[identifier] = asset
        selection['preview'] = identifier
        candidate['assets'] = list(asset_map.values())
        command_for_receipt = [
            'ffmpeg', '-v', 'error', '-n', '-ss', f'{settings["startSeconds"]:g}', '-i',
            'site/public/media/' + Path(parent['file']).name,
            '-t', f'{expected["durationSeconds"]:g}', '-an', '-vf', filter_for(settings['fit']),
            '-c:v', 'libx264', '-preset', settings['preset'], '-crf', str(settings['crf']),
            '-pix_fmt', settings['pixelFormat'], '-movflags', '+faststart',
            'site/public/media/' + Path(asset['file']).name,
        ]
        receipt['assets'][identifier] = enrich_record({
            'startSeconds': settings['startSeconds'],
            'requestedDurationSeconds': expected['requestedDurationSeconds'],
            'command': command_for_receipt,
        }, parent, asset, delivery, settings)
        output_map[identifier] = target
        print(json.dumps({'entry': entry, 'preview': identifier, 'bytes': asset['bytes'], 'reused': False,
                          'crf': settings['crf']}), flush=True)

    candidate['assets'] = sorted(asset_map.values(), key=lambda asset: asset['id'])
    receipt['assets'] = dict(sorted(receipt['assets'].items()))
    return candidate, receipt, output_map


def atomic_write(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_file() and path.read_bytes() == data:
        return False
    fd, temp_name = tempfile.mkstemp(prefix=f'.{path.name}.', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as target:
            target.write(data)
            target.flush()
            os.fsync(target.fileno())
        os.replace(temp_name, path)
    finally:
        Path(temp_name).unlink(missing_ok=True)
    return True


def commit(candidate, receipt, outputs, extra_files=None):
    """Install staged media and atomically publish a complete candidate ledger."""
    extra_files = extra_files or []
    manifest_bytes = (json.dumps(candidate, indent=2) + '\n').encode()
    receipt_bytes = (json.dumps(receipt, indent=2) + '\n').encode()
    files = [(source, SITE / 'public' / next(a['file'].lstrip('/') for a in candidate['assets'] if a['id'] == identifier))
             for identifier, source in outputs.items()]
    files.extend(extra_files)
    unique_files = {}
    for source, target in files:
        unique_files[Path(target)] = Path(source)
    changed = [(source, target) for target, source in unique_files.items()
               if not target.is_file() or sha(source) != sha(target)]
    backups = {target: target.read_bytes() if target.is_file() else None for _, target in changed}
    old_manifest = MANIFEST.read_bytes()
    old_receipt = RECEIPT_PATH.read_bytes() if RECEIPT_PATH.exists() else None
    try:
        for source, target in changed:
            copy_if_changed(source, target)
        atomic_write(RECEIPT_PATH, receipt_bytes)
        atomic_write(MANIFEST, manifest_bytes)
    except Exception:
        atomic_write(MANIFEST, old_manifest)
        if old_receipt is None:
            RECEIPT_PATH.unlink(missing_ok=True)
        else:
            atomic_write(RECEIPT_PATH, old_receipt)
        for target, previous in backups.items():
            if previous is None:
                target.unlink(missing_ok=True)
            else:
                atomic_write(target, previous)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True,
                        help='staging directory; successful files remain there if a later encode fails')
    parser.add_argument('--entry', action='append', dest='entries',
                        help='prepare only this media selection (repeatable); all film selections are the default')
    parser.add_argument('--import-media', action='store_true',
                        help='commit the complete staged candidate to public media and its ledgers')
    args = parser.parse_args()
    media = json.loads(MANIFEST.read_text())
    candidate, receipt, outputs = prepare(media, args.output, args.entries)
    if args.import_media:
        commit(candidate, receipt, outputs)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, json.JSONDecodeError) as error:
        raise SystemExit(str(error)) from error
