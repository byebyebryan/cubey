#!/usr/bin/env python3
"""Check authored scene input, history, resize and material editing in private Xvfb."""
import ctypes as C
from ctypes.util import find_library
from pathlib import Path
import os,subprocess,time,json,argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--binary",default="build/dev/projects/sdf/sdf")
parser.add_argument("--output",type=Path,default=Path("outputs/sdf/qualification"))
parser.add_argument("--config",type=Path,help="scene preset; interactions use static motion")
parser.add_argument("--height",type=int,default=1000)
parser.add_argument("--material-y-offset",type=int,default=0,
                    help="Inspected material-row offset for the fixed study-environment label")
parser.add_argument("--readonly-material",action="store_true",
                    help="Verify the bounded glass material controls remain disabled")
parser.add_argument("--history-controls",action="store_true")
parser.add_argument("--paired-step",type=int,action="append",default=[],
                    help="Also seek, undo and redo these atomic mirrored operations")
parser.add_argument("--material-scroll",type=int,default=0,
                    help="Wheel notches to expose an expanded material panel")
parser.add_argument("--procedural-controls",action="store_true",
                    help="Check pattern/bump toggles and grain editing after five material scroll notches")
parser.add_argument("--marble-controls",action="store_true",
                    help="Check marble detail presets and body tint after eight material scroll notches")
args=parser.parse_args()
if not args.config or args.height != 1000:
    raise SystemExit("Authored material controls require --config and --height 1000")
if args.procedural_controls and (args.history_controls or args.material_scroll!=5):
    raise SystemExit('Procedural controls require static mode and --material-scroll 5')
if args.marble_controls and (args.history_controls or args.procedural_controls or args.material_scroll!=8):
    raise SystemExit('Marble controls require static mode and --material-scroll 8')
if os.environ.get("CUBEY_SDF_PRIVATE_XVFB")!="1":
    raise SystemExit("Run in a private xvfb-run display with CUBEY_SDF_PRIVATE_XVFB=1")
D=C.c_void_p; W=C.c_ulong; U=C.c_uint; I=C.c_int
x=C.CDLL(find_library("X11")); t=C.CDLL(find_library("Xtst"))
def bind(lib,name,args,result=I):
    fn=getattr(lib,name);fn.argtypes=args;fn.restype=result;return fn
bind(x,"XOpenDisplay",[C.c_char_p],D);bind(x,"XDefaultRootWindow",[D],W)
bind(x,"XQueryTree",[D,W,C.POINTER(W),C.POINTER(W),C.POINTER(C.POINTER(W)),C.POINTER(U)])
bind(x,"XFetchName",[D,W,C.POINTER(C.c_char_p)]);bind(x,"XFree",[D])
bind(x,"XMoveWindow",[D,W,I,I]);bind(x,"XResizeWindow",[D,W,U,U]);bind(x,"XSetInputFocus",[D,W,I,W])
bind(x,"XSync",[D,I]);bind(x,"XStringToKeysym",[C.c_char_p],W);bind(x,"XKeysymToKeycode",[D,W],C.c_ubyte)
bind(t,"XTestFakeKeyEvent",[D,U,I,W]);bind(t,"XTestFakeButtonEvent",[D,U,I,W]);bind(t,"XTestFakeMotionEvent",[D,I,I,I,W])
out=args.output.resolve();(out/"logs").mkdir(parents=True,exist_ok=True);(out/"captures").mkdir(exist_ok=True);d=x.XOpenDisplay(None)
(out/"windowed-interaction.json").unlink(missing_ok=True)
assert d,"Xvfb display unavailable"
root=x.XDefaultRootWindow(d)
log=out/"logs/windowed-interaction.log"
with log.open("w") as stream:
    command=["stdbuf","-oL",args.binary]
    if args.config:command += ["--config",str(args.config),"--motion","construction" if args.history_controls else "static","--supersample","1"]
    command += ["--width","900","--height",str(args.height),"--require-validation"]
    proc=subprocess.Popen(command,stdout=stream,stderr=subprocess.STDOUT)
    try:
        window=None
        for attempt in range(100):
            r=W();parent=W();children=C.POINTER(W)();count=U()
            x.XQueryTree(d,root,C.byref(r),C.byref(parent),C.byref(children),C.byref(count))
            for i in range(count.value):
                name=C.c_char_p()
                if x.XFetchName(d,children[i],C.byref(name)) and name.value:
                    value=name.value.decode(errors="replace")
                    x.XFree(C.cast(name,D))
                    if "cubey sdf" in value.lower():window=children[i]
            if children:x.XFree(C.cast(children,D))
            if window:break
            if proc.poll() is not None:raise RuntimeError("native app exited before the window appeared")
            time.sleep(.1)
        assert window,"SDF window not found"
        x.XMoveWindow(d,window,20,20);x.XSetInputFocus(d,window,1,0);x.XSync(d,0);time.sleep(1)
        def capture(name):
            subprocess.run(["ffmpeg","-v","error","-f","x11grab","-video_size",f"900x{args.height}","-i",os.environ["DISPLAY"]+"+20,20","-frames:v","1","-y",str(out/"captures"/name)],check=True)
        capture("window-before.png")
        x.XResizeWindow(d,window,1100,700);x.XSync(d,0);time.sleep(.4)
        x.XResizeWindow(d,window,900,args.height);x.XSync(d,0);time.sleep(.4)
        t.XTestFakeMotionEvent(d,-1,730,320,0);t.XTestFakeButtonEvent(d,1,1,0);x.XSync(d,0)
        for i in range(10):
            t.XTestFakeMotionEvent(d,-1,730+i*5,320+i*4,0);x.XSync(d,0);time.sleep(.03)
        t.XTestFakeButtonEvent(d,1,0,0);x.XSync(d,0);time.sleep(.3)
        capture("window-orbit.png")
        t.XTestFakeButtonEvent(d,4,1,0);t.XTestFakeButtonEvent(d,4,0,0);x.XSync(d,0);time.sleep(.2)
        def key(name):
            code=x.XKeysymToKeycode(d,x.XStringToKeysym(name.encode()))
            t.XTestFakeKeyEvent(d,code,1,0);t.XTestFakeKeyEvent(d,code,0,0);x.XSync(d,0)
        key("space");key("r");time.sleep(.3);capture("window-reset.png")
        def click(cx,cy):
            t.XTestFakeMotionEvent(d,-1,cx+20,cy+20,0)
            t.XTestFakeButtonEvent(d,1,1,0);x.XSync(d,0);time.sleep(.07)
            t.XTestFakeButtonEvent(d,1,0,0);x.XSync(d,0);time.sleep(.35)
        if args.history_controls:
            import re
            observed=[]
            def step():
                return int(re.findall(r'history (?:initial )?step=(\d+)/',log.read_text())[-1])
            def wait_step(predicate):
                deadline=time.monotonic()+15
                while time.monotonic()<deadline:
                    current=step()
                    if predicate(current):return current
                    if proc.poll() is not None:raise RuntimeError('Native app exited while committing a history seek')
                    time.sleep(.05)
                raise AssertionError(f'History seek did not commit; last step {step()}')
            total=int(re.findall(r'history (?:initial )?step=\d+/(\d+)',log.read_text())[-1])
            assert step()==0,log.read_text()
            for index in range(1,6):
                click(233,277)
                observed.append(wait_step(lambda n:n==index))
                assert observed[-1]==index,observed
                if index in (4,5):capture(f"window-step-{index}.png")
            click(125,277);assert wait_step(lambda n:n==4)==4
            click(233,277);assert wait_step(lambda n:n==5)==5
            click(304,306);assert wait_step(lambda n:n==total)==total
            capture("window-step-final.png")
            before=log.read_text().count("sdf: authored instructions=")
            time.sleep(.4)
            assert before==log.read_text().count("sdf: authored instructions=")
            # In this owned 360px panel, the default 13px monospace font
            # wraps long captions at 47 columns and moves the slider down.
            # Use numeric entry rather than one-pixel precision for 1k steps.
            import textwrap
            preset=json.loads(args.config.read_text())
            authored=json.loads(Path(preset['authored-scene']).read_text())
            lines=max(1,len(textwrap.wrap(authored['history'][-1]['label'],width=47)))
            def rows(count):
                label=authored['history'][count-1]['label'] if count else 'Empty workspace'
                return max(1,len(textwrap.wrap(label,width=47)))
            def numeric_seek(count,prior):
                slider_y=306+13*(rows(prior)-1)
                control=x.XKeysymToKeycode(d,x.XStringToKeysym(b'Control_L'))
                t.XTestFakeKeyEvent(d,control,1,0);x.XSync(d,0)
                click(190,slider_y);key('a')
                t.XTestFakeKeyEvent(d,control,0,0);x.XSync(d,0)
                for digit in str(count):key(digit)
                key('Return')
                assert wait_step(lambda n:n==count)==count
            numeric_seek(total//2,total)
            capture("window-step-middle.png")
            prior=total//2
            for paired in args.paired_step:
                assert 1<=paired<=total and len(authored['history'][paired-1].get('nodes',[]))>1
                numeric_seek(paired,prior)
                capture(f'window-paired-{paired}.png')
                click(125,277+13*(rows(paired)-1))
                assert wait_step(lambda n:n==paired-1)==paired-1
                capture(f'window-paired-{paired}-undone.png')
                click(233,277+13*(rows(paired-1)-1))
                assert wait_step(lambda n:n==paired)==paired
                capture(f'window-paired-{paired}-redone.png')
                prior=paired
            click(160,146);assert wait_step(lambda n:n==0)==0
            # Short histories have a longer scaled empty opening hold. Wait
            # for an actual committed edit instead of assuming robot timing.
            preset=json.loads(args.config.read_text())
            authored=json.loads(Path(preset['authored-scene']).read_text())
            weight=.65+sum(item.get('seconds',.65) for item in authored['history'])
            opening=preset.get('construction-seconds',32)*.65/weight
            click(95,146)
            deadline=time.monotonic()+opening+3
            while step()==0 and time.monotonic()<deadline:time.sleep(.1)
            click(95,146)
            assert step()>0
            capture("window-playback-paused.png")
            click(160,146);assert wait_step(lambda n:n==0)==0
        else:
            click(110,600+args.material_y_offset)
            capture("window-material-open.png")
            if args.material_scroll:
                t.XTestFakeMotionEvent(d,-1,250+20,580+20,0)
                for _ in range(args.material_scroll):
                    t.XTestFakeButtonEvent(d,5,1,0);t.XTestFakeButtonEvent(d,5,0,0)
                    x.XSync(d,0);time.sleep(.12)
                time.sleep(.3);capture('window-material-scrolled.png')
            # Coordinates come from inspection of this owned 900x1000 panel.
            # The longer marble panel reaches its scroll limit before 8 full notches.
            click(300,155 if args.marble_controls else 654+args.material_y_offset-65*args.material_scroll)
            capture("window-material-edited.png")
            if args.marble_controls:
                capture('window-marble-balanced.png')
                # Inspected in this owned 900x1000 panel at its lower scroll limit.
                click(200,474);capture('window-marble-menu.png')
                click(200,504);capture('window-marble-original.png')
                for name,index in [('denser',2),('fine',3),('balanced-restored',1)]:
                    click(200,474);click(200,504+index*20)
                    capture('window-marble-'+name+'.png')
                click(260,504);capture('window-marble-custom.png')
                click(97,594);capture('window-marble-color-zero.png')
                click(280,594);capture('window-marble-color-edited.png')
            if args.procedural_controls:
                # Positions inspected in this owned, five-notch scrolled panel.
                click(102,410);capture('window-pattern-off.png')
                click(102,410)
                click(102,440);capture('window-bump-off.png')
                click(102,440)
                click(250,590);capture('window-procedural-edited.png')
        key("Escape")
        status=proc.wait(timeout=15)
        assert status==0,f"native app exit {status}"
    finally:
        if proc.poll() is None:proc.terminate();proc.wait(timeout=10)
text=log.read_text()
assert "validation error" not in text.lower(),text
import re
builds=sum(line.startswith("sdf: authored instructions=") for line in text.splitlines())
if args.history_controls:
    assert builds>10,builds
    visits=[int(s) for s in re.findall(r'history (?:initial )?step=(\d+)/',text)]
    receipt={"passed":True,"command":command,"field_builds":builds,"history_visits":visits,
        "paired_undo_redo_steps":args.paired_step,
        "actions":["resize twice","orbit","zoom","pause/reset","next edits 1 through 5",
                   "previous to 4 and next to 5","scrub to final and enter numeric midpoint","stable paused field",
                   "restart empty","resume and pause playback","Escape"]}
else:
    assert builds==1,builds
    updates=re.findall(r"authored update geometry=(\w+) appearance=(\w+) field_rebuilt=(\d)",text)
    assert (updates or args.readonly_material) and all(row[2]=="0" for row in updates),text
    initial=re.search(r"sdf: authored geometry=(\w+) appearance=(\w+)",text)
    assert all(row[0]==initial[1] for row in updates)
    if args.readonly_material:
        assert all(row[1]==initial[2] for row in updates)
        def model_pixels(name):
            return subprocess.check_output(['ffmpeg','-v','error','-i',str(out/'captures'/name),
                '-vf','crop=450:700:440:150','-f','rawvideo','-pix_fmt','rgb24','-'])
        assert model_pixels('window-material-open.png')==model_pixels('window-material-edited.png')
    else:
        assert any(row[1]!=initial[2] for row in updates)
    receipt={"passed":True,"command":command,"field_builds":builds,"updates":updates,
    "actions":["resize twice","orbit","zoom","pause/reset","change material roughness without field rebuild","Escape"]}
    if args.readonly_material:
        receipt['readonly_material']=True
        receipt['actions']=['resize twice','orbit','zoom','pause/reset',
            'attempt disabled glass material edit','unchanged model pixels','Escape']
    if args.marble_controls:
        choices=re.findall(r'marble detail preset=(\w+)',text)
        assert choices==['Original','Denser','Fine','Balanced'],choices
        def pixels(name):
            return subprocess.check_output(['ffmpeg','-v','error','-i',str(out/'captures'/name),
                '-vf','crop=450:700:440:150','-f','rawvideo','-pix_fmt','rgb24','-'])
        zero=pixels('window-marble-color-zero.png');colored=pixels('window-marble-color-edited.png')
        changed=sum(zero[i:i+3]!=colored[i:i+3] for i in range(0,len(zero),3))
        assert changed>100,changed
        receipt['marble_controls']=True
        receipt['marble_choices']=choices
        receipt['tint_render_changed_pixels']=changed
        receipt['actions'] += ['choose all four marble detail presets','manual vein edit becomes Custom',
                               'edit body color variation','compare rendered model outside the UI']
    if args.procedural_controls:
        def render_pixels(name):
            return subprocess.check_output(['ffmpeg','-v','error','-i',str(out/'captures'/name),
                '-vf','crop=450:700:440:150','-f','rawvideo','-pix_fmt','rgb24','-'])
        base=render_pixels('window-material-edited.png')
        changed={}
        for name in ('window-pattern-off.png','window-bump-off.png','window-procedural-edited.png'):
            frame=render_pixels(name)
            changed[name]=sum(base[i:i+3]!=frame[i:i+3] for i in range(0,len(base),3))
            assert changed[name]>100,(name,changed[name])
        assert len(updates)>=2
        receipt['procedural_controls']=True
        receipt['procedural_render_changed_pixels']=changed
        receipt['actions'] += ['toggle procedural pattern off/on','toggle bump off/on',
                               'edit grain scale without rebuilding','compare rendered model outside the UI']
import hashlib
receipt["binary_sha256"]=hashlib.sha256(Path(args.binary).read_bytes()).hexdigest()
receipt["shader_sha256"]={p.name:hashlib.sha256(p.read_bytes()).hexdigest()
    for p in sorted((Path(args.binary).parent/"shaders").glob("*.spv"))}
receipt["source_commit"]=subprocess.check_output(["git","rev-parse","HEAD"],text=True).strip()
(out/"windowed-interaction.json").write_text(json.dumps(receipt,indent=2)+"\n")
print(json.dumps(receipt))
