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
    args = p.parse_args()
    if os.environ.get('CUBEY_RT_PRIVATE_XVFB') != '1':
        raise RuntimeError('Use an owned xvfb-run with CUBEY_RT_PRIVATE_XVFB=1')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    x, t = C.CDLL('libX11.so.6'), C.CDLL('libXtst.so.6')
    D, W, U, I = C.c_void_p, C.c_ulong, C.c_uint, C.c_int
    bindings = [
        (x,'XOpenDisplay',[C.c_char_p],D),(x,'XDefaultRootWindow',[D],W),
        (x,'XQueryTree',[D,W,C.POINTER(W),C.POINTER(W),C.POINTER(C.POINTER(W)),C.POINTER(U)],I),
        (x,'XFetchName',[D,W,C.POINTER(C.c_char_p)],I),(x,'XFree',[D],I),
        (x,'XSetInputFocus',[D,W,I,W],I),(x,'XSync',[D,I],I),
        (x,'XStringToKeysym',[C.c_char_p],W),(x,'XKeysymToKeycode',[D,W],C.c_ubyte),
        (t,'XTestFakeKeyEvent',[D,U,I,W],I),
        (t,'XTestFakeButtonEvent',[D,U,I,W],I),
        (t,'XTestFakeMotionEvent',[D,I,I,I,W],I),
        (x,'XResizeWindow',[D,W,U,U],I),(x,'XCloseDisplay',[D],I),
        (x,'XTranslateCoordinates',[D,W,W,I,I,C.POINTER(I),C.POINTER(I),C.POINTER(W)],I),
    ]
    for lib,name,parameters,result in bindings:
        f = getattr(lib,name)
        f.argtypes,f.restype = parameters,result
    display = x.XOpenDisplay(None)
    if not display:
        raise RuntimeError('Owned display unavailable')
    root,window = x.XDefaultRootWindow(display),None
    env = dict(os.environ,XDG_SESSION_TYPE='x11',VK_LAYER_VALIDATE_SYNC='1')
    env.pop('WAYLAND_DISPLAY',None)
    command = [str(args.binary.resolve()),'--scene','materials','--samples','32',
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

            time.sleep(3)
            capture('initial')
            edited_roughness = None
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
            key('Escape')
            code = process.wait(timeout=20)
            stream.flush()
            text = log.read_text()
            assert code == 0 and not any(token in text.lower() for token in
                ['vuid-','hazard detected','vulkan validation error']),text
            metrics = json.loads((out/'metrics.json').read_text())
            assert metrics['history_samples'] == 32,metrics
            if args.exercise:
                assert metrics['history_epochs'] == 6,metrics['history_epochs']
                assert metrics['as_builds'] == 2 and metrics['target_generations'] == 2,metrics
                assert metrics['config']['reflection-source'] == 'scene',metrics['config']
                assert metrics['extent'] == [1200,820],metrics['extent']
                journal = metrics['sampling_frames']
                assert len({f['exposure'] for f in journal if f['epoch']==1}) >= 2, 'exposure reset history'
                assert {f['material_revision'] for f in journal} == {1,2,3,4}
                bronze = next(m for m in metrics['workshop_materials'] if m['catalog_id']=='bronze')
                assert bronze['source_fingerprint'] == bronze['resolved_fingerprint'], 'reset lost recipe identity'
            else:
                assert metrics['history_epochs'] == 1,metrics
            (out/'receipt.json').write_text(json.dumps(dict(
                command=command,exit=code,validation_clean=True,preview=not args.exercise,
                edited_roughness=edited_roughness,
                samples=metrics['history_samples'],epochs=metrics['history_epochs'],
                as_builds=metrics['as_builds']),indent=2)+'\n')
            print('Native workshop '+('edit/reload/resize' if args.exercise else 'preview')+' passed:',out)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=15)
            x.XCloseDisplay(display)


if __name__ == '__main__':
    main()
