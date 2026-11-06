"""Capture and inspect the RT workshop on an explicitly owned Xvfb display."""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, default=Path('build/dev/projects/raytracing/raytracing'))
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--exercise', action='store_true')
    p.add_argument('--catalog-exercise',action='store_true')
    p.add_argument('--gallery-exercise',action='store_true')
    p.add_argument('--optics-exercise',action='store_true')
    p.add_argument('--material',default='bronze')
    args = p.parse_args()
    if os.environ.get('CUBEY_RT_PRIVATE_XVFB') != '1':
        raise RuntimeError('Use an owned xvfb-run with CUBEY_RT_PRIVATE_XVFB=1')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    x, t = C.CDLL('libX11.so.6'), C.CDLL('libXtst.so.6')
    D, W, U, I = C.c_void_p, C.c_ulong, C.c_uint, C.c_int
    bindings = [
        (x,'XOpenDisplay',[C.c_char_p],D),(x,'XDefaultRootWindow',[D],W),
        (x,'XDisplayWidth',[D,I],I),(x,'XDisplayHeight',[D,I],I),
        (x,'XQueryTree',[D,W,C.POINTER(W),C.POINTER(W),C.POINTER(C.POINTER(W)),C.POINTER(U)],I),
        (x,'XFetchName',[D,W,C.POINTER(C.c_char_p)],I),(x,'XFree',[D],I),
        (x,'XSetInputFocus',[D,W,I,W],I),(x,'XSync',[D,I],I),
        (x,'XStringToKeysym',[C.c_char_p],W),(x,'XKeysymToKeycode',[D,W],C.c_ubyte),
        (t,'XTestFakeKeyEvent',[D,U,I,W],I),
        (t,'XTestFakeButtonEvent',[D,U,I,W],I),
        (t,'XTestFakeMotionEvent',[D,I,I,I,W],I),
        (x,'XResizeWindow',[D,W,U,U],I),(x,'XCloseDisplay',[D],I),
        (x,'XTranslateCoordinates',[D,W,W,I,I,C.POINTER(I),C.POINTER(I),C.POINTER(W)],I),
        (x,'XInternAtom',[D,C.c_char_p,I],W),(x,'XSendEvent',[D,W,I,C.c_long,D],I),
    ]
    for lib,name,parameters,result in bindings:
        f = getattr(lib,name)
        f.argtypes,f.restype = parameters,result
    display = x.XOpenDisplay(None)
    if not display:
        raise RuntimeError('Owned display unavailable')
    if x.XDisplayWidth(display,0)<1200 or x.XDisplayHeight(display,0)<820:
        x.XCloseDisplay(display)
        raise RuntimeError('Owned Xvfb must cover 1200x820; use -s "-screen 0 1600x1200x24"')
    root,window = x.XDefaultRootWindow(display),None
    env = dict(os.environ,XDG_SESSION_TYPE='x11',VK_LAYER_VALIDATE_SYNC='1')
    env.pop('WAYLAND_DISPLAY',None)
    command = [str(args.binary.resolve()),'--scene','materials','--material',args.material,'--samples','32',
               '--width','1000','--height','760','--require-validation',
               '--metrics',str(out/'metrics.json'),'--recipe-output',str(out/'edited-recipe.json'),
               '--save-config',str(out/'view.json')]
    log = out/'native.log'
    with log.open('w') as stream:
        process = subprocess.Popen(command,env=env,stdout=stream,stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic()+45
            while window is None:
                if process.poll() is not None:
                    raise RuntimeError(log.read_text())
                a,b,children,count = W(),W(),C.POINTER(W)(),U()
                x.XQueryTree(display,root,C.byref(a),C.byref(b),C.byref(children),C.byref(count))
                for i in range(count.value):
                    title = C.c_char_p()
                    if x.XFetchName(display,children[i],C.byref(title)) and title.value:
                        name = title.value.decode(errors='replace')
                        x.XFree(C.cast(title,D))
                        if 'raytrac' in name.lower().replace(' ',''):
                            window = children[i]
                if children:
                    x.XFree(C.cast(children,D))
                if time.monotonic()>deadline:
                    raise TimeoutError('Workshop window')
                time.sleep(.05)

            def key(name):
                x.XSetInputFocus(display,window,1,0)
                code = x.XKeysymToKeycode(display,x.XStringToKeysym(name.encode()))
                for pressed in (1,0):
                    t.XTestFakeKeyEvent(display,code,pressed,0)
                    x.XSync(display,0)
                    time.sleep(.1)

            def capture(name):
                subprocess.run(['import','-window',hex(window),str(out/(name+'.png'))],check=True,timeout=15)

            def click(px,py):
                rx,ry,child = I(),I(),W()
                x.XTranslateCoordinates(display,window,root,0,0,C.byref(rx),C.byref(ry),C.byref(child))
                x.XSetInputFocus(display,window,1,0)
                t.XTestFakeMotionEvent(display,-1,rx.value+px,ry.value+py,0)
                x.XSync(display,0)
                for pressed in (1,0):
                    t.XTestFakeButtonEvent(display,1,pressed,0)
                    x.XSync(display,0)
                    time.sleep(.1)

            # The OS window exists before pipelines and the swapchain finish.
            # Wait for the host's actual rendering-ready receipt before input.
            deadline = time.monotonic()+120
            while 'windowed_render_ready:' not in log.read_text():
                if process.poll() is not None:
                    raise RuntimeError(log.read_text())
                if time.monotonic()>deadline:
                    raise TimeoutError('Workshop rendering readiness')
                time.sleep(.1)
            time.sleep(3)
            capture('initial')
            edited_roughness = None
            if args.gallery_exercise:
                click(280,677)
                time.sleep(3)
                capture('gallery')
                click(310,489)
            if args.optics_exercise:
                click(140,189)
                time.sleep(3)
                click(70,549)
                edited=json.loads((out/'edited-recipe.json').read_text())
                assert 1.8<edited['surface']['optics']['ior']<2.4,edited
                capture('ior-edited')
            if args.catalog_exercise:
                click(110,616)
                key('Home')
                families={e['family'] for e in json.loads(Path('assets/materials/catalog.json').read_text())['entries']}
                for _ in range(len(families)):
                    key('Down')
                key('Return')
                click(110,646)
                key('Home')
                key('Down')
                key('Return')
                click(110,677)
                key('Home')
                key('Down')
                key('Down')
                key('Return')
                time.sleep(3)
                capture('cloth-swatch')
                click(140,129)
                time.sleep(3)
                click(70,549)
                exported=json.loads((out/'edited-recipe.json').read_text())
                assert exported['id']=='woven-cloth',exported['id']
                edited_roughness=exported['surface']['roughness']
                click(310,489)
                capture('cloth-edited')
            if args.exercise:
                # Fixed coordinates were inspected in the native 1000x760 UI.
                click(150,428)  # Exposure changes only the display resolve.
                time.sleep(1)
                capture('exposure')
                click(110,279)
                click(65,329)
                time.sleep(3)
                capture('scene-optics')
                click(140,129)  # Material edit starts a new history epoch.
                time.sleep(3)
                click(70,549)   # Save recipe.
                click(310,489)  # Save view.
                capture('edited')
                good = (out/'edited-recipe.json').read_bytes()
                edited_roughness = json.loads(good)['surface']['roughness']
                assert .6 < edited_roughness < .9, edited_roughness
                assert (out/'view.json').is_file(), 'native Save view did not publish'
                bad = '{"format":"cubey.material.recipe","unexpected":true}\n'
                (out/'rejected-input.json').write_text(bad)
                (out/'edited-recipe.json').write_text(bad)
                click(175,549)  # Rejected import must retain the live snapshot.
                time.sleep(.5)
                capture('rejected')
                click(70,549)
                assert (out/'edited-recipe.json').read_bytes() == good, 'failed import replaced state'
                click(175,549)  # Now reload the valid exported recipe.
                time.sleep(3)
                capture('reloaded')
                click(80,489)   # Reset to canonical recipe.
                time.sleep(3)
                capture('reset')
                click(70,549)
                reset_roughness = json.loads((out/'edited-recipe.json').read_text())['surface']['roughness']
                assert abs(reset_roughness-.32) < 1e-6, reset_roughness
                x.XResizeWindow(display,window,1200,820)
                x.XSync(display,0)
                time.sleep(3)
                capture('resized')
            # Native keyboard navigation can consume Escape while editing.
            # Request the normal window close protocol, preserving teardown.
            class ClientData(C.Union):
                _fields_=[('l',C.c_long*5),('b',C.c_char*20)]
            class ClientMessage(C.Structure):
                _fields_=[('type',I),('serial',W),('send_event',I),('display',D),
                          ('window',W),('message_type',W),('format',I),('data',ClientData)]
            class Event(C.Union):
                _fields_=[('client',ClientMessage),('pad',C.c_long*24)]
            event=Event()
            event.client.type=33
            event.client.display=display
            event.client.window=window
            event.client.message_type=x.XInternAtom(display,b'WM_PROTOCOLS',0)
            event.client.format=32
            event.client.data.l[0]=x.XInternAtom(display,b'WM_DELETE_WINDOW',0)
            x.XSendEvent(display,window,0,0,C.byref(event))
            x.XSync(display,0)
            code = process.wait(timeout=20)
            stream.flush()
            text = log.read_text()
            assert code == 0 and not any(token in text.lower() for token in
                ['vuid-','hazard detected','vulkan validation error']),text
            metrics = json.loads((out/'metrics.json').read_text())
            assert metrics['history_samples'] == 32,metrics
            if args.exercise:
                assert metrics['history_epochs'] == 6,metrics['history_epochs']
                assert metrics['as_builds'] == 10 and metrics['target_generations'] == 2,metrics
                assert metrics['config']['reflection-source'] == 'scene',metrics['config']
                assert metrics['extent'] == [1200,820],metrics['extent']
                journal = metrics['sampling_frames']
                assert len({f['exposure'] for f in journal if f['epoch']==1}) >= 2, 'exposure reset history'
                assert {f['material_revision'] for f in journal} == {1,2,3,4}
                selected = next(m for m in metrics['workshop_materials'] if m['catalog_id']==args.material)
                assert selected['source_fingerprint'] == selected['resolved_fingerprint'], 'reset lost recipe identity'
            elif args.catalog_exercise:
                assert metrics['config']['material']=='woven-cloth' and metrics['config']['specimen']=='swatch',metrics['config']
                assert metrics['as_builds']==10 and metrics['target_generations']==1
                assert metrics['history_epochs']==4,metrics['history_epochs']
                assert len({f['resident_requested_bytes'] for f in metrics['sampling_frames']})==1
            elif args.gallery_exercise:
                assert metrics['config']['material']=='gallery' and metrics['history_epochs']==2
                assert abs(metrics['config']['distance']-.855)<1e-6
                assert metrics['as_builds']==10 and metrics['target_generations']==1
                saved=json.loads((out/'view.json').read_text())
                assert saved['recipe-input']=='' and saved['recipe-output']==''
            elif args.optics_exercise:
                assert metrics['history_epochs']==2 and metrics['as_builds']==10
                selected=next(m for m in metrics['workshop_materials'] if m['catalog_id']==args.material)
                assert selected['resolved_recipe']['surface']['optics']['ior']>1.8
            else:
                assert metrics['history_epochs'] == 1,metrics
            (out/'receipt.json').write_text(json.dumps(dict(
                command=command,exit=code,validation_clean=True,preview=not (args.exercise or args.catalog_exercise or args.gallery_exercise or args.optics_exercise),catalog=args.catalog_exercise,gallery=args.gallery_exercise,optics=args.optics_exercise,
                edited_roughness=edited_roughness,
                samples=metrics['history_samples'],epochs=metrics['history_epochs'],
                as_builds=metrics['as_builds']),indent=2)+'\n')
            print('Native workshop '+('edit/reload/resize' if args.exercise else 'catalog selection' if args.catalog_exercise else 'gallery' if args.gallery_exercise else 'optics edit' if args.optics_exercise else 'preview')+' passed:',out)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=15)
            x.XCloseDisplay(display)


if __name__ == '__main__':
    main()
