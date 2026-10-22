#!/usr/bin/env python3
"""Check authored robot input, resize and material editing in a private Xvfb display."""
import ctypes as C
from ctypes.util import find_library
from pathlib import Path
import os,subprocess,time,json,argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--binary",default="build/dev/projects/sdf/sdf")
parser.add_argument("--output",type=Path,default=Path("outputs/sdf/qualification"))
parser.add_argument("--config",type=Path,help="scene preset; interactions use static motion")
parser.add_argument("--height",type=int,default=1000)
args=parser.parse_args()
if not args.config or args.height != 1000:
    raise SystemExit("Authored material controls require --config and --height 1000")
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
out=args.output.resolve();(out/"logs").mkdir(parents=True,exist_ok=True);(out/"captures").mkdir(exist_ok=True);d=x.XOpenDisplay(None)
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
        def click(cx,cy):
            t.XTestFakeMotionEvent(d,-1,cx+20,cy+20,0)
            t.XTestFakeButtonEvent(d,1,1,0);x.XSync(d,0);time.sleep(.07)
            t.XTestFakeButtonEvent(d,1,0,0);x.XSync(d,0);time.sleep(.35)
        click(110,600)
        capture("window-material-open.png")
        # Coordinates come from inspection of this owned 900x1000 panel.
        # Open the ivory material and change its actual roughness slider.
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
assert builds==1,builds
updates=re.findall(r"authored update geometry=(\w+) appearance=(\w+) field_rebuilt=(\d)",text)
assert updates and all(row[2]=="0" for row in updates),text
initial=re.search(r"sdf: authored geometry=(\w+) appearance=(\w+)",text)
assert all(row[0]==initial[1] for row in updates)
assert any(row[1]!=initial[2] for row in updates)
receipt={"passed":True,"command":command,"field_builds":builds,"updates":updates,
"actions":["resize twice","orbit","zoom","pause/reset","change ivory roughness from 0.52 without field rebuild","Escape"]}
(out/"windowed-interaction.json").write_text(json.dumps(receipt,indent=2)+"\n")
print(json.dumps(receipt))
