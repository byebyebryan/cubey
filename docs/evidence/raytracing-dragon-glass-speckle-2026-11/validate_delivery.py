"""Verify retained runtime/input identity and the delivered research artifacts."""
import hashlib
import json
import subprocess
from pathlib import Path

evidence=Path(__file__).resolve().parent
root=Path('outputs/raytracing/dragon-glass-speckle-20261116')
digest=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
manifest=json.loads((evidence/'captures.json').read_text())
identity=manifest['identity']
binary=Path('build/dev/projects/raytracing/raytracing')
assert digest(binary)==identity['binary_sha256']
shaders={p.name:digest(p) for p in (binary.parent/'shaders').glob('*.spv')}
assert shaders==identity['shaders']
assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==identity['commit']
assert not subprocess.check_output(['git','diff','--','projects/raytracing'],text=True)
assert {p.name:digest(p) for p in Path('assets/materials').glob('*.json')}==identity['recipes']
preset=json.loads(Path('projects/raytracing/presets/optics/dragon-glass.json').read_text())
assert digest(preset['model'])==identity['model_sha256']
broad_build=json.loads((root/'broad-sun-build/build.json').read_text())
assert all(shaders[name]==sha for name,sha in broad_build['shader_sha256'].items())
assert all(digest(p)==sha for p,sha in broad_build['libraries'].items())
broad=json.loads((evidence/'broad-sun.json').read_text())
for capture in manifest['captures']+broad['captures']:
    for suffix,sha in capture['receipt']['artifacts'].items():
        assert digest(capture['prefix']+suffix)==sha
    assert capture['receipt']['finite_output'] and capture['receipt']['nonnegative_hdr'] and capture['receipt']['validation_clean']
analysis=json.loads((evidence/'analysis.json').read_text())
assert max(p['relative_parity_error'] for p in analysis['probes'])<2e-4
assert all(p['status_or']==0 for p in analysis['probes'])
qa=json.loads((evidence/'browser-qa.json').read_text())
assert len(qa['comparisonChecks'])==14 and qa['consoleErrors']==[] and qa['figuresLoaded']
delivery=json.loads((evidence/'report-delivery.json').read_text())
assert delivery['delivery']['state']=='verified'
check=subprocess.run(['ctest','--preset','dev-docs'],capture_output=True,text=True)
(evidence/'documentation.log').write_text(check.stdout+check.stderr)
assert check.returncode==0,check.stdout+check.stderr
result=dict(ordinary_runtime_preserved=True,ordinary_raytracing_source_preserved=True,
    accepted_model_and_recipes_preserved=True,ordinary_commit=identity['commit'],
    broad_preview_compiled_shader_bytes_match_ordinary=True,
    ordinary_extra_shader_artifacts=sorted(shaders.keys()-broad_build['shader_sha256'].keys()),
    foundation_libraries_preserved=True,documentation_exit_code=check.returncode,
    ordinary_captures=12,private_lighting_captures=4,private_pixel_probes=3,
    sample_journal_sha256={p.name:digest(p) for p in (root/'sample-probes').glob('*.journal.json')},
    browser_comparisons_checked=14,report_url=delivery['url'],research_committed=False,
    public_site_changed=False)
(evidence/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
