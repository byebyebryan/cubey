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
parser.add_argument("--history-controls",action="store_true")
args=parser.parse_args()
if not args.config or args.height != 1000:
    raise SystemExit("Authored material controls require --config and --height 1000")
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
            midpoint_y=306+13*(lines-1)
            control=x.XKeysymToKeycode(d,x.XStringToKeysym(b'Control_L'))
            t.XTestFakeKeyEvent(d,control,1,0);x.XSync(d,0)
            click(190,midpoint_y);key('a')
            t.XTestFakeKeyEvent(d,control,0,0);x.XSync(d,0)
            for digit in str(total//2):key(digit)
            key('Return')
            assert wait_step(lambda n:n==total//2)==total//2
            capture("window-step-middle.png")
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
            click(110,600)
            capture("window-material-open.png")
            # Coordinates come from inspection of this owned 900x1000 panel.
            click(300,654)
            capture("window-material-edited.png")
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
        "actions":["resize twice","orbit","zoom","pause/reset","next edits 1 through 5",
                   "previous to 4 and next to 5","scrub to final and enter numeric midpoint","stable paused field",
                   "restart empty","resume and pause playback","Escape"]}
else:
    assert builds==1,builds
    updates=re.findall(r"authored update geometry=(\w+) appearance=(\w+) field_rebuilt=(\d)",text)
    assert updates and all(row[2]=="0" for row in updates),text
    initial=re.search(r"sdf: authored geometry=(\w+) appearance=(\w+)",text)
    assert all(row[0]==initial[1] for row in updates)
    assert any(row[1]!=initial[2] for row in updates)
    receipt={"passed":True,"command":command,"field_builds":builds,"updates":updates,
    "actions":["resize twice","orbit","zoom","pause/reset","change ivory roughness from 0.52 without field rebuild","Escape"]}
import hashlib
receipt["binary_sha256"]=hashlib.sha256(Path(args.binary).read_bytes()).hexdigest()
receipt["shader_sha256"]={p.name:hashlib.sha256(p.read_bytes()).hexdigest()
    for p in sorted((Path(args.binary).parent/"shaders").glob("*.spv"))}
receipt["source_commit"]=subprocess.check_output(["git","rev-parse","HEAD"],text=True).strip()
(out/"windowed-interaction.json").write_text(json.dumps(receipt,indent=2)+"\n")
print(json.dumps(receipt))
