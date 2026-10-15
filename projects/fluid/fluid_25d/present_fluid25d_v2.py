#!/usr/bin/env python3
"""Capture/package the approved Joining valleys sunset; no solver or upload.

Explicit optional archive inputs are required. V1 Mountain reproduction remains
separate. Native 1080p is the master; the gallery is an identified 720p derivative.
"""
from __future__ import annotations

import argparse
import array
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import sys

import present_fluid25d_v1 as p

ROOT, MEDIA = p.ROOT, p.MEDIA
APPROVED = 'f35e77792d96e640bcefafad9af2edc2e47b14e62f19fcd67c26555256e76576'
RECORDING = 'a6b00c7aac9d7c0426306c7fe9c60d0a28ec76513bb8a9da42dfc192087cc5cc'
CASE = '9c8fdb91e68ce515b4ebc64f8c9750e51472ff6075cd48cbe04b9b7ec2463aad'
RESULT = '892dd8a1eca7723f82700b1be331dab10db7156a6716b20996b6a2631f1064b6'
EMPTY = hashlib.sha256(b'').hexdigest()
POSTER_S = 6.0  # Final water state, before the darkest sunset frames.


def git(*args):
    return subprocess.check_output(['rtk', 'proxy', 'git', *args], cwd=ROOT)


def clean_source():
    # Include the index AND untracked source, not just an unstaged diff.
    status = git('status', '--porcelain=v1', '--untracked-files=all')
    if status:
        raise ValueError('capture requires clean tracked/index/untracked source')
    return dict(source_revision=git('rev-parse', 'HEAD').decode().strip(),
                source_status_sha256=hashlib.sha256(status).hexdigest(),
                source_dirty_diff_sha256=hashlib.sha256(git('diff', 'HEAD')).hexdigest())


def direct_file(root, name):
    path = root / name
    if Path(name).name != name or path.is_symlink() or not path.is_file():
        raise ValueError('expected a regular direct-child archive/media file')
    return path


def checked_file(root, descriptor):
    path = direct_file(root, descriptor['path'])
    if p.digest(path) != descriptor['sha256']:
        raise ValueError('archive/media payload hash changed')
    return path


def verify_archive(case, recording, audit_path):
    if p.digest(recording) != RECORDING:
        raise ValueError('not the owner-selected Joining valleys recording')
    identity = p.demo.case_identity(case)
    if identity['protocol_sha256'] != CASE:
        raise ValueError('not the selected immutable case')
    manifest, spec = p.read(recording), identity['spec']
    run = recording.parent.parent
    if p.digest(run/'result.json') != RESULT:
        raise ValueError('original run receipt changed')
    result = p.read(run/'result.json')
    if (not result['reached_target'] or not result['immutable_case_unchanged'] or
            result['cleanup']['active_group_remaining'] or result['rain_mm_per_hour'] != 512):
        raise ValueError('original run did not qualify')
    expected_grid = dict(width=1024, height=1024, cell_size_m=16,
                         sample_location='cell-center', storage_order='row-major',
                         row_direction='world-z-positive')
    if manifest['grid'] != expected_grid or spec['manning_n'] != .05 or abs(
            spec['rain_rate_m_per_s']*3600000-512) > 1e-6:
        raise ValueError('geometry or forcing differs')
    provenance = manifest['provenance']
    sealed = provenance['source_sha256']
    if (provenance['capture_schema'] != 'cubey.fluid25d.external-binary-capture.v1' or
            provenance['case_protocol_sha256'] != CASE or sealed['case_protocol'] != CASE or
            sealed['case_spec'] != p.digest(case/'case-spec.json') or
            sealed['metadata'] != p.digest(run/'session/metadata.json')):
        raise ValueError('sealed native identity differs')
    terrain = spec['terrain_source_identity']
    for key, expected in (('bed', terrain['solver_bed_sha256']),
                          ('source_bed', terrain['source_bed_sha256'])):
        checked_file(recording.parent, manifest[key])
        if manifest[key]['sha256'] != expected:
            raise ValueError('terrain/numerical bed differs')
    if sealed['native_numerical_bed'] != manifest['bed']['sha256']:
        raise ValueError('sealed numerical bed differs')
    frames, captures = manifest['frames'], provenance['captures']
    if len(frames) != 72 or len(captures) != 72:
        raise ValueError('all 72 native saves required')
    previous, gaps = -1, []
    for frame, capture in zip(frames, captures):
        path = checked_file(recording.parent, frame)
        time = frame['time_s']
        if (path.stat().st_size != 3*1024*1024*4 or
                time != capture['physical_time_s'] or time <= previous):
            raise ValueError('native snapshot layout/timeline differs')
        if previous >= 0:
            gaps.append(time-previous)
        previous = time
    if frames[0]['time_s'] != 0 or not 21600 <= previous <= 21700 or max(gaps) > 360:
        raise ValueError('native endpoints/cadence differ')
    initial = array.array('f', direct_file(recording.parent, frames[0]['path']).read_bytes())
    if sys.byteorder != 'little':
        initial.byteswap()
    if any(initial):  # Negative-zero momentum is numerically dry, too.
        raise ValueError('opening is not dry')
    audit = next(row for row in p.read(audit_path)['cases'] if row['case'] == case.name)
    if not all(audit[key] for key in ('finite_nonnegative_dry_momentum_clean',
            'volume_not_above_supply_2pct', 'target_reached', 'owned_group_gone')):
        raise ValueError('original field audit failed')
    return dict(identity=identity, recording_sha256=RECORDING, result_sha256=RESULT,
                field_audit=audit, field_audit_sha256=p.digest(audit_path),
                bed_sha256=manifest['bed']['sha256'], original_frame_count=len(frames),
                maximum_native_gap_s=max(gaps), final=frames[-1],
                terrain_generator=p.read(terrain['manifest_path'])['source'],
                extension_sha256=provenance['extension_sha256'],
                boundary='Older sealed native snapshots; all retained payload/bed hashes rechecked. '
                         'Original packet headers unavailable; no new conservation certificate.')


def runtime():
    shaders = {s.name: p.digest(s) for s in sorted((p.demo.RELEASE_APP.parent/'shaders').glob('*.spv'))}
    if not shaders:
        raise ValueError('missing runtime shaders')
    return dict(app_sha256=p.digest(p.demo.RELEASE_APP), shaders=shaders)


def editorial_receipts(raw, shot):
    timeline = p.check_timeline(raw, shot)
    states = re.findall(r'fluid_25d_recording_capture: output_frame=(\d+) requested_s=(\S+) saved_s=(\S+) camera=overview hydraulic_dispatches=(\d+)', raw)
    rain = re.findall(r'fluid_25d_rain_capture: output_frame=(\d+) applied_mm_h=(\S+) known=1 clock_s=(\S+)', raw)
    if (len(states) != 480 or any(int(v[3]) for v in states) or len(rain) != 480 or
            [int(v[0]) for v in rain] != list(range(480)) or
            any(float(v[1]) != 512 or abs(float(v[2])-int(v[0])/60) > 1e-7 for v in rain)):
        raise ValueError('missing rendered frames, rain clock, or zero-dispatch receipts')
    if (any(float(v[1]) != 0 or float(v[2]) != 0 for v in states[:24]) or
            any(abs(float(v[2])-shot['end_s']) > .001 for v in states[-120:]) or
            timeline['unique_hydraulic_states'] != 72):
        raise ValueError('native endpoint holds or source states differ')
    cameras = re.findall(r'fluid_25d_terrain_camera: output_frame=(\d+) yaw=(\S+) pitch=(\S+) distance=(\S+)', raw)
    seen = set()
    for index, yaw, pitch, distance in cameras:
        index = int(index)
        fraction = index/479
        eased = fraction*fraction*(3-2*fraction)
        if (not 0 <= index < 480 or abs(float(yaw)-(.3+.6*eased)) > 2e-6 or
                abs(float(pitch)-shot['pitch']) > 1e-6 or float(distance) != 13500):
            raise ValueError('camera does not match approved eased orbit')
        seen.add(index)
    if seen != set(range(480)):
        raise ValueError('missing camera receipts')
    solar = re.findall(r'terrain_surface_environment: wall_s=(\S+) time_hours=(\S+) sun_elevation=(\S+) sky_generation=(\d+).* clouds=0 ', raw)
    rate = (shot['solar_end_hour']-shot['solar_start_hour'])/(479/60)
    if (len(solar) != 32 or any(abs(float(wall)-i*.25) > 1e-6 or
            # Native solar hours accumulate in float32; permit <1.8 seconds
            # of solar rounding across the entire 2.5-hour artistic interval.
            abs(float(hour)-(15.5+rate*float(wall))) > 5e-4
            for i, (wall, hour, _, _) in enumerate(solar))):
        raise ValueError('shared solar presentation clock differs')
    return dict(all_frames_rendered=True, cloned_frames=0, hydraulic_dispatches=0,
                opening_held_native_frames=24, closing_held_native_frames=120,
                rain_clock_last_s=float(rain[-1][2]), matched_camera=True,
                solar_receipts=[list(receipt) for receipt in solar], **timeline)


def fresh_output(path):
    out = path.absolute()
    if out.exists() or out.parent.resolve() != (ROOT/'outputs/showcase').resolve():
        raise ValueError('use a fresh direct leaf under outputs/showcase')
    out.mkdir()
    return out


def capture(args):
    source = clean_source()
    case, recording = args.case.resolve(strict=True), args.recording.resolve(strict=True)
    info = verify_archive(case, recording, args.audit)
    frozen = runtime()
    shot = p.patch_solar_refinement()
    out = fresh_output(args.out)
    protocol = dict(schema='cubey.fluid25d.presentation-capture.v2', **source, **frozen,
                    input=info, harness_sha256=p.digest(Path(__file__)),
                    helper_sha256=p.digest(Path(p.__file__)), tuning_sha256=p.digest(shot['tuning']),
                    scope='Hardware-headless Vulkan recorded-field presentation, not live performance')
    p.write(out/'protocol.json', protocol)
    row = p.render(case, recording, shot, out)
    raw = (out/(shot['name']+'.log')).read_text()
    row['editorial'] = editorial_receipts(raw, row['shot'])
    if row['sha256'] != APPROVED:
        raise ValueError('capture differs from owner-approved sunset bytes; retain as failed evidence')
    if (clean_source() != source or runtime() != frozen or
            verify_archive(case, recording, args.audit) != info):
        raise ValueError('source, runtime, or inputs changed during capture')
    row['log_sha256'] = p.digest(out/(shot['name']+'.log'))
    row['approved_byte_exact'] = True
    p.write(out/'capture.json', row)
    print('PASS: clean-source 1080p sunset matches approved bytes; '+str(out), flush=True)


def qualify_capture(root):
    protocol, row = p.read(root/'protocol.json'), p.read(root/'capture.json')
    if (protocol['schema'] != 'cubey.fluid25d.presentation-capture.v2' or
            protocol['source_status_sha256'] != EMPTY or protocol['source_dirty_diff_sha256'] != EMPTY or
            protocol['input']['recording_sha256'] != RECORDING or row['sha256'] != APPROVED or
            row['cleanup']['active_group_remaining'] or row['cleanup']['exit_code'] != 0):
        raise ValueError('capture is not a qualified clean-source selected master')
    master = checked_file(root, row)
    checked_file(root, dict(path=row['source'], sha256=row['source_sha256']))
    log = checked_file(root, dict(path=row['shot']['name']+'.log', sha256=row['log_sha256']))
    if editorial_receipts(log.read_text(), row['shot']) != row['editorial']:
        raise ValueError('capture receipts changed')
    if '--headless' not in row['command'] or '--fluid25d-external-session' in row['command']:
        raise ValueError('not an offscreen recorded-field capture')
    p.check_video(p.probe(master), 480, 8, 1920, 1080)
    return protocol, row, master


def package(args):
    root = args.capture.resolve(strict=True)
    protocol, row, master = qualify_capture(root)
    out = fresh_output(args.out)
    export = out/'export'
    export.mkdir()
    shutil.copy2(master, export/'fluid-25d-master-1080p.mp4')
    gallery = export/'fluid-25d.mp4'
    command = ['rtk', 'proxy', 'ffmpeg', '-v', 'error', '-i', str(master), '-an',
               '-vf', 'scale=1280:720:flags=lanczos,setpts=N/(60*TB)', '-r', '60', '-fps_mode', 'cfr',
               '-c:v', 'libx264', '-preset', 'medium', '-crf', '24', '-pix_fmt', 'yuv420p',
               '-movflags', '+faststart', str(gallery)]
    subprocess.run(command, check=True, timeout=180)
    stream = p.probe(gallery)
    p.check_video(stream, 480, 8)
    poster_command = ['rtk', 'proxy', 'ffmpeg', '-v', 'error', '-ss', str(POSTER_S),
                      '-i', str(gallery), '-frames:v', '1', '-vf', 'format=rgb24', str(export/'fluid-25d.png')]
    subprocess.run(poster_command, check=True, timeout=60)
    files = ('fluid-25d-master-1080p.mp4', 'fluid-25d.mp4', 'fluid-25d.png')
    document = dict(schema='cubey.fluid25d.presentation.v2', source_protocol=protocol, hero=row,
                    gallery=dict(derived_from='fluid-25d-master-1080p.mp4', command=command, probe=stream),
                    poster=dict(timestamp_seconds=POSTER_S, command=poster_command),
                    media={name: dict(path='docs/media/showcase/'+name,
                        bytes=(export/name).stat().st_size, sha256=p.digest(export/name)) for name in files},
                    verdict='Owner-selected Joining valleys 15-degree sunset; approved byte-exact master.',
                    publication='Packaged locally; GitHub attachment upload and push not performed.',
                    legacy='The separate 36-second Mountain rain-off study retains its V1 recipe; not a companion to this cut.',
                    scopes=['72 exact native water states held between saves; no hydraulic interpolation',
                            '480 natively rendered frames; independent cosmetic rain/foam, camera and solar clocks',
                            'Approximately six physical hours compressed into 5.6 active seconds, plus animated holds',
                            '512 mm/h artistic demo rainfall, not a realistic storm or certified permanent lake',
                            '60 FPS is encoded cadence, not a live solver or scanout benchmark'],
                    solver=dict(name='SynxFlow external service worker',
                                identity_source='selected sealed recording provenance',
                                extension_sha256=protocol['input']['extension_sha256']),
                    licenses=dict(cubey_source_and_generated_media='MIT', terrain_code_and_model='MIT declarations at pinned revisions',
                                  external_worker='GPL-3.0-only separate process; code not embedded'))
    p.write(export/'fluid-25d-recipe.json', document)
    shutil.copy2(Path(__file__).parent/'fixtures/presentation-v2/index.html', export/'index.html')
    print('PASS: 1080p master / derived 720p60 gallery / formed-water poster; '+str(export), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    cap = sub.add_parser('capture', help='clean source and exact optional Joining valleys inputs required')
    cap.add_argument('--case', type=Path, required=True)
    cap.add_argument('--recording', type=Path, required=True)
    cap.add_argument('--audit', type=Path, required=True)
    cap.add_argument('--out', type=Path, required=True)
    pack = sub.add_parser('package', help='fresh bundle only; never replaces repository media')
    pack.add_argument('--capture', type=Path, required=True)
    pack.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    capture(args) if args.mode == 'capture' else package(args)


if __name__ == '__main__':
    main()
