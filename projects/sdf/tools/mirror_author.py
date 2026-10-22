"""Opt-in X reflection and atomic strokes; emit ordinary version-2 solids."""
from copy import deepcopy
import math

from refinement_author import leaf_names
from sdf_author import union


def reflect_x(node, rename=lambda name: name+'-mirrored'):
    """Reflect a subtree in its parent's X=0 plane, including local frames.

    S R S is a proper rotation. All supported primitive distance functions
    are invariant under the remaining local X reflection, so the reflected
    quaternion is (x,-y,-z,w). Reflect every local frame, not just leaf centers;
    this also handles nested rotations and positive uniform scales.
    """
    result=deepcopy(node)
    def visit(part):
        if part['type'] not in ('box','sphere','capsule','cylinder','torus',
                                'capped-cone','round-cone','group','union',
                                'subtract','smooth-union','smooth-subtract'):
            raise ValueError('Unsupported mirrored node: '+part['type'])
        part['name']=rename(part['name'])
        if 'position' in part:part['position'][0]=-part['position'][0]
        if 'rotation_xyzw' in part:
            x,y,z,w=part['rotation_xyzw']
            part['rotation_xyzw']=[x,-y,-z,w]
        for child in part.get('children',[]):visit(child)
    visit(result)
    return result


class MirrorAuthor:
    """Register a complete sculpting stroke and optionally its reflected copy.

    Build the stroke with ordinary primitive/curve helpers first, then pass its
    subtree to edit(). Use the returned subtree in the surrounding Boolean.
    Changing mirror_x affects future edits only; centerline=True emits once.
    No geometry is folded or compressed at runtime.
    """
    def __init__(self, mirror_x=True):
        self.mirror_x=mirror_x
        self.history=[]
        self.names=set()

    def edit(self,node,label,seconds=.8,*,mirror=None,centerline=False,rename=None):
        enabled=(self.mirror_x if mirror is None else mirror) and not centerline
        result=deepcopy(node)
        if enabled:
            reflected=reflect_x(node,rename) if rename else reflect_x(node)
            result=union(node['name']+'-mirror-pair',result,reflected)
        names=leaf_names(result)
        if len(names)!=len(set(names)) or self.names.intersection(names):
            raise ValueError('Stroke names must be unique across all operations')
        if not label or len(label)>149 or not math.isfinite(seconds) or not .2<=seconds<=5:
            raise ValueError('Invalid stroke label or duration')
        if len(self.names)+len(names)>1024:
            raise ValueError('Stroke exceeds authored primitive capacity')
        step=dict(nodes=names,label=label+(' [Mirror X]' if enabled else ''),seconds=seconds)
        if enabled:step['mirror_axis']='x'
        self.history.append(step);self.names.update(names)
        return result
