#!/usr/bin/env python3
"""Three small places for native SDF composition review; no renderer changes."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess

from sdf_author import (box, capsule, capped_cone, cylinder, material, rotation,
                        sphere, subtract, torus, union, write_scene)

ROOT = Path(__file__).resolve().parents[3]
SCENES = Path('projects/sdf/scenes/diorama-drafts-2026-11')
PRESETS = Path('projects/sdf/presets/diorama-drafts-2026-11')
PALETTE = [
    material('plaster', 'E6CD9C', .86), material('stone', 'A99C81', .92),
    material('light-stone', 'D4BE91', .88), material('roof', 'A94E38', .78),
    material('tile-edge', 'C77750', .75), material('wood', '614534', .83),
    material('dark', '283C40', .75), material('teal', '497E79', .69),
    material('brass', 'D0A456', .56), material('water', '518F9A', .35),
    material('leaf', '657951', .87), material('leaf-light', '8A9961', .88),
    material('earth', '6F6856', .96), material('linen', 'E0D6B7', .9),
    material('glow', 'F0BF70', .6, .12), material('blue', '4D667F', .85),
]
CONCEPTS = {
    'canal-mill': dict(title='The canal mill', scale=.67, yaw=.67, pitch=.43, distance=1.22,
        idea='A working mill and tiny delivery yard wrapped around a water channel.',
        focal='The large paddle wheel, warm tiled roof and arched footbridge.',
        details='Timber framing, roof courses, shutters, striped awning, hanging sign, sacks, barrels, cart and planting.'),
    'hill-observatory': dict(title='The hill observatory', scale=.68, yaw=.53, pitch=.43, distance=1.20,
        idea='A small scientific outpost built onto an uneven, terraced rock.',
        focal='The split copper-colored dome and telescope above the winding approach.',
        details='Dome ribs, balcony, arched entrance, clocks, tripod instrument, retaining masonry, switchback stairs and cypresses.'),
    'terraced-courtyard': dict(title='The terraced courtyard', scale=.64, yaw=.48, pitch=.49, distance=1.24,
        idea='A compact, inhabited courtyard with an arcade, tower and rooftop terraces.',
        focal='The central fountain and the route up the open stair toward the bell tower.',
        details='Arched recesses, columns, tiled roofs, balustrades, banners, paving, benches, jars and garden pots.'),
}


def leaf_names(node):
    return ([node['name']] if 'children' not in node else
            [n for child in node['children'] for n in leaf_names(child)])


def signed_leaves(node, negative=False):
    if 'children' not in node:
        return [(node['name'], negative)]
    return [item for i, child in enumerate(node['children'])
            for item in signed_leaves(child, negative ^
                                      (node['type'] in ('subtract', 'smooth-subtract') and i > 0))]


class Draft:
    def __init__(self):
        self.serial = 0
        self.parts = []
        self.history = []

    def name(self, label):
        self.serial += 1
        return f'{label}-{self.serial:04d}'

    def b(self, label, p, e, mat='stone', rounding=.003, **options):
        return box(self.name(label), p, e, min(rounding, min(e)*.45), mat, **options)

    def cy(self, label, p, r, h, mat='stone', **options):
        return cylinder(self.name(label), p, r, h, mat, **options)

    def sp(self, label, p, r, mat='leaf'):
        return sphere(self.name(label), p, r, mat)

    def beam(self, label, p, q, r=.005, mat='wood'):
        return capsule(self.name(label), p, q, r, mat)

    def cone(self, label, p, rb, rt, h, mat='stone'):
        return capped_cone(self.name(label), p, rb, rt, h, mat)

    def tor(self, label, p, r, t, mat='brass', **options):
        return torus(self.name(label), p, r, t, mat, **options)

    def u(self, label, *nodes, **options):
        if len(nodes) == 1:
            node = nodes[0]
            if options:
                # The lone transformed group here is an origin-centered CSG
                # operator. Preserve the local coordinates of its children.
                assert node.get('position', [0, 0, 0]) == [0, 0, 0]
                assert 'rotation_xyzw' not in node and 'scale' not in node
                node.update(options)
            return node
        return union(self.name(label), *nodes, **options)

    def cut(self, label, body, *tools):
        return subtract(self.name(label), body, self.u('tools', *tools))

    def place(self, label, nodes, p, angle=0):
        return self.u(label, *nodes, position=list(p), rotation_xyzw=rotation((0, 1, 0), angle))

    def phase(self, label, *nodes):
        self.parts.extend(nodes)
        # Native history actions have a single additive/subtractive polarity.
        # These phases contain complete local groups, so body-before-tool order
        # follows by emitting their positive leaves before their cutters.
        signed = [item for node in nodes for item in signed_leaves(node)]
        for negative in (False, True):
            names = [name for name, polarity in signed if polarity == negative]
            if names:
                self.history.append(dict(nodes=names, label=label+(' / carve' if negative else ''), seconds=1.3))


def arch(a, p, width, height, depth, mat='plaster'):
    """A vertical arch starting at p.y, facing positive Z; width is half-width."""
    x, y, z = p
    return a.u('arch', a.b('arch-rect', (x, y+(height-width)/2, z),
                          (width, (height-width)/2, depth), mat),
               a.cy('arch-round', (x, y+height-width, z), width, depth, mat,
                    rotation_xyzw=rotation((1, 0, 0), 90)))


def arch_frame(a, p, width, height, mat='light-stone'):
    x, y, z = p
    return a.cut('arch-surround', arch(a, (x, y-.007, z), width+.009, height+.016, .009, mat),
                 arch(a, (x, y, z+.004), width, height, .025, mat))


def window(a, p, width=.029, height=.041, angle=0, shutters=True):
    trim = a.cut('window-frame', a.b('frame', (0, 0, 0), (width+.006, height+.006, .008), 'wood'),
                 a.b('frame-inset', (0, 0, .005), (width, height, .018), 'wood'))
    pieces = [trim, a.b('window-dark', (0, 0, -.005), (width, height, .003), 'dark'),
              a.b('window-mullion', (0, 0, .009), (.003, height, .004), 'linen'),
              a.b('window-crossbar', (0, -.005, .009), (width, .003, .004), 'linen'),
              a.b('sill', (0, -height-.008, .009), (width+.012, .006, .015), 'light-stone')]
    if shutters:
        for side in (-1, 1):
            x = side*(width+.018)
            pieces.append(a.b('shutter', (x, 0, .003), (.010, height, .005), 'teal'))
            for y in (-height*.55, height*.55):
                pieces.append(a.b('shutter-strap', (x, y, .009), (.011, .0025, .003), 'dark'))
    return a.place('window', pieces, p, angle)


def roof(a, center, half_span, half_length, eave, rise, angle=0, mat='roof', courses=4):
    slope = math.atan2(rise, half_span)
    length = math.hypot(rise, half_span)
    edge_mat = 'brass' if mat == 'teal' else 'tile-edge'
    pieces = []
    for side in (-1, 1):
        theta = -side*math.degrees(slope)
        pieces.append(a.b('roof-plane', (side*half_span/2, eave+rise/2, 0),
                          (length/2+.01, .009, half_length), mat,
                          rotation_xyzw=rotation((0, 0, 1), theta)))
        for row in range(1, courses+1):
            t = row/(courses+1)
            x = side*half_span*t
            y = eave+rise*(1-t)+.011
            pieces.append(a.beam('tile-course', (x, y, -half_length), (x, y, half_length), .004, edge_mat))
        for i in range(10):
            z = -half_length+2*half_length*(i+.5)/10
            pieces.append(a.beam('tile-rib', (side*.008, eave+rise+.011, z),
                                 (side*half_span, eave+.011, z), .003, edge_mat))
        pieces.append(a.beam('eave', (side*half_span, eave-.005, -half_length),
                             (side*half_span, eave-.005, half_length), .007, 'wood'))
    pieces.append(a.beam('ridge-cap', (0, eave+rise+.017, -half_length-.006),
                         (0, eave+rise+.017, half_length+.006), .009, edge_mat))
    return a.place('tiled-roof', pieces, center, angle)


def gable(a, center, half_span, half_length, bottom, eave, rise, mat='plaster'):
    apex = eave+rise
    body = a.b('gable-mass', (0, (bottom+apex)/2, 0),
               (half_span, (apex-bottom)/2, half_length), mat)
    slope = math.atan2(rise, half_span)
    tools = []
    for side in (-1, 1):
        tools.append(a.b('roof-trim', (side*math.sin(slope)*.5, apex+math.cos(slope)*.5, 0),
                         (.7, .5, .6), mat,
                         rotation_xyzw=rotation((0, 0, 1), -side*math.degrees(slope))))
    return a.place('gable', [a.cut('shaped-gable', body, *tools)], center)


def barrel(a, p):
    x, y, z = p
    nodes = [a.cy('barrel', (x, y+.028, z), .023, .028, 'wood')]
    for dy in (.006, .028, .050):
        nodes.append(a.tor('barrel-hoop', (x, y+dy, z), .023, .003, 'dark'))
    nodes.append(a.b('barrel-lid', (x, y+.058, z), (.018, .002, .0025), 'brass'))
    return a.u('barrel-prop', *nodes)


def pot(a, p, radius=.021, tall=False):
    x, y, z = p
    nodes = [a.cone('pot', (x, y+.019, z), radius*.7, radius, .019, 'roof'),
             a.tor('pot-rim', (x, y+.038, z), radius, .004, 'tile-edge'),
             a.cy('pot-soil', (x, y+.037, z), radius*.82, .003, 'earth')]
    for i in range(3):
        ang = i*math.tau/3
        nodes.append(a.beam('plant', (x, y+.04, z),
                            (x+math.cos(ang)*.014, y+(.11 if tall else .070), z+math.sin(ang)*.014),
                            .012 if tall else .014, 'leaf' if i%2 else 'leaf-light'))
    return a.u('garden-pot', *nodes)


def cypress(a, p, height=.17):
    x, y, z = p
    return a.u('cypress', a.cy('trunk', (x, y+height*.30, z), .007, height*.30, 'wood'),
               a.cone('lower-crown', (x, y+height*.55, z), .027, .014, height*.30, 'leaf'),
               a.cone('upper-crown', (x, y+height*.80, z), .020, .002, height*.26, 'leaf-light'))


def lantern(a, p):
    x, y, z = p
    return a.u('wall-lantern', a.b('lantern-back', (x, y+.018, z), (.005, .026, .006), 'dark'),
               a.b('lantern-glow', (x, y, z+.016), (.010, .016, .010), 'glow'),
               a.b('lantern-cap', (x, y+.019, z+.016), (.015, .004, .014), 'dark'),
               a.b('lantern-foot', (x, y-.019, z+.016), (.013, .003, .013), 'dark'))


def canal_mill():
    a = Draft()
    a.phase('Set the island, canal and stone banks',
            a.cut('canal-in-island', a.b('island', (0, -.29, 0), (.36, .045, .30), 'earth', .012),
                  a.b('channel-cut', (0, -.245, .185), (.39, .040, .085), 'earth')),
            a.b('yard', (0, -.246, -.10), (.36, .010, .19), 'stone'),
            a.b('canal', (0, -.258, .185), (.36, .012, .082), 'water'),
            a.b('near-bank', (0, -.236, .282), (.36, .017, .018), 'light-stone'),
            a.b('far-bank', (0, -.230, .083), (.36, .021, .018), 'light-stone'))
    a.phase('Indicate the canal current',
            *[a.beam('water-ripple', (x, -.244, z), (x+.050, -.244, z), .0025, 'linen')
              for x, z in ((-.30, .145), (-.21, .205), (-.10, .17), (.015, .235), (.27, .14))])
    stones = []
    for i in range(11):
        for z in (.083, .282):
            stones.append(a.b('bank-cap', (-.33+i*.066, -.208 if z<.1 else -.216, z),
                              (.030, .005, .023), 'stone'))
    a.phase('Lay the bank coping and delivery path', *stones,
            *[a.b('path-slab', (.22, -.231, -.22+i*.053), (.055, .005, .023), 'light-stone') for i in range(6)])
    house = gable(a, (-.13, 0, -.095), .135, .125, -.235, .130, .117)
    door = arch(a, (-.16, -.235, .036), .034, .157, .042)
    house = a.cut('mill-openings', house, door,
                  a.b('upper-window-recess', (-.13, .058, .028), (.036, .049, .025), 'plaster'))
    a.phase('Raise the mill and carve its entrance', house,
            arch_frame(a, (-.16, -.235, .040), .034, .157),
            a.b('door-shadow', (-.16, -.163, -.001), (.032, .070, .004), 'dark'),
            *[a.b('door-plank', (-.184+i*.012, -.169, .006), (.005, .059, .006), 'wood') for i in range(5)])
    frame = []
    for x in (-.266, -.13, .006):
        frame.append(a.b('upright', (x, -.057, .034), (.006, .178, .007), 'wood'))
    for y in (-.218, -.032, .132):
        frame.append(a.b('front-floor-beam', (-.13, y, .035), (.14, .006, .008), 'wood'))
        frame.append(a.b('side-floor-beam', (.008, y, -.095), (.008, .006, .13), 'wood'))
    frame.extend([a.beam('front-brace', (-.26, -.02, .039), (-.19, .12, .039), .005),
                  a.beam('front-brace', (-.075, .12, .039), (0, -.02, .039), .005),
                  window(a, (-.13, .059, .040)), window(a, (.012, .050, -.12), angle=90),
                  window(a, (-.13, .177, .040), .021, .024, shutters=False)])
    a.phase('Add timber framing, shutters and attic window', *frame)
    a.phase('Tile the gabled roof and build the chimney',
            roof(a, (-.13, 0, -.095), .155, .15, .13, .136),
            a.b('chimney', (-.195, .249, -.169), (.023, .091, .023), 'roof'),
            a.b('chimney-cap', (-.195, .344, -.169), (.030, .009, .030), 'light-stone'),
            a.b('chimney-soot', (-.195, .354, -.169), (.016, .002, .016), 'dark'),
            *[a.b('chimney-course', (-.195, .20+i*.033, -.144), (.024, .003, .003), 'tile-edge') for i in range(4)])
    wheel = [a.tor('wheel-rim', (x, -.082, .047), .132, .009, 'wood',
                   rotation_xyzw=rotation((0, 0, 1), 90)) for x in (.060, .105)]
    wheel.append(a.cy('wheel-axle', (.055, -.082, .047), .020, .075, 'dark',
                      rotation_xyzw=rotation((0, 0, 1), 90)))
    for i in range(12):
        t = i*math.tau/12
        dy, dz = math.cos(t), math.sin(t)
        wheel.append(a.beam('wheel-spoke', (.083, -.082+dy*.015, .047+dz*.015),
                            (.083, -.082+dy*.122, .047+dz*.122), .006))
        wheel.append(a.b('paddle', (.083, -.082+dy*.132, .047+dz*.132),
                         (.031, .014, .022), 'wood', rotation_xyzw=rotation((1, 0, 0), math.degrees(t))))
    a.phase('Fit the waterwheel, axle and twelve paddles', *wheel)
    bridge = []
    for i in range(10):
        z = .073+i*.023
        y = -.207+.030*math.sin(math.pi*i/9)
        bridge.append(a.b('bridge-plank', (.232, y, z), (.066, .008, .010), 'wood'))
    for side in (-1, 1):
        x = .232+side*.061
        posts = []
        for i in range(4):
            z = .073+i*.069
            y = -.207+.030*math.sin(math.pi*i/3)
            bridge.append(a.beam('bridge-post', (x, y, z), (x, y+.061, z), .005, 'wood'))
            posts.append((x, y+.061, z))
        bridge.extend(a.beam('bridge-rail', p, q, .005, 'wood') for p, q in zip(posts, posts[1:]))
    a.phase('Bridge the canal with planks and curved rails', *bridge)
    awning = []
    for i in range(8):
        awning.append(a.b('awning-stripe', (-.245+i*.027, -.062, .081), (.014, .007, .055),
                          'linen' if i%2 else 'teal', rotation_xyzw=rotation((1, 0, 0), 9)))
    for x in (-.255, -.042):
        awning.append(a.beam('awning-post', (x, -.230, .13), (x, -.067, .13), .005))
    awning.append(a.beam('awning-edge', (-.26, -.07, .134), (-.033, -.07, .134), .006))
    awning.extend([a.beam('sign-bracket', (-.251, .05, .04), (-.251, .05, .11), .004, 'dark'),
                   a.b('sign-board', (-.251, .015, .112), (.033, .024, .005), 'wood'),
                   a.tor('sign-mill-mark', (-.251, .015, .119), .015, .003, 'brass',
                         rotation_xyzw=rotation((1, 0, 0), 90)), lantern(a, (-.08, -.126, .040))])
    a.phase('Shelter the delivery door and hang the mill sign', *awning)
    props = [barrel(a, (.155, -.232, -.15)), barrel(a, (.20, -.232, -.23)),
             pot(a, (-.31, -.232, -.17), tall=True), pot(a, (-.31, -.23, .015))]
    for x, z in ((-.07, .063), (-.03, .033), (-.10, .091)):
        props.append(a.cone('grain-sack', (x, -.20, z), .021, .016, .029, 'linen'))
        props.append(a.tor('sack-tie', (x, -.169, z), .012, .003, 'wood'))
    cart = [a.b('cart-bed', (-.21, -.176, .247), (.046, .009, .025), 'wood'),
            a.b('cart-side', (-.21, -.151, .223), (.046, .019, .004), 'wood'),
            a.b('cart-side', (-.21, -.151, .271), (.046, .019, .004), 'wood')]
    for z in (.211, .283):
        cart.append(a.tor('cart-wheel', (-.21, -.203, z), .027, .006, 'dark',
                          rotation_xyzw=rotation((1, 0, 0), 90)))
    for z in (.228, .266):
        cart.append(a.beam('cart-handle', (-.16, -.18, z), (-.10, -.15, z), .004))
    a.phase('Stock the yard with sacks, barrels, a cart and pots', *props,
            a.place('delivery-cart', cart, (.43, 0, -.282)),
            cypress(a, (.303, -.232, -.22), .21))
    return a


def hill_observatory():
    a = Draft()
    rocks = [a.cy('island', (0, -.306, 0), .34, .035, 'earth')]
    for i in range(18):
        t = i*math.tau/18
        r = .253 if i%3 else .232
        rocks.append(a.b('cliff-block', (math.cos(t)*r, -.242+(i%3)*.015, math.sin(t)*r),
                         (.063, .057+(i%2)*.018, .046), 'stone' if i%3 else 'earth', .012,
                         rotation_xyzw=rotation((0, 1, 0), -math.degrees(t))))
    a.phase('Build the rocky hill and two retained terraces', *rocks,
            a.cy('lower-terrace', (0, -.209, 0), .288, .018, 'light-stone'),
            a.cy('upper-terrace', (-.045, -.116, -.07), .215, .024, 'stone'),
            a.cy('upper-coping', (-.045, -.090, -.07), .220, .006, 'light-stone'))
    stairs = []
    for i in range(10):
        stairs.append(a.b('approach-step', (.17, -.207+i*.012, .255-i*.021),
                          (.045, .010, .014), 'light-stone'))
    stairs.extend([a.b('landing', (.17, -.092, .043), (.057, .009, .027), 'light-stone'),
                   a.beam('stair-rail', (.22, -.14, .259), (.22, -.024, .065), .005, 'dark'),
                   a.beam('rail-post', (.22, -.19, .25), (.22, -.13, .25), .005, 'dark'),
                   a.beam('rail-post', (.22, -.10, .075), (.22, -.025, .075), .005, 'dark')])
    a.phase('Climb from the path onto the upper terrace', *stairs,
            *[a.b('flagstone', (-.13+i*.043, -.188, .215+(i%2)*.013), (.019, .004, .020), 'stone') for i in range(7)])
    cx, cz = -.075, -.075
    body = a.cy('observatory-drum', (cx, .036, cz), .129, .126, 'plaster')
    interior = a.cy('drum-interior', (cx, .053, cz), .105, .128, 'plaster')
    entrance = arch(a, (cx, -.088, .049), .036, .150, .04)
    slots = [a.b('drum-window-cut', (cx+s*.113, .070, -.016), (.022, .039, .032), 'plaster') for s in (-1, 1)]
    a.phase('Hollow the observatory and open its arched entrance',
            a.cut('open-drum', body, interior, entrance, *slots),
            arch_frame(a, (cx, -.088, .060), .036, .150),
            a.tor('drum-foundation', (cx, -.080, cz), .130, .008, 'light-stone'),
            a.tor('drum-frieze', (cx, .151, cz), .131, .009, 'light-stone'))
    dome = a.sp('dome', (cx, .16, cz), .144, 'teal')
    dome = a.cut('hollow-dome', dome, a.sp('dome-inner', (cx, .16, cz), .130, 'teal'),
                 a.b('dome-bottom-trim', (cx, -.10, cz), (.22, .26, .22), 'teal'),
                 a.b('dome-slit', (cx, .267, cz+.085), (.031, .118, .12), 'teal'))
    ribs = [dome, a.tor('dome-base-ring', (cx, .165, cz), .144, .007, 'brass')]
    for side in (-1, 1):
        points = [(cx+side*.035, .179, cz+.140), (cx+side*.035, .245, cz+.109),
                  (cx+side*.035, .291, cz+.047), (cx+side*.035, .300, cz)]
        ribs.extend(a.beam('slit-rim', p, q, .005, 'brass') for p, q in zip(points, points[1:]))
    a.phase('Fit the split dome, brass ring and slit edges', *ribs)
    telescope = [a.cy('telescope-pedestal', (cx, .061, cz), .016, .090, 'dark'),
                 a.beam('telescope-barrel', (cx, .169, cz-.035), (cx, .263, cz+.13), .026, 'brass'),
                 a.beam('telescope-collar', (cx, .239, cz+.087), (cx, .259, cz+.122), .033, 'dark'),
                 a.sp('telescope-lens', (cx, .265, cz+.136), .022, 'blue')]
    a.phase('Aim the telescope through the dome opening', *telescope)
    balcony = [a.cy('balcony-platform', (cx, -.080, cz), .178, .009, 'light-stone')]
    rail_points = []
    for i in range(13):
        t = (-.60+i*1.19/12)*math.pi
        x, z = cx+math.sin(t)*.17, cz+math.cos(t)*.17
        if abs(x-cx)<.042 and z>cz:
            continue
        balcony.append(a.beam('baluster', (x, -.073, z), (x, -.018, z), .004, 'dark'))
        rail_points.append((x, -.018, z))
    for p, q in zip(rail_points, rail_points[1:]):
        if math.dist(p, q)<.070:
            balcony.append(a.beam('balcony-rail', p, q, .005, 'dark'))
    a.phase('Wrap the drum with an entrance balcony', *balcony)
    annex = gable(a, (.165, 0, -.144), .067, .078, -.09, .107, .064)
    a.phase('Attach the keeper annex and tiled roof', annex,
            roof(a, (.165, 0, -.144), .080, .091, .108, .071, mat='teal', courses=3),
            window(a, (.165, .042, -.060), .021, .029, shutters=False),
            a.b('annex-sill', (.165, -.083, -.144), (.075, .009, .085), 'light-stone'))
    instrument = [a.cy('instrument-pier', (-.15, -.143, .156), .027, .049, 'light-stone'),
                  a.tor('armillary', (-.15, -.037, .156), .047, .004, 'brass'),
                  a.tor('armillary-meridian', (-.15, -.037, .156), .047, .004, 'brass',
                        rotation_xyzw=rotation((1, 0, 0), 68)),
                  a.tor('armillary-meridian', (-.15, -.037, .156), .047, .004, 'brass',
                        rotation_xyzw=rotation((0, 0, 1), 72)),
                  a.sp('armillary-center', (-.15, -.037, .156), .014, 'blue')]
    for x, z in ((.10, .14), (.18, .14), (.14, .21)):
        instrument.append(a.beam('tripod-leg', (x, -.186, z), (.14, -.080, .17), .005, 'wood'))
    instrument.extend([a.beam('survey-tube', (.105, -.075, .14), (.174, -.045, .20), .012, 'brass'),
                       a.sp('survey-lens', (.177, -.044, .203), .013, 'dark')])
    a.phase('Set out the armillary and tripod instrument', *instrument)
    clock = [a.cy('clock-face', (.165, .126, -.061), .023, .005, 'linen',
                  rotation_xyzw=rotation((1, 0, 0), 90))]
    for i in range(8):
        t = i*math.tau/8
        clock.append(a.sp('clock-mark', (.165+math.cos(t)*.018, .126+math.sin(t)*.018, -.054), .0025, 'dark'))
    clock.extend([a.beam('clock-hand', (.165, .126, -.050), (.165, .142, -.050), .0025, 'dark'),
                  a.beam('clock-hand', (.165, .126, -.050), (.177, .120, -.050), .0025, 'dark')])
    a.phase('Add the annex clock and terrace planting', *clock,
            cypress(a, (-.27, -.187, -.08), .22), cypress(a, (.243, -.187, -.105), .17),
            pot(a, (-.22, -.187, .08)), pot(a, (.04, -.187, .22)),
            lantern(a, (cx-.064, -.017, .063)))
    return a


def terraced_courtyard():
    a = Draft()
    a.phase('Lay the raised courtyard and paving',
            a.b('island', (0, -.29, 0), (.335, .046, .30), 'earth', .012),
            a.b('stone-plinth', (0, -.245, 0), (.338, .010, .303), 'light-stone'),
            *[a.b('paving-slab', (-.185+i*.065, -.231, .010+j*.068), (.029, .004, .031),
                  'stone' if (i+j)%3 else 'light-stone') for i in range(6) for j in range(4)])
    back = a.b('rear-wing', (-.025, -.003, -.19), (.267, .231, .085), 'plaster')
    holes = [a.b('rear-wing-interior', (-.025, -.012, -.19), (.249, .212, .064), 'plaster')]
    arcade = []
    for x in (-.235, -.13, -.025, .08):
        holes.append(arch(a, (x, -.231, -.105), .036, .182, .045))
        arcade.append(arch_frame(a, (x, -.231, -.096), .036, .182))
    for x in (-.235, -.13, -.025, .08):
        holes.append(a.b('upper-window-cut', (x, .113, -.104), (.029, .043, .04), 'plaster'))
        arcade.append(window(a, (x, .114, -.097), .025, .035, shutters=False))
    a.phase('Hollow the rear wing and open the lower arcade', a.cut('arcaded-wing', back, *holes), *arcade,
            a.b('upper-floor', (-.025, -.012, -.19), (.258, .013, .077), 'wood'),
            a.b('floor-string', (-.025, .012, -.098), (.27, .010, .015), 'light-stone'))
    a.phase('Tile the long roof and detail the upper facade',
            roof(a, (-.025, 0, -.19), .10, .285, .23, .069, angle=90, courses=3),
            a.b('eave-frieze', (-.025, .222, -.097), (.27, .007, .012), 'wood'),
            *[a.b('cornice-block', (-.27+i*.035, .202, -.095), (.007, .011, .014), 'light-stone') for i in range(15)])
    loggia = []
    for z in (-.05, .052, .154):
        loggia.append(a.b('loggia-pier', (-.258, -.128, z), (.019, .105, .020), 'light-stone'))
        loggia.append(a.b('pier-base', (-.258, -.225, z), (.026, .010, .027), 'stone'))
        loggia.append(a.b('pier-capital', (-.258, -.023, z), (.026, .010, .027), 'light-stone'))
    loggia.extend([a.b('loggia-lintel', (-.258, .003, .052), (.027, .023, .132), 'plaster'),
                   a.b('loggia-roof', (-.258, .039, .052), (.066, .009, .145), 'roof',
                       rotation_xyzw=rotation((0, 0, 1), -12)),
                   a.beam('loggia-ridge', (-.30, .055, -.095), (-.30, .055, .199), .006, 'tile-edge')])
    a.phase('Carry an open side loggia into the courtyard', *loggia)
    tower = a.b('bell-tower', (.224, .090, -.166), (.061, .324, .064), 'plaster')
    bell_cut = arch(a, (.224, .308, -.106), .032, .109, .145)
    tower = a.cut('tower-openings', tower, bell_cut,
                  a.b('tower-door-cut', (.224, -.174, -.102), (.026, .057, .040), 'plaster'),
                  arch(a, (.224, -.003, -.102), .030, .130, .040))
    a.phase('Raise the bell tower and its open crown', tower,
            a.b('tower-belt', (.224, .285, -.166), (.068, .009, .071), 'light-stone'),
            a.b('tower-cornice', (.224, .425, -.166), (.072, .012, .075), 'light-stone'),
            a.cone('bell', (.224, .355, -.165), .025, .012, .025, 'brass'),
            a.beam('bell-hanger', (.224, .379, -.165), (.224, .412, -.165), .005, 'dark'),
            roof(a, (.224, 0, -.166), .085, .085, .439, .075, mat='teal', courses=3),
            arch_frame(a, (.224, .308, -.098), .032, .109),
            arch_frame(a, (.224, -.003, -.095), .030, .130),
            window(a, (.287, .104, -.162), .020, .032, angle=90, shutters=False))
    steps = []
    for i in range(12):
        steps.append(a.b('terrace-step', (.177, -.223+i*.019, .183-i*.022),
                         (.042, .012, .014), 'light-stone'))
    steps.extend([a.b('stair-landing', (.204, .003, -.086), (.066, .010, .028), 'light-stone'),
                  a.beam('stair-rail', (.222, -.165, .186), (.222, .052, -.055), .005, 'dark'),
                  a.beam('stair-rail', (.132, -.165, .186), (.132, .052, -.055), .005, 'dark')])
    for i in range(5):
        z = .183-i*.055
        y = -.223+i*.0475
        for x in (.132, .222):
            steps.append(a.beam('stair-baluster', (x, y, z), (x, y+.060, z), .004, 'dark'))
    a.phase('Climb the courtyard stair to the tower landing', *steps)
    fountain = [a.cy('fountain-foot', (-.035, -.221, .106), .081, .009, 'stone'),
                a.cy('fountain-basin', (-.035, -.204, .106), .069, .010, 'light-stone'),
                a.tor('basin-rim', (-.035, -.191, .106), .065, .007, 'light-stone'),
                a.cy('basin-water', (-.035, -.191, .106), .058, .003, 'water'),
                a.cone('fountain-stem', (-.035, -.151, .106), .022, .014, .042, 'light-stone'),
                a.cy('upper-bowl', (-.035, -.106, .106), .037, .006, 'light-stone'),
                a.tor('upper-rim', (-.035, -.098, .106), .033, .005, 'light-stone'),
                a.sp('fountain-finial', (-.035, -.080, .106), .014, 'brass')]
    for i in range(4):
        t = i*math.pi/2
        fountain.append(a.beam('water-thread', (-.035+math.cos(t)*.03, -.105, .106+math.sin(t)*.03),
                               (-.035+math.cos(t)*.044, -.183, .106+math.sin(t)*.044), .003, 'water'))
    a.phase('Place the fountain in the paved square', *fountain)
    banners = []
    for x in (-.182, .025):
        banners.extend([a.beam('banner-rod', (x-.038, .203, -.074), (x+.038, .203, -.074), .004, 'brass'),
                        a.b('banner', (x, .119, -.071), (.028, .079, .005), 'blue'),
                        a.b('banner-hem', (x, .045, -.064), (.028, .006, .003), 'brass')])
    bench = [a.b('bench-seat', (-.16, -.173, .246), (.070, .008, .024), 'wood'),
             a.b('bench-back', (-.16, -.136, .269), (.070, .026, .006), 'wood')]
    for x in (-.21, -.11):
        bench.append(a.b('bench-leg', (x, -.203, .246), (.008, .029, .020), 'dark'))
    a.phase('Hang banners and furnish the garden edges', *banners, *bench,
            pot(a, (-.19, -.23, -.01)), pot(a, (.09, -.23, .23)),
            pot(a, (.275, -.23, .20), tall=True), pot(a, (-.28, -.23, .255)),
            cypress(a, (.285, -.23, .02), .20),
            lantern(a, (.128, -.065, -.097)), lantern(a, (-.287, -.065, -.097)),
            barrel(a, (.27, -.23, -.04)))
    return a


BUILDERS = {'canal-mill': canal_mill, 'hill-observatory': hill_observatory,
            'terraced-courtyard': terraced_courtyard}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, data):
    Path(path).write_text(json.dumps(data, indent=2, allow_nan=False)+'\n')


def prepare():
    records = []
    for subject, build in BUILDERS.items():
        d = build()
        info = CONCEPTS[subject]
        root = d.u(subject, *d.parts, scale=info['scale'])
        names = leaf_names(root)
        assert len(names) == len(set(names)) and len(names) <= 900
        assert sorted(names) == sorted(n for step in d.history for n in step['nodes'])
        scene = SCENES/(subject+'.json')
        preset = PRESETS/(subject+'.json')
        write_scene(ROOT/scene, info['title'], PALETTE, root, d.history)
        config = {'authored-scene': str(scene), 'atlas-axis': 64, 'motion': 'static',
                  'yaw': info['yaw'], 'pitch': info['pitch'], 'distance': info['distance'],
                  'target-y': -.015, 'sun-elevation': 43, 'sun-azimuth': 135,
                  'shadows': 1, 'ao': True, 'ground': False, 'exposure': .4, 'supersample': 2}
        (ROOT/preset).parent.mkdir(parents=True, exist_ok=True)
        save(ROOT/preset, config)
        records.append(dict(subject=subject, scene=str(scene), preset=str(preset),
                            leaves=len(names), history_actions=len(d.history)))
    return records


def capture(output, rows, width=1280, height=960, alternate=True):
    output.mkdir(parents=True, exist_ok=False)
    (output/'inputs').mkdir()
    binary = ROOT/'build/release/projects/sdf/sdf'
    shaders = {p.name: sha(p) for p in sorted((binary.parent/'shaders').glob('*.spv'))}
    receipt = dict(format='cubey.sdf.diorama-drafts', version=1,
                   source_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                   binary_sha256=sha(binary), shaders=shaders, concepts=CONCEPTS,
                   generator_sha256=sha(__file__), runs=[], complete=False)
    shutil.copy2(__file__, output/Path(__file__).name)
    save(output/'drafts.json', receipt)
    for row in rows:
        subject = row['subject']
        scene = output/'inputs'/(subject+'-scene.json')
        shutil.copy2(ROOT/row['scene'], scene)
        config = json.loads((ROOT/row['preset']).read_text())
        config['authored-scene'] = str(scene)
        config_path = output/'inputs'/(subject+'-config.json')
        save(config_path, config)
        for view in (('hero', 'alternate') if alternate else ('hero',)):
            image = output/(subject+'-'+view+'.png')
            command = [str(binary), '--config', str(config_path), '--headless', '--require-validation',
                       '--frames', '1', '--width', str(width), '--height', str(height),
                       '--render-mode', 'balanced', '--supersample', '2', '--output', str(image)]
            if view == 'alternate':
                command += ['--yaw', '-.62', '--pitch', '.48']
            result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=180)
            log = result.stdout+result.stderr
            image.with_suffix('.log').write_text(log)
            if result.returncode or 'VUID-' in log or 'Validation Error' in log:
                receipt['failure'] = dict(subject=subject, view=view, returncode=result.returncode,
                                         log=str(image.with_suffix('.log')))
                save(output/'drafts.json', receipt)
                raise RuntimeError(log[-3500:])
            assert image.is_file()
            receipt['runs'].append(dict(**row, view=view, command=command, image=str(image), image_sha256=sha(image),
                                        scene_input=str(scene), scene_sha256=sha(scene),
                                        config_input=str(config_path), config_sha256=sha(config_path),
                                        stats=[line for line in log.splitlines() if line.startswith('sdf:')]))
            save(output/'drafts.json', receipt)
            print(subject, view, row['leaves'], 'leaves PASS', flush=True)
    assert sha(binary) == receipt['binary_sha256']
    assert all(sha(binary.parent/'shaders'/name) == digest for name, digest in shaders.items())
    receipt.update(complete=True, runtime_unchanged=True)
    save(output/'drafts.json', receipt)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture-dir', type=Path)
    parser.add_argument('--subjects', choices=list(BUILDERS), nargs='+', default=list(BUILDERS))
    parser.add_argument('--hero-only', action='store_true')
    args = parser.parse_args()
    records = prepare()
    print(json.dumps(records), flush=True)
    if args.capture_dir:
        capture(args.capture_dir.resolve(), [r for r in records if r['subject'] in args.subjects],
                alternate=not args.hero_only)
