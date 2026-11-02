#!/usr/bin/env python3
"""Compile/link only private targets using existing Release Ninja commands.

The study's normal CMake target depends on the public SDF target. Executing
the generated compile/link commands directly preserves the frozen public
binary/SPVs while still using the configured compiler, options and libraries.
Regenerate build.ninja first if CMakeLists.txt changed.
"""
import argparse
import concurrent.futures
import json
from pathlib import Path
import shlex
import subprocess
from edit_service_loop import ROOT, digest, save


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--receipt', type=Path, required=True)
    args = p.parse_args()
    assert not args.receipt.exists(), 'preserve previous build receipt'
    build = ROOT / 'build/release'
    targets = ['projects/sdf/cubey_project_sdf_cached_stroke_study',
               'projects/sdf/cubey_project_sdf_stroke_preparation_tests',
               'projects/sdf/cubey_project_sdf_authored_tests']
    def command(target):
        line = subprocess.check_output(['ninja', '-t', 'commands', target], cwd=build, text=True).splitlines()[-1]
        result = shlex.split(line)
        if result[:2] == [':', '&&']: result = result[2:]
        if result[-2:] == ['&&', ':']: result = result[:-2]
        assert '&&' not in result
        return result
    links = [(t, command(t)) for t in targets]
    objects = sorted({a for _, link in links for a in link if a.endswith('.cpp.o')})
    commands = [(o, command(o)) for o in objects]
    def execute(item):
        target, cmd = item
        r = subprocess.run(cmd, cwd=build, capture_output=True, text=True)
        return dict(target=target, command=cmd, exit_code=r.returncode, log=r.stdout+r.stderr)
    receipt = dict(source_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                   harness_sha256=digest(__file__), objects=[], links=[], complete=False)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for r in pool.map(execute, commands):
            receipt['objects'].append(r)
            if r['exit_code']: print(r['target'], r['log'], flush=True)
    save(args.receipt, receipt)
    assert all(r['exit_code'] == 0 for r in receipt['objects']), 'compile failed'
    for item in links:
        result = execute(item); receipt['links'].append(result)
        save(args.receipt, receipt)
        assert result['exit_code'] == 0, result['log']
    baseline = json.loads((ROOT / 'outputs/sdf/interactive-edit-loop-20261031/baseline.json').read_text())
    assert digest(build / 'projects/sdf/sdf') == baseline['binaries']['sdf']
    assert all(digest(build / 'projects/sdf/shaders' / n) == h for n, h in baseline['original_shaders'].items())
    receipt.update(complete=True, protected_shaders_unchanged=len(baseline['original_shaders']),
                   public_executable_unchanged=True,
                   binaries={t: digest(build / t) for t in targets})
    save(args.receipt, receipt)
    print('Compiled', len(objects), 'objects and linked three private targets; public binary/SPVs unchanged.')


if __name__ == '__main__': main()
