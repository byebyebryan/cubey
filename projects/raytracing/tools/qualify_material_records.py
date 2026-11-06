"""Check actual RT recipe SSBO evaluation against the shared CPU evaluator."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,default=Path('build/dev/projects/raytracing/raytracing'))
    p.add_argument('--reference',type=Path,default=Path('build/dev/projects/raytracing/cubey_project_raytracing_material_reference'))
    p.add_argument('--output',type=Path,required=True)
    args = p.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True,exist_ok=False)
    custom = json.loads(Path('assets/materials/marble.json').read_text())
    custom['material_from_object'] = [1.2,.2,0,0, .35,.7,.1,0, 0,.15,1.4,0, .03,-.02,.01,1]
    domain = output/'nonidentity-marble.json'
    domain.write_text(json.dumps(custom,indent=2)+'\n')
    results = []
    for name,extra in [('catalog',[]),('patterns-off',['--no-pattern']),
                       ('bump-off',['--no-bump']),('nonidentity-domain',['--recipe-input',str(domain)])]:
        out = output/name
        out.mkdir()
        command = [str(args.binary.resolve()),'--headless','--frames','1','--scene','materials',
                   '--material-contract','--samples','1','--width','96','--height','96',
                   '--require-validation','--optics-output',str(out/'probe.bin'),
                   '--metrics',str(out/'metrics.json'),'--output',str(out/'probe.png'),*extra]
        run = subprocess.run(command,capture_output=True,text=True,timeout=180,
                             env=dict(os.environ,VK_LAYER_VALIDATE_SYNC='1'))
        log = run.stdout+run.stderr
        (out/'native.log').write_text(log)
        assert run.returncode == 0 and not any(t in log.lower() for t in
            ['vuid-','hazard detected','vulkan validation error']),log
        reference = [str(args.reference.resolve()),str(out/'metrics.json'),str(out/'probe.bin'),str(out/'receipt.json')]
        cpu = subprocess.run(reference,capture_output=True,text=True,timeout=120)
        (out/'reference.log').write_text(cpu.stdout+cpu.stderr)
        assert cpu.returncode == 0,cpu.stdout+cpu.stderr
        receipt = json.loads((out/'receipt.json').read_text())
        results.append(dict(name=name,command=command,reference_command=reference,**receipt))
        print(name,'passed',receipt['samples'],'points; max component',
              max(c['maximum_component_error'] for c in receipt['cases']))
    summary = dict(passed=True,cases=results,
        binary_sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest(),
        reference_sha256=hashlib.sha256(args.reference.read_bytes()).hexdigest(),
        shader_sha256=hashlib.sha256((args.binary.parent/'shaders/trace.rgen.spv').read_bytes()).hexdigest(),
        source_base=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
        scope='GPU record layout, shared evaluator, pattern/bump controls and nonidentity affine domain; no image transport claim')
    (output/'receipt.json').write_text(json.dumps(summary,indent=2)+'\n')


if __name__ == '__main__':
    main()
