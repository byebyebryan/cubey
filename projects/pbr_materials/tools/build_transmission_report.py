"""Build the qualified forward-transmission internal comparison bundle."""
import argparse,hashlib,json,re,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
def read(p):return json.loads(p.read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--evidence',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 root=a.evidence.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);assets=out/'assets';assets.mkdir();data=out/'data';data.mkdir()
 receipts={k:read(root/k/'receipt.json') for k in ['stills','films','integration','native']}
 assert all(v['passed'] for v in receipts.values())
 for k,v in receipts.items():(data/(k+'.json')).write_text(json.dumps(v,indent=2)+'\n')
 f0=read(ROOT/'docs/evidence/forward-transmission-2026-11/f0.json');assert f0['passed'];(data/'f0.json').write_text(json.dumps(f0,indent=2)+'\n')
 log=(root/'dev-suite.log').read_text();match=re.search(r'100% tests passed(?:, 0 tests failed)?(?: out of| out of) (\d+)',log);assert match and '***Failed' not in log
 test_count=int(match[1])
 materials=[dict(id=e['id'],label=e.get('label',e['id'])) for e in read(ROOT/'assets/materials/catalog.json')['entries']]
 for name in [*[m['id'] for m in materials],'gallery']:
  for mode in ['screen','forward','sdf','rt']:
   src=root/('integration' if mode=='screen' else 'stills')/(mode+'-'+name+'.png');shutil.copy2(src,assets/src.name)
 for mode in ['screen','forward','sdf','rt']:
  for motion in ['orbit','dawn','dusk']:
   for suffix in ['.mp4','-poster.png']:
    src=root/('integration' if mode=='screen' else 'films')/(mode+'-'+motion+suffix);shutil.copy2(src,assets/src.name)
 f0root=ROOT/f0['raw_evidence']
 for name in ['gradient-screen.png','gradient-environment.png']:shutil.copy2(f0root/name,assets/name)
 shutil.copy2(ROOT/'docs/evidence/forward-transmission-research-2026-11/sphere-directions.png',assets/'sphere-directions.png')
 native=root/'native/environment.png';assert native.is_file();shutil.copy2(native,assets/'native.png')
 gradient=next(r for r in f0['routing'] if r['control']=='gradient')
 profiles=receipts['integration']['profiles'];costs=[]
 for source in ['screen','environment']:
  rows=[r for r in profiles if r['source']==source];assert len(rows)==3
  costs.append(dict(source=source.title(),p95=max(r['gpu']['material workshop whole scene']['p95_ms'] for r in rows)))
 gltf=[r for r in receipts['integration']['results'] if r['name'].startswith('gltf-')]
 calibrated=[r for r in gltf if r.get('semantic_conformance')]
 assert len(calibrated)==5
 payload=dict(materials=materials,gain=gradient['screen_error']['mean']/gradient['environment_error']['mean'],old_error=gradient['screen_error']['mean'],new_error=gradient['environment_error']['mean'],tests=test_count,costs=costs,
  source=receipts['stills']['identity']['source'],gpu=' · driver '.join(v.strip() for v in receipts['stills']['identity']['gpu']['csv'].split(',')[:2]),
  native_text='Native source selection, Save View, lighting changes, resize, orbit and zoom were exercised on an isolated display with synchronization validation. This is automation evidence; human visual acceptance remains separate.',
  checks=[dict(title='Optical controls',text='Four IORs, independent observed-normal geometry, directional-gradient and constant-radiance controls. Original ideal-normal limits remain disclosed.'),dict(title='Legacy retention',text='Five 1080p controls captured before implementation remain byte-identical in screen mode. The historical 640p bronze control retains a one-code residual in one pixel, also present with its exact old shader.'),dict(title='glTF compatibility',text=f'{len(gltf)} pinned native asset captures and five calibrated semantic conformance checks, including roughness, thin-wall, attenuation, order and dispersion.'),dict(title='View persistence',text='Both source choices save and replay exactly. The saved view keeps camera/source; material edits still require a recipe export.'),dict(title='Matched media',text='All twelve materials, four-column screen/environment/SDF/RT stills and twelve native 1080p films. Canonical recipe identities are retained.'),dict(title='Native lifecycle',text='Source and lighting changes, resize, input and native Save View complete with clean synchronization validation.')])
 template=Path(__file__).with_name('transmission_report.html').read_text();(out/'index.html').write_text(template.replace('__DATA__',json.dumps(payload).replace('</',r'<\/')))
 shutil.copy2(root/'dev-suite.log',data/'dev-suite.log')
 files={str(f.relative_to(out)):sha(f) for f in out.rglob('*') if f.is_file()};(out/'artifact-manifest.json').write_text(json.dumps(dict(renderer_source=payload['source'],files=files),indent=2)+'\n')
 print(out)
if __name__=='__main__':main()
