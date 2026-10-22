"""Opt-in authoring helpers; emit only existing bounded version-2 geometry."""
from author_sculptures import Sculpt
from sdf_author import round_cone, union
from sdf_probe import distance


def samples(points, radii, count=4, smooth_radius=True):
    """Catmull-Rom centers and monotone Hermite widths, including endpoints.

    Tangents use the harmonic mean of neighboring secants, zero at a width
    extremum. Widths remain inside each span's endpoint range. This is an
    authoring approximation; exported solids are ordinary rounded segments.
    """
    assert len(points) == len(radii) >= 2 and count >= 1
    delta = [b-a for a,b in zip(radii,radii[1:])]
    slopes = [delta[0], *[2*a*b/(a+b) if a*b>0 else 0
                        for a,b in zip(delta,delta[1:])], delta[-1]]
    result=[]
    for i in range(len(points)-1):
        p0,p1,p2,p3=[points[max(0,min(len(points)-1,j))] for j in (i-1,i,i+1,i+2)]
        for j in range(count):
            t=j/count
            p=[.5*(2*b+(-a+c)*t+(2*a-5*b+4*c-d)*t*t+(-a+3*b-3*c+d)*t**3)
               for a,b,c,d in zip(p0,p1,p2,p3)]
            if smooth_radius:
                r=((2*t**3-3*t*t+1)*radii[i]+(t**3-2*t*t+t)*slopes[i]
                   +(-2*t**3+3*t*t)*radii[i+1]+(t**3-t*t)*slopes[i+1])
                assert min(radii[i:i+2])-1e-12 <= r <= max(radii[i:i+2])+1e-12
            else:
                r=radii[i]*(1-t)+radii[i+1]*t
            result.append((p,r))
    return [*result,(points[-1],radii[-1])]


class Refinement(Sculpt):
    def __init__(self):
        super().__init__(True)

    def segments(self,name,path,label,join=.001):
        nodes=[self.leaf(round_cone(f'{name}-{i}',a,b,ra,rb),
                         label if i==0 else label+' / continue',.42)
               for i,((a,ra),(b,rb)) in enumerate(zip(path,path[1:]))]
        return self.blend(name,nodes,join) if join else union(name,*nodes)

    def curve(self,name,points,radii,label,count=4,join=0):
        return self.segments(name,samples(points,radii,count),label,join)

    def groove(self,name,body,points,radii,label,axis=2,side=1,count=2,depth=.28):
        """Project an authored 3D path from a selected world-axis side.

        The other two coordinates define the path. Fail on a missed surface;
        callers inspect wall retention independently. No runtime brush exists.
        """
        path=[]
        for p,r in samples(points,radii,count):
            p=list(p)
            outside=.49
            for i in range(1,197):
                inside=.49-i*.005
                p[axis]=side*inside
                if distance(body,p)<=0:
                    for _ in range(20):
                        middle=(outside+inside)/2
                        p[axis]=side*middle
                        if distance(body,p)>0:outside=middle
                        else:inside=middle
                    p[axis]=side*((outside+inside)/2-r*depth)
                    path.append((p.copy(),r))
                    break
                outside=inside
            else:
                raise ValueError(f'{name}: missed surface at {p}, axis={axis}, side={side}')
        return self.segments(name,path,label,0)

    def cuts(self,name,body,tools,blend=.003):
        if not tools:return body
        tool=tools[0] if len(tools)==1 else union(name+'-tools',*tools)
        return self.cut(name,body,tool,blend)


def leaf_names(node):
    return ([node['name']] if 'children' not in node else
            [name for child in node['children'] for name in leaf_names(child)])


def verify(sculpt,tree):
    names=leaf_names(tree)
    assert len(names)==len(set(names))==len(sculpt.history)<=128
    assert set(names)=={s['node'] for s in sculpt.history}
    return len(names)
