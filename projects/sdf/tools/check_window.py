#!/usr/bin/env python3
"""Exercise native input and resize in a private Xvfb display."""
import ctypes as C
from ctypes.util import find_library
from pathlib import Path
import os,subprocess,time,json,argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--binary",default="build/dev/projects/sdf/sdf")
parser.add_argument("--output",type=Path,default=Path("outputs/sdf/qualification"))
parser.add_argument("--config",type=Path,help="scene preset; interactions use static motion")
parser.add_argument("--height",type=int,default=600)
parser.add_argument("--carving-controls",action="store_true",help="exercise carving scrub and explicit regeneration at 850px height")
parser.add_argument("--expected-scene",choices=("negative-sticks","negative-sphere"),default="negative-sticks")
args=parser.parse_args()
if args.carving_controls and (not args.config or args.height != 850):
    raise SystemExit("Carving controls require --config and --height 850")
if os.environ.get("CUBEY_SDF_PRIVATE_XVFB")!="1":
    raise SystemExit("Use qualify.py --windowed to create an isolated display")
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
out=args.output.resolve();d=x.XOpenDisplay(None)
(out/"windowed-interaction.json").unlink(missing_ok=True)
assert d,"Xvfb display unavailable"
root=x.XDefaultRootWindow(d)
log=out/"logs/windowed-interaction.log"
with log.open("w") as stream:
    command=["stdbuf","-oL",args.binary]
    if args.config:command += ["--config",str(args.config),"--motion","static","--supersample","1"]
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
        if args.carving_controls:
            # Coordinates are from the inspected native panel in this isolated
            # 900x850 window, never from the user's desktop. Receipts below check
            # actual field prefixes and generator identity, not just key delivery.
            def click(cx,cy):
                t.XTestFakeMotionEvent(d,-1,cx+20,cy+20,0)
                t.XTestFakeButtonEvent(d,1,1,0);x.XSync(d,0);time.sleep(.07)
                t.XTestFakeButtonEvent(d,1,0,0);x.XSync(d,0);time.sleep(.25)
            click(190,176);capture("window-motion-menu.png")
            click(110,286);capture("window-carve-base.png")
            click(300,226);capture("window-carve-end.png")
            click(190,226);capture("window-carve-middle.png")
            click(76,226);capture("window-carve-back.png")
            before_seed=sum(line.startswith("sdf: edits=") for line in log.read_text().splitlines())
            click(130,594)
            modifier=x.XKeysymToKeycode(d,x.XStringToKeysym(b"Control_L"))
            t.XTestFakeKeyEvent(d,modifier,1,0);key("a");t.XTestFakeKeyEvent(d,modifier,0,0)
            key("4");key("3");key("Return");time.sleep(.2)
            capture("window-seed-staged.png")
            staged_log=log.read_text()
            assert sum(line.startswith("sdf: edits=") for line in staged_log.splitlines())==before_seed
            assert "sdf: regenerated" not in staged_log,"Seed edit regenerated without the button"
            click(140,655);capture("window-regenerated-base.png")
            click(300,226);capture("window-regenerated-end.png")
            click(750,780)
        key("Escape")
        status=proc.wait(timeout=15)
        assert status==0,f"native app exit {status}"
    finally:
        if proc.poll() is None:proc.terminate();proc.wait(timeout=10)
text=log.read_text()
assert "validation error" not in text.lower(),text
build_lines=[line for line in text.splitlines() if line.startswith("sdf: edits=")]
builds=len(build_lines)
if args.carving_controls:
    import re
    prefixes=[int(re.search(r"edits=(\d+)",line)[1]) for line in build_lines]
    assert len(prefixes)==6 and prefixes[:3]==[73,1,73] and 36<=prefixes[3]<=42 and prefixes[4:]==[1,73],prefixes
    assert f"regenerated scene={args.expected_scene}" in text,text
    old_hash=re.search(r"sdf: recipe hash=(\w+)",text)[1]
    new_hash=re.search(rf"regenerated scene={args.expected_scene} hash=(\w+)",text)[1]
    assert old_hash!=new_hash,"Seed editing/regeneration did not change the full recipe"
else:
    assert builds==1,f"camera/resize/input rebuilt the geometry {builds} times"
receipt={"passed":True,"display":"isolated Xvfb","command":command,"field_builds":builds,"actions":["resize twice","pointer orbit","scroll zoom","Space","R","Escape"],"log":str(log.relative_to(out))}
if args.carving_controls:
    receipt.update(prefixes=prefixes,initial_hash=old_hash,regenerated_hash=new_hash)
    receipt["actions"] += ["select Carve", "scrub to end", "scrub middle/back", "stage seed without regeneration", f"explicitly regenerate {args.expected_scene}", "scrub regenerated recipe"]
(out/"windowed-interaction.json").write_text(json.dumps(receipt,indent=2)+"\n")
print(json.dumps(receipt))
