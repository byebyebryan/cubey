#!/usr/bin/env python3
"""Qualify authored SDF expressions, robot views, replay and matched GPU costs."""
import argparse
import csv
import datetime as dt
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess
from qualify import distribution
from sdf_author import box, capped_cone, cylinder, material, rotation, smooth_subtract, smooth_union, sphere, subtract, torus, union, write_scene


def check_video(path, width=1280, height=960, expected_frames=480):
    """Screen the encoded presentation's visible exhaustion color without NumPy."""
    high = bytes(int(value > 180) for value in range(256))
    low = bytes(int(value < 90) for value in range(256))
    frames = maximum = 0
    size = width * height * 3
    with subprocess.Popen(['ffmpeg', '-v', 'error', '-i', str(path), '-f', 'rawvideo',
                           '-pix_fmt', 'rgb24', '-'], stdout=subprocess.PIPE) as proc:
        try:
            while data := proc.stdout.read(size):
                if len(data) != size:
                    raise RuntimeError('Incomplete decoded video frame')
                # One bit in each byte represents a pixel's threshold result.
                # Integer AND/bit_count combines the three channels in C.
                r = int.from_bytes(data[0::3].translate(high), 'little')
                g = int.from_bytes(data[1::3].translate(low), 'little')
                b = int.from_bytes(data[2::3].translate(high), 'little')
                maximum = max(maximum, (r & g & b).bit_count())
                frames += 1
            if proc.wait() != 0:
                raise RuntimeError('Video decoding failed')
        except BaseException:
            proc.kill()
            raise
    if frames != expected_frames or maximum:
        raise RuntimeError(f'Video screening failed: {frames} frames, {maximum} magenta pixels')
    receipt = dict(passed=True, decoded_frames=frames, maximum_magenta_pixels_per_frame=maximum,
        criterion='R > 180, G < 90, B > 180 in decoded RGB24',
        scope='Checks the visible traversal-exhaustion color in every encoded frame; native ray fixtures are separate.')
    (path.parent / 'video-check.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--binary', type=Path, default=Path('build/dev/projects/sdf/sdf'))
    parser.add_argument('--video', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    out = args.output.resolve()
    if out.exists():
        raise RuntimeError('Use a fresh output directory to preserve previous qualification')
    for directory in ('captures', 'fixtures', 'logs', 'configs', 'profiles', 'scenes'):
        (out / directory).mkdir(parents=True)
    binary = args.binary.resolve()
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    manifest = dict(captured_utc=dt.datetime.now(dt.timezone.utc).isoformat(),
        source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        source_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=root, text=True).strip()),
        binary_sha256=sha(binary),
        shader_sha256={p.name: sha(p) for p in sorted((binary.parent / 'shaders').glob('*.spv'))},
        device=subprocess.check_output(['nvidia-smi', '--query-gpu=name,driver_version', '--format=csv,noheader'], text=True).strip(),
        runs={})

    def save():
        (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')

    def run(name, options=(), *, config=True, capture=True, video=False, error=None):
        output = out / ('turntable.mp4' if video else f'captures/{name}.png')
        command = [str(binary), '--headless', '--require-validation']
        if config:
            command += ['--config', str(root / 'projects/sdf/presets/survey-robot.json')]
        command += ['--width', '960', '--height', '960', *map(str, options)]
        if capture:
            command += ['--output', str(output)]
        subprocess.run([*command, '--write-config-template', str(out / f'configs/{name}.json')],
                       cwd=root, check=True, stdout=subprocess.DEVNULL)
        result = subprocess.run(command, cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (out / f'logs/{name}.log').write_text(result.stdout)
        if 'validation error' in result.stdout.lower():
            raise RuntimeError(name + ': Vulkan validation error')
        if error:
            if not result.returncode or error not in result.stdout or output.exists():
                raise RuntimeError(name + ': expected rejection failed: ' + result.stdout)
        elif result.returncode:
            raise RuntimeError(name + ': ' + result.stdout)
        row = dict(command=command, exit_code=result.returncode, expected_error=error,
                   stats=[line for line in result.stdout.splitlines() if line.startswith('sdf:')])
        if capture and not error:
            row.update(capture=str(output.relative_to(out)), capture_sha256=sha(output))
        manifest['runs'][name] = row
        save()
        print(name, 'passed' if not error else 'expected rejection', flush=True)
        return row

    materials = [material('red', 'CC4636'), material('blue', '397BD6', .7)]
    fixtures = {
        'sphere': sphere('ball', (0, 0, 0), .2),
        'capsule': dict(type='capsule', name='capsule', radius=.07, half_length=.16),
        'cylinder': cylinder('cylinder', (0, 0, 0), .15, .12, 'blue'),
        'torus': torus('seal', (0, 0, 0), .18, .045, 'blue'),
        'torus-transformed': union('seal-assembly',
            torus('seal', (0, 0, 0), .18, .025, 'blue', rotation_xyzw=rotation((1, 0, 0), 71)),
            sphere('cap', (.2, 0, 0), .04), position=(.02, .01, -.03), scale=.8,
            rotation_xyzw=rotation((0, 0, 1), 23)),
        'capped-cone': capped_cone('taper', (0, 0, 0), .18, .07, .2, 'blue'),
        'cone-tip': capped_cone('tip', (0, 0, 0), .18, 0, .2, 'blue', rotation_xyzw=rotation((1, 0, 1), 34)),
        'cone-equal-ends': capped_cone('cylinder-equivalent', (0, 0, 0), .15, .15, .12, 'blue'),
        'smooth-cut': union('assembly', smooth_subtract('recess', sphere('body', (0, 0, 0), .25),
            sphere('tool', (.1, 0, .2), .15), .04, cut_material='blue'), sphere('plug', (.1, 0, .2), .035)),
        'smooth-inherit-cut': smooth_subtract('recess', sphere('body', (0, 0, 0), .25),
            sphere('tool', (.1, 0, .2), .15, 'blue'), .04),
        'local-cut': union('assembly', subtract('housing', sphere('body', (0, 0, 0), .25),
            sphere('cut', (.1, 0, .2), .15), cut_material='blue'), sphere('plug', (.1, 0, .2), .04, 'blue')),
        'inherit-cut': subtract('body-cut', sphere('body', (0, 0, 0), .25), sphere('cut', (.1, 0, .2), .15, 'blue')),
        'smooth': smooth_union('blend', sphere('left', (-.16, 0, 0), .15), sphere('right', (.16, 0, 0), .15, 'blue'), .12, True),
        'nested-blend': smooth_union('outer', smooth_union('inner', sphere('a', (-.08, 0, 0), .13),
            sphere('b', (.08, 0, 0), .13, 'blue'), .12, True), sphere('c', (0, .14, 0), .11), .08, True),
        'transform': union('transform', box('box', (-.12, 0, 0), (.07, .1, .08), .01),
            cylinder('tube', (.12, 0, 0), .07, .12, 'blue'), position=(.08, .02, 0), scale=.8,
            rotation_xyzw=rotation((0, 0, 1), 27)),
        'thin-gap': union('gap', sphere('left', (-.102, 0, 0), .1), sphere('right', (.102, 0, 0), .1, 'blue')),
        'workspace-clip': sphere('clipped', (.48, 0, 0), .1),
        'maximum': union('maximum', *[sphere(f's{i}', ((i % 4 - 1.5) * .12, (i // 4 % 4 - 1.5) * .12,
            (i // 16 - 1.5) * .12), .018, 'blue' if i % 2 else 'red') for i in range(64)]),
        'maximum-128': union('maximum-128', *[sphere(f's{i}', ((i % 8 - 3.5) * .08,
            (i // 8 % 4 - 1.5) * .10, (i // 32 - 1.5) * .10), .018,
            'blue' if i % 2 else 'red') for i in range(128)]),
    }
    for name, tree in fixtures.items():
        path = out / f'fixtures/{name}.json'
        write_scene(path, name, materials, tree)
        run(f'fixture-{name}', ['--authored-scene', path, '--validate-field', '--sun-azimuth', 160,
            '--width', 640, '--height', 480, '--exposure', .7], config=False)
    for target in ('cubey_project_sdf_recipe_tests', 'cubey_project_sdf_authored_tests'):
        with (out / f'logs/{target}.log').open('w') as stream:
            subprocess.run([str(binary.with_name(target))], cwd=root, check=True, stdout=stream, stderr=subprocess.STDOUT)
    run('lifecycle', ['--validate-field', '--validate-batches', '--validate-subtractions', '--width', 320, '--height', 320])
    for name, options in (
        ('hero', ['--validate-field', '--export-authored', out / 'scenes/robot.json']),
        ('front', ['--yaw', 0, '--pitch', .04]),
        ('side', ['--yaw', math.pi / 2, '--pitch', .10]),
        ('rear', ['--yaw', math.pi + .35, '--pitch', .17]),
        ('clay', ['--clay', '--roughness', .65]),
        ('parts', ['--debug', 5]), ('base-color', ['--debug', 6]), ('roughness', ['--debug', 7]),
        ('normals', ['--debug', 1]), ('detail', ['--distance', .87, '--target-y', .22]),
        ('reloaded', ['--authored-scene', out / 'scenes/robot.json', '--validate-field', '--export-authored', out / 'scenes/reloaded.json']),
    ):
        run(name, options)
    assert (out / 'scenes/robot.json').read_bytes() == (out / 'scenes/reloaded.json').read_bytes()
    manifest['replay_scene_bytes_identical'] = True
    run('legacy-cut', ['--scene', 'cut', '--validate-field', '--width', 640, '--height', 480], config=False)
    run('legacy-sticks', ['--config', root / 'projects/sdf/presets/stick-ball.json', '--motion', 'static', '--validate-field', '--width', 640, '--height', 480], config=False)
    run('incompatible-motion', ['--motion', 'carve'], error='Authored scenes support static/orbit')
    for name, options in (
        ('primary', ['--shadows', 0]), ('hard', ['--shadows', 1]), ('clay', ['--shadows', 1, '--clay']),
    ):
        prefix = out / f'profiles/{name}'
        row = run(f'profile-{name}', ['--headless-render-only', '--frames', 180, '--profile-warmup-frames', 30,
            '--profile-output', prefix, '--width', 1920, '--height', 1080, '--supersample', 1, *options], capture=False)
        frames, stages = {}, {}
        with Path(f'{prefix}.passes.csv').open() as stream:
            for item in csv.DictReader(stream):
                if item['kind'] == 'gpu':
                    duration = float(item['duration_ms'])
                    index = int(item['frame_index'])
                    frames[index] = frames.get(index, 0) + duration
                    stages.setdefault(item['label'], []).append(duration)
        row['gpu_frame'] = distribution(list(frames.values()))
        row['gpu_stages'] = {key: distribution(values) for key, values in stages.items()}
        save()
    if args.video:
        run('video', ['--motion', 'orbit', '--capture', 'video', '--fps', 30, '--frames', 480,
                     '--width', 1280, '--height', 960], video=True)
        manifest['video_probe'] = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-show_entries',
            'stream=codec_name,width,height,nb_frames,r_frame_rate', '-show_entries', 'format=duration', '-of', 'json', str(out / 'turntable.mp4')], text=True))
        assert manifest['video_probe']['format']['duration'] == '16.000000'
        assert manifest['video_probe']['streams'][0]['nb_frames'] == '480'
        check_video(out / 'turntable.mp4')
        for seconds in (0, 4, 8, 12, 15.9):
            subprocess.run(['ffmpeg', '-v', 'error', '-ss', str(seconds), '-i', str(out / 'turntable.mp4'),
                '-frames:v', '1', '-y', str(out / f'captures/video-{seconds}.png')], check=True)
    shutil.copy2(root / 'projects/sdf/tools/author_robot.py', out / 'scenes/author_robot.py')
    shutil.copy2(root / 'projects/sdf/tools/sdf_author.py', out / 'scenes/sdf_author.py')
    save()
    print(out / 'manifest.json', flush=True)


if __name__ == '__main__':
    main()
