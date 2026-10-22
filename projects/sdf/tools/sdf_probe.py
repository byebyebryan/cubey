"""Double-precision saved-scene probes for authoring and modeling-intent audits.

This is a small distance-only interpreter, not the native rendering oracle.
"""
import math


def length(v):
    return math.sqrt(sum(x*x for x in v))


def distance(node, point):
    if node is None:
        return 1000.
    p = [x-y for x,y in zip(point,node.get('position',[0,0,0]))]
    q = node.get('rotation_xyzw',[0,0,0,1])
    norm = length(q)
    x,y,z,w = [v/norm for v in q]
    # Independent quaternion matrix expansion, transposed for the inverse.
    p = [(1-2*y*y-2*z*z)*p[0]+(2*x*y+2*z*w)*p[1]+(2*x*z-2*y*w)*p[2],
         (2*x*y-2*z*w)*p[0]+(1-2*x*x-2*z*z)*p[1]+(2*y*z+2*x*w)*p[2],
         (2*x*z+2*y*w)*p[0]+(2*y*z-2*x*w)*p[1]+(1-2*x*x-2*y*y)*p[2]]
    scale = node.get('scale',1)
    p = [v/scale for v in p]
    kind = node['type']
    if 'children' in node:
        values = [distance(c,p) for c in node['children']]
        d = values[0]
        for b in values[1:]:
            if kind == 'union': d = min(d,b)
            elif kind == 'subtract': d = max(d,-b)
            elif kind == 'smooth-union':
                k=node['radius']; h=max(0,min(1,.5+.5*(b-d)/k))
                d=b*(1-h)+d*h-k*h*(1-h)
            elif kind == 'smooth-subtract':
                k=node['radius']; h=max(0,min(1,.5+.5*(d+b)/k))
                d=-b*(1-h)+d*h+k*h*(1-h)
            else: raise ValueError(kind)
    elif kind == 'sphere': d=length(p)-node['radius']
    elif kind == 'box':
        v=[abs(x)-e for x,e in zip(p,node['half_extent'])]
        d=length([max(0,x) for x in v])+min(max(v),0)-node.get('rounding',0)
    elif kind == 'capsule':
        p[1]-=max(-node['half_length'],min(node['half_length'],p[1]))
        d=length(p)-node['radius']
    elif kind == 'cylinder':
        v=[math.hypot(p[0],p[2])-node['radius'],abs(p[1])-node['half_length']]
        d=length([max(0,x) for x in v])+min(max(v),0)
    elif kind == 'torus':
        d=math.hypot(math.hypot(p[0],p[2])-node['major_radius'],p[1])-node['minor_radius']
    elif kind in ('round-cone','capped-cone'):
        bottom,top,h=node['bottom_radius'],node['top_radius'],node['half_length']
        radius=math.hypot(p[0],p[2]); y=p[1]+h; height=2*h
        if kind=='round-cone':
            if abs(top-bottom)>=height:
                d=math.hypot(radius,y)-bottom if bottom>=top else math.hypot(radius,y-height)-top
            else:
                slope=(top-bottom)/height; a=math.sqrt(max(0,1-slope*slope))
                t=radius*slope+y*a
                d=(math.hypot(radius,y)-bottom if t<0 else
                   math.hypot(radius,y-height)-top if t>height*a else radius*a-y*slope-bottom)
        else:
            edge=[top-bottom,height]; v=[radius-bottom,y]
            t=max(0,min(1,sum(a*b for a,b in zip(v,edge))/sum(a*a for a in edge)))
            side=length([a-t*b for a,b in zip(v,edge)])
            caps=min(math.hypot(max(radius-bottom,0),y),math.hypot(max(radius-top,0),y-height))
            d=min(side,caps)*(-1 if 0<=y<=height and radius<=bottom+(top-bottom)*y/height else 1)
    else: raise ValueError(kind)
    return d*scale


def front_surface(tree, x, y):
    """Bracket the first +Z surface and bisect it; fail if the stroke misses."""
    outside=.49
    for index in range(1,100):
        inside=.49-index*.01
        if distance(tree,(x,y,inside))<=0:
            for _ in range(22):
                middle=(outside+inside)/2
                if distance(tree,(x,y,middle))>0: outside=middle
                else: inside=middle
            return (outside+inside)/2
        outside=inside
    raise ValueError(f'No front surface at {x}, {y}')
