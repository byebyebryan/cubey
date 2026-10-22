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
args=parser.parse_args()
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
assert d,"Xvfb display unavailable"
root=x.XDefaultRootWindow(d)
log=out/"logs/windowed-interaction.log"
with log.open("w") as stream:
    command=[args.binary]
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
        key("space");key("r");time.sleep(.3);capture("window-reset.png");key("Escape")
        status=proc.wait(timeout=15)
        assert status==0,f"native app exit {status}"
    finally:
        if proc.poll() is None:proc.terminate();proc.wait(timeout=10)
text=log.read_text()
assert "validation error" not in text.lower(),text
builds=sum(line.startswith("sdf: edits=") for line in text.splitlines())
assert builds==1,f"camera/resize/input rebuilt the geometry {builds} times"
receipt={"passed":True,"display":"isolated Xvfb","command":command,"field_builds":builds,"actions":["resize twice","pointer orbit","scroll zoom","Space","R","Escape"],"log":str(log.relative_to(out))}
(out/"windowed-interaction.json").write_text(json.dumps(receipt,indent=2)+"\n")
print(json.dumps(receipt))
