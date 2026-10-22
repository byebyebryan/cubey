#!/usr/bin/env python3
"""Capture and qualify the authored robot's native edit-by-edit presentation."""
import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import subprocess
from qualify_authored import check_video


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--video', action='store_true')
    parser.add_argument('--regressions', action='store_true')
    parser.add_argument('--config', type=Path, default=Path('projects/sdf/presets/survey-robot-build.json'))
    parser.add_argument('--hidden-preview-node', action='append', default=[],
                        help='Named cutter expected to be occluded from the fixed camera')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    out = args.output.resolve()
    if out.exists():
        raise RuntimeError('Use a fresh qualification output directory')
    for folder in ('captures', 'logs', 'configs', 'scenes'):
        (out / folder).mkdir(parents=True)
    binary = root / 'build/dev/projects/sdf/sdf'
    preset = args.config.resolve()
    config = json.loads(preset.read_text())
    scene = json.loads((root / config['authored-scene']).read_text())
    history = scene['history']
    total = .65 + sum(step['seconds'] for step in history)
    construction, duration, fps = config['construction-seconds'], config['duration'], 30
    frames = int(duration * fps)
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = dict(captured_utc=dt.datetime.now(dt.timezone.utc).isoformat(),
        source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        source_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=root, text=True).strip()),
        binary_sha256=sha(binary), shader_sha256={p.name: sha(p) for p in sorted((binary.parent / 'shaders').glob('*.spv'))},
        device=subprocess.check_output(['nvidia-smi', '--query-gpu=name,driver_version', '--format=csv,noheader'], text=True).strip(),
        history=history, construction_seconds=construction, duration=duration, fps=fps, frames=frames,
        config=str(preset.relative_to(root) if preset.is_relative_to(root) else preset),
        runs={}, steps=[], previews=[])
    def save():
        (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    def run(name, options=(), expected_step=None, video=False):
        output = out / ('native.mp4' if video else f'captures/{name}.png')
        command = [str(binary), '--headless', '--require-validation', '--config', str(preset),
                   '--width', '960', '--height', '960', *map(str, options), '--output', str(output)]
        subprocess.run([*command, '--write-config-template', str(out / f'configs/{name}.json')], cwd=root,
                       check=True, stdout=subprocess.DEVNULL)
        result = subprocess.run(command, cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (out / f'logs/{name}.log').write_text(result.stdout)
        if result.returncode or 'validation error' in result.stdout.lower():
            raise RuntimeError(name + ': ' + result.stdout)
        if expected_step is not None:
            assert f'sdf: history initial step={expected_step}/{len(history)}' in result.stdout, result.stdout
        row = dict(command=command, exit_code=result.returncode, capture=str(output.relative_to(out)),
            capture_sha256=sha(output), stats=[s for s in result.stdout.splitlines() if s.startswith('sdf:')])
        manifest['runs'][name] = row
        save()
        print(name, 'passed', flush=True)
        return row
    # The native check independently compiles/evaluates every prefix, then
    # revisits the midpoint, empty and final states on the same field instance.
    validation = run('history-validation', ['--time', construction, '--validate-history', '--validate-field'], len(history))
    assert 'sdf: authored history passed' in '\n'.join(validation['stats'])
    manifest['validated_states'] = [int(re.search(r'step=(\d+)', s)[1]) for s in validation['stats'] if s.startswith('sdf: history validated')]
    assert manifest['validated_states'] == [*range(len(history) + 1), len(history)//2, 0, len(history)]
    start = .65
    for index in range(len(history) + 1):
        step = history[index - 1] if index else None
        time = (start + .7 * step['seconds']) / total * construction if step else 0
        run(f'step-{index:02}', ['--time', time], index)
        manifest['steps'].append(dict(index=index, time=time, label=step['label'] if step else 'Empty workspace',
                                     capture=f'captures/step-{index:02}.png'))
        if step:
            start += step['seconds']
    # Identify cutters from the actual tree, not the English label.
    cutters = set()
    def walk(node, negative=False):
        if 'children' not in node:
            if negative: cutters.add(node['name'])
        else:
            for i, child in enumerate(node['children']):
                walk(child, negative != (node['type'] == 'subtract' and i == 1))
    walk(scene['root'])
    start = .65
    for index, step in enumerate(history):
        if step['node'] in cutters:
            time = (start + .175 * step['seconds']) / total * construction
            run(f'preview-{index+1:02}', ['--time', time], index)
            off = run(f'preview-{index+1:02}-off', ['--time', time, '--no-cutter-cue'], index)
            on = manifest['runs'][f'preview-{index+1:02}']
            identity = lambda row: next(s for s in row['stats'] if s.startswith('sdf: authored geometry='))
            assert identity(on) == identity(off)
            manifest['previews'].append(dict(index=index+1, node=step['node'], time=time, label=step['label'],
                on=on['capture'], off=off['capture']))
        start += step['seconds']
    final = run('static-final', ['--motion', 'static', '--export-authored', out / 'scenes/robot.json', '--validate-field'])
    identity = lambda row: next(s for s in row['stats'] if s.startswith('sdf: authored geometry='))
    assert identity(final) == identity(manifest['runs'][f'step-{len(history):02}'])
    manifest['final_matches_static_fingerprints'] = True
    def pixels(path):
        return subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(out / path), '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'])
    def compare(left, right):
        a, b = pixels(left), pixels(right)
        assert len(a) == len(b)
        delta = [abs(x-y) for x, y in zip(a, b)]
        return dict(max_abs=max(delta), mean_abs=sum(delta)/len(delta), changed=sum(d>0 for d in delta),
                    fraction_above_1=sum(d>1 for d in delta)/len(delta))
    metrics = compare(final['capture'], manifest['runs'][f'step-{len(history):02}']['capture'])
    metrics['limits'] = dict(max_abs=16, mean_abs=.002, fraction_above_1=.0001)
    assert all(metrics[k] <= v for k, v in metrics['limits'].items()), metrics
    manifest['final_image_comparison'] = metrics
    for preview in manifest['previews']:
        preview['image_comparison'] = compare(preview['on'], preview['off'])
        # A fixed construction camera can hide rear-facing cutters. Record
        # visibility separately; field/fingerprint checks still apply to all.
        preview['visible_from_capture_camera'] = preview['image_comparison']['changed'] > 100
        assert preview['visible_from_capture_camera'] or preview['node'] in args.hidden_preview_node, preview
    assert set(args.hidden_preview_node).issubset(cutters)
    manifest['hidden_preview_allowlist'] = args.hidden_preview_node
    if args.video:
        row = run('video', ['--capture', 'video', '--fps', fps, '--frames', frames,
                            '--width', 1280, '--height', 960], video=True)
        visited = [int(re.search(r'step=(\d+)', s)[1]) for s in row['stats'] if s.startswith('sdf: history step=')]
        assert visited == list(range(1, len(history)+1)), visited
        builds = [s for s in row['stats'] if s.startswith('sdf: authored instructions=')]
        assert len(builds) == len(history)+1
        manifest['video_builds'] = len(builds)
        check_video(out / 'native.mp4', expected_frames=frames)
        events = []
        for frame in range(frames):
            time = duration * frame / (frames - 1)
            cursor, prefix, preview_index = .65, 0, None
            for index, step in enumerate(history):
                progress = (time / construction * total - cursor) / step['seconds']
                if progress < 0: break
                if step['node'] in cutters and progress < .35:
                    preview_index = index
                    break
                prefix = index+1
                cursor += step['seconds']
            if time >= construction:
                text = f'FINISHED  |  {len(history)} edits  |  Orbiting the robot'
            elif preview_index is not None:
                text = f'CUT PREVIEW  {preview_index+1:02} / {len(history)}  |  {history[preview_index]["label"]}'
            else:
                label = history[prefix-1]['label'] if prefix else 'Empty workspace'
                text = f'EDIT  {prefix:02} / {len(history)}  |  {label}'
            if not events or events[-1]['text'] != text:
                events.append(dict(time=frame/fps, text=text, prefix=prefix))
        def stamp(seconds):
            hundredths = round(seconds * 100)
            return f'{hundredths//360000}:{hundredths//6000%60:02}:{hundredths//100%60:02}.{hundredths%100:02}'
        header = ('[Script Info]\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 960\n'
            '[V4+ Styles]\nFormat: Name,Fontname,Fontsize,PrimaryColour,SecondaryColour,OutlineColour,BackColour,Bold,Italic,Underline,StrikeOut,ScaleX,ScaleY,Spacing,Angle,BorderStyle,Outline,Shadow,Alignment,MarginL,MarginR,MarginV,Encoding\n'
            'Style: Default,DejaVu Sans,26,&H00FFFFFF,&H00FFFFFF,&HBB172730,&HBB172730,0,0,0,0,100,100,0,0,3,12,0,1,38,38,26,1\n'
            '[Events]\nFormat: Layer,Start,End,Style,Name,MarginL,MarginR,MarginV,Effect,Text\n')
        for i, event in enumerate(events):
            end = events[i+1]['time'] if i+1 < len(events) else duration
            header += f'Dialogue: 0,{stamp(event["time"])},{stamp(end)},Default,,0,0,0,,{event["text"]}\n'
        (out / 'captions.ass').write_text(header)
        manifest['video_events'] = events
        subprocess.run(['ffmpeg', '-v', 'error', '-i', 'native.mp4', '-vf', 'subtitles=captions.ass',
            '-c:v', 'libx264', '-crf', '18', '-preset', 'medium', '-pix_fmt', 'yuv420p', '-movflags', '+faststart',
            'presentation.mp4'], cwd=out, check=True)
        manifest['presentation_sha256'] = sha(out / 'presentation.mp4')
        for time in (1.2, 4.73, 5.8, 10.6, 18, 26, 35):
            subprocess.run(['ffmpeg', '-v', 'error', '-ss', str(time), '-i', str(out / 'presentation.mp4'),
                '-frames:v', '1', str(out / f'captures/video-{time}.png')], check=True)
    save()
    if args.regressions:
        subprocess.run(['python3', str(root / 'projects/sdf/tools/qualify_authored.py'), '--output', str(out / 'regressions')], cwd=root, check=True)
    print(out / 'manifest.json', flush=True)


if __name__ == '__main__':
    main()
