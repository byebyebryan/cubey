"""Author opt-in groundless clear-atmosphere inputs for all three consumers."""
import argparse
import copy
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
RT = 'projects/raytracing/presets/material-workshop'
PBR = 'projects/pbr_materials/presets/rt-comparison'
SDF = 'projects/sdf/presets/rt-comparison'
SCENES = 'projects/sdf/scenes/rt-comparison'


def documents():
    entries = json.loads((ROOT/'assets/materials/catalog.json').read_text())['entries']
    files, specimens = {}, []
    identity = [1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]
    materials, spheres = [], []
    for i, entry in enumerate(entries):
        path = ROOT/'assets/materials'/entry['recipe']
        recipe = json.loads(path.read_text())
        name = entry['id']
        position = [((i % 4)-1.5)*.252, (1-i//4)*.195, 0]
        effective = copy.deepcopy(recipe)
        domain = recipe.get('material_from_object', identity).copy()
        for row in range(4):
            domain[12+row] -= sum(domain[col*4+row]*position[col] for col in range(3))
        effective['material_from_object'] = domain
        materials.append(dict(name=name, recipe=effective))
        spheres.append(dict(name=name+'-sphere',type='sphere',radius=.075,position=position,material=name))
        files[f'{SCENES}/{name}.json'] = dict(format='cubey.sdf.scene',version=3,name=name+' comparison',
            environment_only_gallery=True,materials=[dict(name=name,recipe=recipe)],
            root=dict(name=name+'-sphere',type='sphere',radius=.25,position=[0,0,0],material=name))
        specimens.append(dict(id=name,position=position,radius=.075,
            source_recipe=str(path.relative_to(ROOT)),source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
            canonical_material_from_object=recipe.get('material_from_object', identity),
            sdf_effective_material_from_world=domain))
    files[f'{SCENES}/gallery.json'] = dict(format='cubey.sdf.scene',version=3,
        name='RT workshop comparison: twelve canonical recipes',environment_only_gallery=True,
        materials=materials,root=dict(name='material-gallery',type='union',children=spheres))
    rt = dict(scene='materials',material='gallery',environment='atmosphere',
        **{'sphere-density':'dense','reflection-source':'environment','transmission-source':'environment',
           'throughput-cutoff':0,'sun-elevation':.13962634015954636,'sun-azimuth':-.4363323129985824,
           'shadows':False,'exposure':0,'distance':.855,'yaw':0,'pitch':0,'fov':45,'samples':256,'seed':7})
    pbr = dict(presentation='library',material='gallery',environment='clear',sphere_density='dense',
               lighting_cycle='frozen',distance=.855,yaw=0,pitch=0,pbr=dict(exposure=0))
    sdf = {'authored-scene':f'{SCENES}/gallery.json','render-mode':'analytic',
           'motion':'static','duration':8,'lighting':'material-atmosphere','lighting-cycle':'frozen',
           'clouds':False,'sun-elevation':8,'sun-azimuth':-155,'exposure':0,
           'distance':.855,'yaw':0,'pitch':0,'target-y':0,'shadows':0,
           'ground':False,'glass-crossings':16,'environment-extent':128}
    for label, directory, base in [('rt',RT,rt),('forward',PBR,pbr),('sdf',SDF,sdf)]:
        files[f'{directory}/gallery-clear.json'] = base
        for motion in ['orbit','dawn','dusk']:
            preset = copy.deepcopy(base)
            if label=='rt':
                preset['presentation']='library'
                preset['lighting-cycle']='frozen' if motion=='orbit' else motion
                preset['orbit-degrees']=20 if motion=='orbit' else 0
            elif label=='forward':
                preset['lighting_cycle']='frozen' if motion=='orbit' else motion
                preset['orbit_degrees']=20 if motion=='orbit' else 0
            else:
                preset['lighting-cycle']='frozen' if motion=='orbit' else motion
                preset['motion']='orbit' if motion=='orbit' else 'static'
                preset['orbit-turns']=1/18 if motion=='orbit' else 0
            files[f'{directory}/gallery-{motion}.json'] = preset
    scene = copy.deepcopy(rt)
    scene.update({'reflection-source':'scene','transmission-source':'scene','shadows':True})
    files[f'{RT}/gallery-scene.json'] = scene
    files[f'{RT}/manifest.json'] = dict(format='cubey.material.comparison',version=1,
        scope='Opt-in material comparison. Canonical assets and existing media/defaults are unchanged.',
        layout=dict(columns=4,rows=3,spacing=[.252,.195],radius=.075,ground=False),specimens=specimens,
        camera=dict(distance=.855,yaw=0,pitch=0,vertical_fov_degrees=45,
                    isolated=dict(radius=.25,distance=1.05,yaw=0,pitch=.15),
                    conversion='SDF pitch = -common pitch; forward/RT pitch = common pitch'),
        geometry=dict(mesh=dict(latitude_segments=128,longitude_segments=256),
            sdf='Analytic source SDF sphere tracing; no mesh conversion. Geometric and filtered optical approximations differ.',
            rt='Shared mesh with reported seam snapping, pole-degenerate removal and outward boundary winding'),
        environment=dict(sun_elevation_degrees=8,sun_azimuth_degrees=-155,altitude_m=2,
            gain=1,exposure_ev=0,visible_background='SkyOnlyNoGroundOcclusion',reflection_probe='Ground',
            probe_extent=128,probe_mips=5,probe_hz_frozen=4,probe_hz_cycling=30,clouds=False,moon=False,night=False,
            display='Shared ACES HDR post; linear accumulation before exposure/display conversion'),
        timeline=dict(duration_seconds=8,authored_time='8 * output_index / (output_count - 1)',
            orbit_degrees=20,orbit_interpolation='linear',dawn=[-1.2,14],dusk=[14,-1.2],
            elevation_interpolation='shared smoothstep environment transition',exposure='shared atmosphere auto exposure for cycles',
            rt_sampling='Freeze authored state, complete 256 primary/history iterations, resolve once; then advance output time'),
        transport=dict(comparison='No local specimen shadows; environment after body exit',
            scene='Separate RT-only scene-reflection/refraction preset with local visibility',
            forward='Estimated thickness and shared filtered environment optics',
            sdf='Measured central paths and shared filtered environment optics; environment after exit',
            rt='Geometric interfaces, sampled rough transmission; ten conditional rough-glass trials per primary, bounded Fresnel chains',
            limits=[8,32,128],diffuse_gi=False))
    return files


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--check',action='store_true');a=p.parse_args()
    files=documents()
    for name,value in files.items():
        path=ROOT/name;text=json.dumps(value,indent=2)+'\n'
        if a.check:
            if not path.exists() or path.read_text()!=text:raise SystemExit('Stale comparison input: '+name)
        else:
            path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text)
    print(('Checked' if a.check else 'Wrote'),len(files),'comparison inputs')


if __name__=='__main__':main()
