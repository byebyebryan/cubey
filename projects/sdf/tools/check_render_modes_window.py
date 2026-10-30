#!/usr/bin/env python3
"""Actual mode/sample controls in an owned Xvfb native window, never the desktop."""
import argparse
import ctypes as C
from ctypes.util import find_library
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary',type=Path,default=Path('build/release/projects/sdf/sdf'))
p.add_argument('--output',type=Path,required=True)
p.add_argument('--config',type=Path,required=True)
p.add_argument('--inspect',action='store_true')
p.add_argument('--initial-mode',choices=['auto','cached','balanced'],default='cached')
p.add_argument('--workshop-check',action='store_true')
args=p.parse_args();args.binary=args.binary.resolve();out=args.output.resolve()
out.mkdir(parents=True,exist_ok=False)
if os.environ.get('CUBEY_SDF_PRIVATE_XVFB')!='1':raise SystemExit('Requires owned xvfb-run display')
D=C.c_void_p;W=C.c_ulong;U=C.c_uint;I=C.c_int
x=C.CDLL(find_library('X11'));t=C.CDLL(find_library('Xtst'))
def bind(lib,name,parameters,result=I):
    function=getattr(lib,name);function.argtypes=parameters;function.restype=result;return function
bind(x,'XOpenDisplay',[C.c_char_p],D);bind(x,'XDefaultRootWindow',[D],W)
bind(x,'XQueryTree',[D,W,C.POINTER(W),C.POINTER(W),C.POINTER(C.POINTER(W)),C.POINTER(U)])
bind(x,'XFetchName',[D,W,C.POINTER(C.c_char_p)]);bind(x,'XFree',[D])
bind(x,'XMoveWindow',[D,W,I,I]);bind(x,'XResizeWindow',[D,W,U,U]);bind(x,'XSetInputFocus',[D,W,I,W]);bind(x,'XSync',[D,I])
bind(x,'XStringToKeysym',[C.c_char_p],W);bind(x,'XKeysymToKeycode',[D,W],C.c_ubyte)
bind(t,'XTestFakeKeyEvent',[D,U,I,W]);bind(t,'XTestFakeButtonEvent',[D,U,I,W]);bind(t,'XTestFakeMotionEvent',[D,I,I,I,W])
display=x.XOpenDisplay(None)
if not display:raise RuntimeError('Owned Xvfb display unavailable')
root=x.XDefaultRootWindow(display);log=out/'native.log'
config=json.loads(args.config.read_text())
if 'authored-scene' in config:
    source=Path(config['authored-scene'])
    if not source.is_absolute():source=Path.cwd()/source
    scene=out/'scene.json';scene.write_text(source.read_text());config['authored-scene']=str(scene)
config.update({'render-mode':args.initial_mode,'motion':'static'})
profile=out/'config.json';profile.write_text(json.dumps(config,indent=2)+'\n')
command=['stdbuf','-oL',str(args.binary),'--config',str(profile),'--width','900','--height','1000','--require-validation']
def wait_for(predicate,description):
    deadline=time.monotonic()+90
    while not predicate():
        if process.poll() is not None:raise RuntimeError('Native window exited during '+description)
        if time.monotonic()>deadline:raise RuntimeError('Timeout: '+description)
        time.sleep(.05)
def capture(name):
    path=out/(name+'.png')
    subprocess.run(['ffmpeg','-nostdin','-v','error','-f','x11grab','-draw_mouse','0','-video_size','900x1000',
        '-i',os.environ['DISPLAY']+'+20,20','-frames:v','1','-y',str(path)],check=True)
    return path
def click(cx,cy):
    t.XTestFakeMotionEvent(display,-1,cx+20,cy+20,0);t.XTestFakeButtonEvent(display,1,1,0);x.XSync(display,0);time.sleep(.06)
    t.XTestFakeButtonEvent(display,1,0,0);x.XSync(display,0);time.sleep(.15)
def key(name):
    code=x.XKeysymToKeycode(display,x.XStringToKeysym(name.encode()))
    t.XTestFakeKeyEvent(display,code,1,0);t.XTestFakeKeyEvent(display,code,0,0);x.XSync(display,0);time.sleep(.15)
def modes():
    return [dict(requested=r,effective=e,ss=int(ss),generation=int(g),field_generation=int(f),field_bytes=int(b))
        for r,e,ss,g,f,b in re.findall(r'render-mode requested=(\S+) effective=(\S+).*? ss=(\d+) generation=(\d+) field_generation=(\d+) field_bytes=(\d+)',log.read_text())]
with log.open('w') as stream:
    process=subprocess.Popen(command,stdout=stream,stderr=subprocess.STDOUT)
    try:
        window=None
        def find_window():
            global window
            r=W();parent=W();children=C.POINTER(W)();count=U()
            x.XQueryTree(display,root,C.byref(r),C.byref(parent),C.byref(children),C.byref(count))
            for i in range(count.value):
                name=C.c_char_p()
                if x.XFetchName(display,children[i],C.byref(name)) and name.value:
                    title=name.value.decode(errors='replace');x.XFree(C.cast(name,D))
                    if 'cubey sdf' in title.lower():window=children[i]
            if children:x.XFree(C.cast(children,D))
            return window is not None
        wait_for(find_window,'window creation');wait_for(lambda:'windowed_render_ready:' in log.read_text(),'render readiness')
        x.XMoveWindow(display,window,20,20);x.XSetInputFocus(display,window,1,0);x.XSync(display,0);time.sleep(.4)
        capture('initial');click(180,106);capture('mode-menu')
        if args.inspect:
            click(410,90);click(180,136);capture('samples-menu');click(410,90);capture('menu-closed')
        elif args.workshop_check:
            click(410,90)
            def updates():return re.findall(r'authored update geometry=(\w+) appearance=(\w+) field_rebuilt=(\d) region_rebuilt=(\d)',log.read_text())
            count=len(updates());click(180,226);click(180,256)
            wait_for(lambda:len(updates())>count,'workshop recipe selection')
            if updates()[-1][2:]!=('0','0'):raise RuntimeError('Recipe edit rebuilt geometry')
            capture('recipe-selected')
            count=len(updates());click(180,166);click(180,216)
            wait_for(lambda:len(updates())>count,'workshop specimen selection')
            expected=('1','0') if modes()[-1]['effective']=='cached' else ('1','1')
            if updates()[-1][2:]!=expected:raise RuntimeError('Specimen did not publish matching field/regions')
            capture('specimen-selected')
        else:
            click(410,90)
            baseline=modes()[-1]
            for mode,row_y in [('cached',156),('cached-refined',176),('balanced',196),
                               ('analytic',216),('hybrid',236),('cached',156),('balanced',196)]:
                before=modes()[-1];count=len(modes())
                if before['requested']==mode and before['effective']==mode:
                    capture('mode-'+mode)
                    continue
                click(180,106);click(180,row_y)
                wait_for(lambda:len(modes())>count and modes()[-1]['effective']==mode,'mode '+mode)
                current=modes()[-1]
                if current['ss']!=baseline['ss']:raise RuntimeError('Mode changed supersampling')
                if mode=='analytic' and current['field_bytes']:raise RuntimeError('Source retained field')
                if before['effective']!='analytic' and mode!='analytic' and current['field_generation']!=before['field_generation']:
                    raise RuntimeError('Cached selection rebuilt field')
                capture('mode-'+mode)
            before=modes()[-1]
            for ss,row_y in [(1,166),(2,186)]:
                count=len(modes());click(180,136);click(180,row_y)
                wait_for(lambda:len(modes())>count and modes()[-1]['ss']==ss,'supersampling '+str(ss))
                if modes()[-1]['field_generation']!=before['field_generation']:raise RuntimeError('Sample change rebuilt field')
                capture('samples-'+str(ss))
            generation=modes()[-1]['field_generation'];count=len(modes())
            x.XResizeWindow(display,window,1100,700);x.XSync(display,0)
            wait_for(lambda:len(modes())>count,'first resize')
            count=len(modes());x.XResizeWindow(display,window,900,1000);x.XSync(display,0)
            wait_for(lambda:len(modes())>count,'second resize')
            if modes()[-1]['field_generation']!=generation:raise RuntimeError('Resize rebuilt field')
            capture('resized-restored')
        key('Escape');process.wait(timeout=20)
        if process.returncode:raise RuntimeError('Native exit code '+str(process.returncode))
    finally:
        if process.poll() is None:process.terminate();process.wait(timeout=15)
text=log.read_text()
if re.search(r'Validation (?:Error|Warning)|VUID-',text):raise RuntimeError('Native validation reported an issue')
receipt=dict(command=command,inspect_only=args.inspect,mode_rows=modes(),
    passed=not args.inspect,
    binary_sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest(),scope='Owned virtual X11 window; not physical desktop acceptance')
(out/'window.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('Native panel inspection saved',out,flush=True)
