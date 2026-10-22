#!/usr/bin/env python3
"""Analytic reflection and atomic authoring contracts, independent of native CSG."""
import copy
import random
import unittest

from mirror_author import MirrorAuthor, reflect_x
from refinement_author import Refinement, leaf_names
from sdf_author import (box,capsule,capped_cone,cylinder,rotation,round_cone,
                        smooth_subtract,sphere,torus,union)
from sdf_probe import distance


class MirrorAuthorTests(unittest.TestCase):
    def test_reflection_of_all_primitives_and_nested_frames(self):
        nodes=[box('box',(.09,.04,-.02),(.06,.02,.03),rotation_xyzw=rotation((1,2,3),37)),
               sphere('sphere',(.1,.05,.02),.04),
               capsule('capsule',(.03,-.04,.02),(.1,.1,-.05),.025),
               cylinder('cylinder',(.09,.02,.03),.03,.08,rotation_xyzw=rotation((2,-1,3),71)),
               torus('torus',(.1,.05,.02),.06,.012,rotation_xyzw=rotation((2,1,3),44)),
               capped_cone('cone',(.08,0,.04),.04,.015,.09,rotation_xyzw=rotation((3,2,1),112)),
               round_cone('taper',(.02,-.06,.01),(.11,.08,-.02),.04,.01)]
        nested=union('nested',*copy.deepcopy(nodes),position=[.02,.03,-.02],
                     rotation_xyzw=rotation((2,3,-1),48),scale=.83)
        carved=smooth_subtract('carved',nested,sphere('tool',(.1,0,.04),.06),.004)
        rng=random.Random(726)
        for node in [*nodes,carved]:
            original=copy.deepcopy(node);reflected=reflect_x(node)
            for _ in range(240):
                point=[rng.uniform(-.3,.3) for _ in range(3)]
                self.assertAlmostEqual(distance(reflected,point),
                                       distance(node,[-point[0],*point[1:]]),places=11)
            self.assertEqual(node,original)

    def test_stroke_pair_centerline_and_disable(self):
        author=MirrorAuthor()
        stroke=Refinement().curve('brow',[(.03,.1,.1),(.08,.12,.1),(.12,.1,.08)],
                                  [.01,.02,.01],'Brow',count=3)
        paired=author.edit(stroke,'Raise the brow')
        self.assertEqual(len(leaf_names(paired)),12)
        self.assertEqual(len(author.history),1)
        self.assertEqual(len(author.history[0]['nodes']),12)
        self.assertEqual(author.history[0]['mirror_axis'],'x')
        center=author.edit(sphere('nose',(0,0,.2),.03),'Nose',centerline=True)
        self.assertEqual(leaf_names(center),['nose'])
        author.mirror_x=False
        off=author.edit(sphere('chip',(.1,0,.2),.01),'Asymmetric chip')
        self.assertEqual(leaf_names(off),['chip'])
        self.assertNotIn('mirror_axis',author.history[-1])
        with self.assertRaises(ValueError):author.edit(stroke,'Duplicate')
        self.assertEqual(len(author.history),3)


if __name__=='__main__':unittest.main()
