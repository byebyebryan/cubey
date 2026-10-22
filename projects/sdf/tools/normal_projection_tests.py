#!/usr/bin/env python3
"""Independent analytic depth checks for oblique projected sculpting strokes."""
import math
import unittest

from refinement_author import Refinement
from sdf_author import sphere, subtract
from sdf_probe import distance


class NormalProjection(unittest.TestCase):
    def test_oblique_front_and_rear_depth_and_wall(self):
        body=sphere('body',(0,0,0),.2)
        for axis in range(3):
            for side in (-1,1):
                for depth in (-.75,.75):
                    with self.subTest(axis=axis,side=side,depth=depth):
                        points=[[0.,0.,0.],[0.,0.,0.]]
                        points[0][(axis+1)%3]=.16
                        points[1][(axis+1)%3]=.16
                        points[1][(axis+2)%3]=.01
                        stroke=Refinement().groove('stroke',body,points,[.01,.01],
                            'Oblique stroke',axis=axis,side=side,count=1,
                            depth=depth,normal_offset=True)
                        node=stroke['children'][0]
                        self.assertAlmostEqual(distance(body,node['position']),-depth*.01,delta=.0002)
                        self.assertGreater(side*node['position'][axis],0)
                        if depth<0:
                            surface=points[0].copy();surface[axis]=side*math.sqrt(.2**2-.16**2)
                            result=subtract('carved',body,stroke)
                            self.assertAlmostEqual(distance(result,surface),.0025,delta=.00002)
                            # Four millimeters inside remains solid, even at
                            # this grazing projection angle.
                            retained=[v*.98 for v in surface]
                            self.assertLess(distance(result,retained),0)


if __name__=='__main__':unittest.main()
