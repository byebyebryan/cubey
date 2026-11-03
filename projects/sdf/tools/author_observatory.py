#!/usr/bin/env python3
"""Selected observatory: staged native modeling passes and a local CSG section."""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess

from author_diorama_drafts import Draft, PALETTE, arch, arch_frame, cypress, lantern, leaf_names, pot, window
from sdf_author import capsule, rotation, subtract, write_scene

ROOT = Path(__file__).resolve().parents[3]
SCENES = Path('projects/sdf/scenes/observatory-2026-11')
PRESETS = Path('projects/sdf/presets/observatory-2026-11')
CX, CZ = -.075, -.075
STAGES = {'site': 1, 'instrument': 2, 'furnished': 3, 'section': 3}
TITLES = {'site': 'Pass 1 / site and architecture', 'instrument': 'Pass 2 / dome and telescope',
          'furnished': 'Pass 3 / furnished observatory', 'section': 'Pass 3 / architectural section'}


class Observatory(Draft):
    def __init__(self):
        super().__init__()
        self.shell_indices = []

    def phase(self, label, *nodes, shell=False):
        if shell:
            self.shell_indices.extend(range(len(self.parts), len(self.parts)+len(nodes)))
        super().phase(label, *nodes)

    def axial(self, label, start, end, radius, mat='brass'):
        node = capsule(self.name(label), start, end, radius, mat)
        node['type'] = 'cylinder'
        return node

    def place(self, label, nodes, p, angle=0):
        if len(nodes) != 1:
            return super().place(label, nodes, p, angle)
        # A one-child operator is invalid. Compose the parent pose directly
        # onto its lone child, including an already positioned primitive.
        node = copy.deepcopy(nodes[0])
        t = math.radians(angle)
        x, y, z = node.get('position', [0, 0, 0])
        node['position'] = [p[0]+math.cos(t)*x+math.sin(t)*z, p[1]+y,
                            p[2]-math.sin(t)*x+math.cos(t)*z]
        qx, qy, qz, qw = node.get('rotation_xyzw', [0, 0, 0, 1])
        s, c = math.sin(t/2), math.cos(t/2)
        node['rotation_xyzw'] = [c*qx+s*qz, c*qy+s*qw, c*qz-s*qx, c*qw-s*qy]
        return node

    def radial(self, label, nodes, angle, center=(CX, 0, CZ)):
        return self.place(label, nodes, center, angle)


def add_site(a):
    rock = a.cy('rock-core', (0, -.267, 0), .311, .063, 'earth')
    tools = []
    for i in range(9):
        t = i*math.tau/9
        r = .283+(i%3)*.008
        tools.append(a.b('rock-plane', (math.sin(t)*(r+.15), -.26, math.cos(t)*(r+.15)),
                         (.19, .16, .15), 'earth', .002,
                         rotation_xyzw=rotation((0, 1, 0), math.degrees(t))))
    for x, z, angle in ((-.18, .24, -18), (.075, .288, 14), (.267, .10, 22), (-.274, -.03, -12)):
        tools.append(a.b('rock-fracture', (x, -.268, z), (.004, .078, .045), 'earth', .001,
                         rotation_xyzw=rotation((0, 0, 1), angle)))
    a.phase('Shape the fractured rock foundation',
            a.cy('thin-display-foot', (0, -.340, 0), .325, .010, 'dark'),
            a.cut('fractured-hill', rock, *tools))
    strata = []
    for i in range(14):
        t = i*math.tau/14
        r = .275+(i%3)*.005
        strata.append(a.b('rock-ledge', (math.sin(t)*r, -.271+(i%2)*.029, math.cos(t)*r),
                          (.042+(i%3)*.007, .015, .030), 'stone' if i%4 else 'earth', .007,
                          rotation_xyzw=rotation((0, 1, 0), math.degrees(t)+(i%3-1)*9)))
    a.phase('Expose layered ledges around the cliff', *strata,
            a.cy('lower-terrace', (0, -.207, 0), .284, .016, 'stone'),
            a.tor('lower-coping', (0, -.191, 0), .281, .006, 'light-stone'),
            a.cy('upper-terrace', (-.045, -.138, -.07), .215, .048, 'stone'),
            a.cy('upper-coping', (-.045, -.087, -.07), .221, .006, 'light-stone'))
    blocks = []
    for row in range(2):
        for i in range(22):
            angle = i*360/22+row*8
            # The stair meets the terrace on this sector.
            if 58 < angle < 115:
                continue
            t = math.radians(angle)
            blocks.append(a.b('retaining-block', (-.045+math.sin(t)*.213, -.160+row*.039, -.07+math.cos(t)*.213),
                              (.028, .017, .012), 'light-stone' if (i+row)%4 else 'stone', .002,
                              rotation_xyzw=rotation((0, 1, 0), angle)))
    a.phase('Face the upper retaining wall with offset courses', *blocks)
    paving = []
    for i in range(10):
        t = i/9
        x = -.21+.36*t
        z = .17+.067*math.sin(math.pi*t)
        paving.append(a.b('approach-slab', (x, -.184, z), (.018, .004, .020),
                          'light-stone' if i%3 else 'stone', .002,
                          rotation_xyzw=rotation((0, 1, 0), -15+30*t)))
    stairs = []
    for i in range(10):
        top = -.187+i*.0115
        z = .253-i*.021
        stairs.extend([a.b('stair-mass', (.17, (-.208+top)/2, z), (.044, (top+.208)/2, .013), 'stone'),
                       a.b('stair-tread', (.17, top, z), (.047, .004, .014), 'light-stone', .001)])
    for i in range(5):
        z = .253-i*.047
        y = -.187+i*.02575
        stairs.append(a.beam('stair-post', (.222, y, z), (.222, y+.062, z), .004, 'dark'))
        stairs.append(a.sp('rail-finial', (.222, y+.064, z), .005, 'brass'))
    stairs.extend([a.beam('stair-handrail', (.222, -.122, .253), (.222, -.020, .065), .005, 'dark'),
                   a.b('landing', (.17, -.082, .043), (.057, .008, .027), 'light-stone')])
    a.phase('Pave the approach and give the stair a solid support', *paving, *stairs)
    a.phase('Support the keeper annex and its connecting gallery',
            a.b('annex-foundation', (.165, -.139, -.144), (.075, .053, .087), 'stone'),
            a.b('annex-foot-course', (.165, -.085, -.144), (.078, .008, .089), 'light-stone'),
            a.b('gallery-floor', (.075, -.077, -.126), (.036, .009, .040), 'light-stone'))


def add_drum(a):
    body = a.cy('drum', (CX, .036, CZ), .129, .126, 'plaster')
    openings = [a.cy('room', (CX, .049, CZ), .105, .129, 'plaster'),
                arch(a, (CX, -.088, CZ+.123), .036, .150, .040)]
    surrounds = [arch_frame(a, (CX, -.088, CZ+.135), .036, .150)]
    for angle in (-68, 68, 170):
        openings.append(a.radial('window-opening', [arch(a, (0, .045, .125), .024, .066, .038)], angle))
        surrounds.append(a.radial('arched-window-stone', [arch_frame(a, (0, .045, .135), .024, .066)], angle))
        surrounds.append(a.radial('window-bars', [
            a.b('window-glass', (0, .075, .116), (.021, .029, .003), 'blue'),
            a.b('window-mullion', (0, .075, .137), (.0025, .028, .003), 'wood'),
            a.b('window-crossbar', (0, .077, .137), (.022, .0025, .003), 'wood')], angle))
    openings.append(a.radial('gallery-opening', [arch(a, (0, -.076, .126), .027, .127, .035)], 110))
    a.phase('Hollow the drum and carve the entrance, windows and gallery',
            a.cut('opened-drum', body, *openings), *surrounds,
            a.tor('stone-base-course', (CX, -.079, CZ), .131, .008, 'light-stone'),
            a.tor('upper-frieze', (CX, .150, CZ), .132, .008, 'light-stone'), shell=True)
    details = []
    for i in range(14):
        angle = i*360/14
        if angle < 26 or angle > 334:
            continue
        t = math.radians(angle)
        details.append(a.radial('drum-foot-block', [a.b('stone-block', (0, -.053, .128),
                                                      (.027, .016, .008), 'stone')], angle))
    # Separate voussoirs make the front arch easier to read at the hero scale.
    for i in range(7):
        t = i*math.pi/6
        details.append(a.b('arch-stone-joint', (CX+math.cos(t)*.042, .026+math.sin(t)*.042, CZ+.146),
                           (.004, .010, .003), 'stone', .001,
                           rotation_xyzw=rotation((0, 0, 1), math.degrees(t)-90)))
    details.append(a.cy('compass-plaque', (CX, .112, CZ+.131), .022, .004, 'dark',
                        rotation_xyzw=rotation((1, 0, 0), 90)))
    for i in range(8):
        t = i*math.pi/4
        details.append(a.beam('compass-ray', (CX, .112, CZ+.137),
                              (CX+math.sin(t)*(.019 if i%2==0 else .013),
                               .112+math.cos(t)*(.019 if i%2==0 else .013), CZ+.137), .002, 'brass'))
    a.phase('Detail the entrance stones and compass medallion', *details, shell=True)
    doors = []
    for side in (-1, 1):
        pieces = [a.b('door-leaf', (-side*.016, -.027, 0), (.016, .059, .004), 'wood'),
                  a.b('door-upper-panel', (-side*.016, -.001, .005), (.010, .023, .002), 'teal'),
                  a.b('door-lower-panel', (-side*.016, -.054, .005), (.010, .020, .002), 'teal'),
                  a.sp('door-knob', (-side*.026, -.030, .008), .003, 'brass')]
        doors.append(a.place('open-door', pieces, (CX+side*.036, 0, CZ+.129), -side*58))
    a.phase('Open the two paneled entrance doors', *doors, shell=True)


def add_dome(a, stage):
    pieces = [a.sp('copper-dome', (CX, .160, CZ), .144, 'teal')]
    if stage >= 2:
        # Exact circular meridians replace a densely sampled polyline cage.
        for i in range(6):
            angle = i*30
            qx = rotation((1, 0, 0), 90)
            qy = rotation((0, 1, 0), angle)
            x1, y1, z1, w1 = qy
            x2, y2, z2, w2 = qx
            q = [w1*x2+x1*w2+y1*z2-z1*y2, w1*y2-x1*z2+y1*w2+z1*x2,
                 w1*z2+x1*y2-y1*x2+z1*w2, w1*w2-x1*x2-y1*y2-z1*z2]
            pieces.append(a.tor('standing-meridian', (CX, .160, CZ), .145, .0031, 'patina-dark',
                                rotation_xyzw=q))
    shell = a.cut('hollow-slit-dome', a.u('dome-and-seams', *pieces),
                  a.sp('dome-interior', (CX, .160, CZ), .130, 'teal'),
                  a.b('hemisphere-trim', (CX, -.10, CZ), (.22, .26, .22), 'teal'),
                  a.b('observing-slit', (CX, .267, CZ+.085), (.032, .118, .12), 'teal'))
    parts = [shell, a.tor('dome-turntable', (CX, .162, CZ), .146, .006, 'brass'),
             a.tor('turntable-shadow-gap', (CX, .151, CZ), .144, .004, 'dark')]
    for side in (-1, 1):
        points = [(CX+side*.036, .179, CZ+.140), (CX+side*.036, .245, CZ+.109),
                  (CX+side*.036, .291, CZ+.047), (CX+side*.036, .300, CZ)]
        parts.extend(a.beam('slit-edge', p, q, .004, 'brass') for p, q in zip(points, points[1:]))
    if stage >= 2:
        for i in range(24):
            t = i*math.tau/24
            parts.append(a.radial('ring-gear-tooth', [a.b('tooth', (0, .154, .149),
                                                        (.003, .004, .004), 'patina-dark', .001)], i*15))
            if i%2 == 0:
                parts.append(a.radial('turntable-bracket', [
                    a.b('bracket', (0, .137, .133), (.005, .012, .007), 'patina-dark'),
                    a.sp('bracket-stud', (0, .139, .142), .0032, 'brass')], i*15))
        for latitude in (25, 55):
            v = math.radians(latitude)
            for i in range(16):
                t = i*math.tau/16
                x, y, z = CX+.146*math.cos(v)*math.sin(t), .16+.146*math.sin(v), CZ+.146*math.cos(v)*math.cos(t)
                if abs(x-CX)<.038 and z>CZ-.04:
                    continue
                parts.append(a.sp('dome-rivet', (x, y, z), .0025, 'brass'))
    a.phase('Fit the hollow observing dome and its turntable', *parts, shell=True)


def add_telescope(a, stage):
    if stage == 1:
        a.phase('Retain the draft telescope for the first architecture pass',
                a.cy('draft-pedestal', (CX, .061, CZ), .016, .090, 'dark'),
                a.beam('draft-tube', (CX, .169, CZ-.035), (CX, .263, CZ+.13), .026, 'brass'),
                a.sp('draft-lens', (CX, .265, CZ+.136), .022, 'blue'))
        return
    start, end = (CX, .165, CZ-.050), (CX, .263, CZ+.130)
    vector = [b-x for x, b in zip(start, end)]
    length = math.sqrt(sum(x*x for x in vector))

    def point(t):
        return tuple(x+d*t for x, d in zip(start, vector))

    tube_parts = [a.axial('optical-barrel', start, end, .027, 'linen')]
    for t in (.04, .30, .73, 1.0):
        tube_parts.append(a.axial('tube-collar', point(t-.018), point(t+.018), .030, 'brass'))
    # Bore the assembled tube and its collars together: a solid front collar
    # would otherwise hide the recessed objective with a brass end cap.
    barrel = a.cut('bored-optical-tube', a.u('tube-with-collars', *tube_parts),
                   a.axial('optical-bore', point(-.02), point(1.04), .020, 'linen'))
    parts = [barrel, a.axial('recessed-objective', point(.976), point(.985), .0195, 'blue'),
             a.axial('eyepiece', point(-.14), point(-.02), .009, 'dark'),
             a.axial('eyepiece-rim', point(-.15), point(-.13), .012, 'brass')]
    pivot = point(.40)
    parts.extend([a.cy('mount-foot', (CX, -.060, CZ+.022), .037, .014, 'dark'),
                  a.cone('mount-pedestal', (CX, .020, CZ+.022), .030, .020, .067, 'teal'),
                  a.tor('pedestal-base-ring', (CX, -.043, CZ+.022), .030, .004, 'brass'),
                  a.b('fork-crossbar', (CX, .096, CZ+.022), (.045, .009, .014), 'patina-dark')])
    for side in (-1, 1):
        parts.append(a.b('fork-arm', (CX+side*.040, .15, CZ+.022), (.008, .052, .013), 'teal'))
        parts.append(a.axial('pivot-cap', (CX+side*.046, pivot[1], pivot[2]),
                             (CX+side*.057, pivot[1], pivot[2]), .016, 'brass'))
    parts.extend([a.axial('declination-axle', (CX-.05, pivot[1], pivot[2]), (CX+.05, pivot[1], pivot[2]), .009, 'dark'),
                  a.beam('counterweight-shaft', (CX, .097, CZ+.022), (CX+.080, .067, CZ+.022), .006, 'brass'),
                  a.axial('counterweight', (CX+.067, .071, CZ+.022), (CX+.087, .0635, CZ+.022), .019, 'dark'),
                  a.axial('focuser', (CX+.025, .184, CZ-.015), (CX+.042, .184, CZ-.015), .006, 'dark'),
                  a.tor('focus-wheel', (CX+.044, .184, CZ-.015), .012, .003, 'brass',
                        rotation_xyzw=rotation((0, 0, 1), 90))])
    a.phase('Bore the telescope and assemble the fork, collars and counterweight', *parts)


def add_annex(a):
    # Clip a tall box with two half-plane tools to form the gable.
    from author_diorama_drafts import gable
    annex = gable(a, (.165, 0, -.144), .067, .078, -.09, .107, .064)
    annex = a.cut('keeper-room', annex,
                  a.b('keeper-room-interior', (.165, .005, -.144), (.052, .082, .062), 'plaster'),
                  a.b('front-window-opening', (.165, .042, -.067), (.026, .035, .030), 'plaster'),
                  a.b('gallery-door', (.099, -.014, -.126), (.030, .062, .027), 'plaster'))
    roof_parts = []
    half_span, rise = .080, .071
    theta = math.atan2(rise, half_span)
    for side in (-1, 1):
        roof_parts.append(a.b('annex-roof-plane', (.165+side*half_span/2, .108+rise/2, -.144),
                              (math.hypot(rise, half_span)/2+.009, .008, .092), 'teal',
                              rotation_xyzw=rotation((0, 0, 1), -side*math.degrees(theta))))
        for i in range(5):
            z = -.225+i*.041
            roof_parts.append(a.beam('annex-standing-seam', (.165+side*.005, .190, z),
                                     (.165+side*.083, .123, z), .0027, 'patina-dark'))
        roof_parts.append(a.beam('annex-eave', (.165+side*.084, .104, -.240),
                                 (.165+side*.084, .104, -.048), .006, 'wood'))
    roof_parts.append(a.beam('annex-ridge', (.165, .187, -.241), (.165, .187, -.047), .006, 'brass'))
    quoins = []
    for x in (.099, .231):
        for y in (-.062, -.017, .028, .073):
            quoins.append(a.b('annex-quoin', (x, y, -.064), (.012, .010, .007), 'light-stone'))
    a.phase('Hollow the keeper annex and detail its standing-seam roof', annex, *roof_parts, *quoins,
            window(a, (.165, .042, -.060), .022, .030, shutters=True),
            a.b('gallery-canopy', (.075, .064, -.126), (.033, .006, .046), 'teal'),
            a.beam('gallery-post', (.076, -.07, -.086), (.076, .062, -.086), .004, 'dark'), shell=True)
    clock = [a.cy('clock-face', (.165, .128, -.061), .023, .005, 'linen', rotation_xyzw=rotation((1, 0, 0), 90)),
             a.tor('clock-bezel', (.165, .128, -.054), .024, .003, 'brass', rotation_xyzw=rotation((1, 0, 0), 90))]
    for i in range(12):
        t = i*math.tau/12
        clock.append(a.b('clock-tick', (.165+math.sin(t)*.018, .128+math.cos(t)*.018, -.054),
                         (.0017, .003, .002), 'dark', .0006, rotation_xyzw=rotation((0, 0, 1), -math.degrees(t))))
    clock.extend([a.beam('clock-hand', (.165, .128, -.050), (.165, .143, -.050), .002, 'dark'),
                  a.beam('clock-hand', (.165, .128, -.050), (.177, .121, -.050), .002, 'dark'),
                  a.beam('weather-vane-mast', (.165, .180, -.195), (.165, .247, -.195), .003, 'brass'),
                  a.beam('vane-arrow', (.127, .231, -.195), (.202, .231, -.195), .003, 'brass'),
                  a.b('vane-tail', (.137, .231, -.195), (.009, .009, .002), 'patina-dark'),
                  a.sp('vane-point', (.202, .231, -.195), .005, 'brass')])
    a.phase('Add the keeper clock and roof vane', *clock, shell=True)


def add_balcony(a):
    a.phase('Lay the observatory balcony deck', a.cy('balcony-platform', (CX, -.080, CZ), .178, .009, 'light-stone'))
    rails, points = [], []
    for i in range(18):
        t = (-.62+i*1.23/17)*math.pi
        x, z = CX+math.sin(t)*.170, CZ+math.cos(t)*.170
        if abs(x-CX)<.044 and z>CZ:
            continue
        rails.extend([a.beam('baluster', (x, -.073, z), (x, -.013, z), .0038, 'dark'),
                      a.sp('rail-stud', (x, -.012, z), .0045, 'brass')])
        points.append((x, -.012, z))
    for p, q in zip(points, points[1:]):
        if math.dist(p, q)<.065:
            rails.append(a.beam('top-rail', p, q, .0045, 'dark'))
            rails.append(a.beam('lower-rail', (p[0], -.049, p[2]), (q[0], -.049, q[2]), .0025, 'dark'))
    a.phase('Forge the balcony rail and brass finials', *rails, shell=True)


def add_instruments_and_garden(a, stage):
    instruments = [a.cy('armillary-foot', (-.15, -.179, .156), .034, .009, 'stone'),
                   a.cone('armillary-pier', (-.15, -.136, .156), .024, .018, .036, 'light-stone'),
                   a.cy('armillary-cradle', (-.15, -.075, .156), .009, .026, 'brass')]
    for axis, angle in (((0, 1, 0), 0), ((1, 0, 0), 68), ((0, 0, 1), 72)):
        instruments.append(a.tor('armillary-ring', (-.15, -.037, .156), .047, .0035, 'brass', rotation_xyzw=rotation(axis, angle)))
    instruments.extend([a.sp('armillary-globe', (-.15, -.037, .156), .014, 'blue'),
                        a.beam('polar-axis', (-.17, -.076, .156), (-.13, .009, .156), .0025, 'dark')])
    for i in range(12):
        t = i*math.tau/12
        instruments.append(a.sp('armillary-mark', (-.15+math.sin(t)*.047, -.037, .156+math.cos(t)*.047), .0025, 'patina-dark'))
    for x, z in ((.10, .14), (.18, .14), (.14, .21)):
        instruments.append(a.beam('tripod-leg', (x, -.185, z), (.14, -.080, .17), .0045, 'wood'))
    instruments.extend([a.axial('survey-tube', (.105, -.075, .14), (.174, -.045, .20), .012, 'brass'),
                        a.axial('survey-objective', (.171, -.046, .197), (.177, -.044, .203), .013, 'dark'),
                        a.cy('tripod-head', (.14, -.083, .17), .012, .015, 'patina-dark')])
    a.phase('Calibrate the armillary and terrace survey instrument', *instruments)
    garden = [cypress(a, (-.27, -.187, -.08), .22), cypress(a, (.252, -.187, -.104), .15),
              pot(a, (-.22, -.187, .08)), pot(a, (.04, -.187, .22))]
    if stage >= 3:
        for x, y, z in ((-.255, -.211, .14), (.17, -.289, .251), (-.10, -.303, .295), (.277, -.24, .09)):
            garden.extend(a.sp('moss', (x+dx, y, z), .010, 'leaf' if j%2 else 'leaf-light')
                          for j, dx in enumerate((-.012, 0, .012)))
        for x, z in ((-.24, .12), (.07, .25), (-.19, -.19)):
            for i in range(3):
                garden.append(a.beam('grass-tuft', (x, -.188, z), (x+(i-1)*.010, -.157+(i%2)*.006, z+.004*i), .0025, 'leaf'))
    a.phase('Plant the terrace and cliff ledges', *garden)
    a.phase('Mount the two entrance lanterns on the architecture',
            lantern(a, (CX-.064, -.017, CZ+.139)), lantern(a, (.226, .023, -.061)), shell=True)


def add_furnishings(a, stage):
    floors = [a.cy('room-subfloor', (CX, -.078, CZ), .104, .005, 'wood'),
              a.b('keeper-subfloor', (.165, -.078, -.144), (.053, .005, .063), 'wood')]
    for i in range(10):
        dx = -.090+i*.020
        half = math.sqrt(max(0, .100**2-(abs(dx)+.009)**2))
        floors.append(a.b('room-floorboard', (CX+dx, -.071, CZ), (.009, .0025, half), 'wood', .001))
    a.phase('Lay the room and keeper-cabin timber floors', *floors)
    if stage < 3:
        return
    bookcase = []
    for y in (-.059, -.026, .007):
        bookcase.append(a.b('bookcase-shelf', (CX, y, CZ-.083), (.073, .003, .017), 'wood'))
    for x in (CX-.071, CX+.071):
        bookcase.append(a.b('bookcase-end', (x, -.023, CZ-.084), (.003, .040, .017), 'wood'))
    for row in range(2):
        for i in range(12):
            height = .011+(i%3)*.0025
            bookcase.append(a.b('book-spine', (CX-.062+i*.011, -.054+row*.033+height, CZ-.080),
                               (.0045, height, .010), ('linen', 'blue', 'roof', 'teal')[i%4], .001))
    a.phase('Shelve the observing logs against the curved back wall', *bookcase)
    table = [a.b('chart-table', (.165, -.012, -.145), (.044, .006, .026), 'wood'),
             a.b('chart-sheet', (.158, -.004, -.143), (.025, .0015, .019), 'linen', .0005),
             a.axial('chart-roll', (.137, .002, -.161), (.18, .002, -.161), .006, 'linen'),
             a.axial('roll-core', (.134, .002, -.161), (.183, .002, -.161), .002, 'dark')]
    for x in (.128, .202):
        for z in (-.166, -.123):
            table.append(a.b('table-leg', (x, -.043, z), (.003, .028, .003), 'wood'))
    table.extend([a.cy('keeper-stool', (.165, -.047, -.096), .016, .004, 'wood'),
                  a.b('stool-foot', (.165, -.062, -.096), (.011, .012, .009), 'patina-dark'),
                  a.cy('ink-pot', (.194, .002, -.139), .005, .006, 'blue')])
    star_points = [(.142, -.130), (.150, -.144), (.167, -.137), (.172, -.151), (.183, -.144)]
    for x, z in star_points:
        table.append(a.sp('chart-star', (x, -.001, z), .002, 'blue'))
    for p, q in zip(star_points, star_points[1:]):
        table.append(a.beam('chart-constellation', (p[0], -.001, p[1]), (q[0], -.001, q[1]), .0009, 'blue'))
    a.phase('Furnish the keeper room with a chart desk and stool', *table)
    cabinet = [a.b('eyepiece-cabinet', (CX+.066, -.039, CZ-.024), (.021, .030, .032), 'wood'),
               a.b('cabinet-door', (CX+.066, -.038, CZ+.010), (.018, .025, .003), 'teal'),
               a.sp('cabinet-pull', (CX+.076, -.032, CZ+.016), .003, 'brass')]
    for i in range(3):
        cabinet.append(a.cy('spare-eyepiece', (CX+.054+i*.012, -.002, CZ-.030), .0045, .008, 'brass'))
    a.phase('Store the interchangeable eyepieces beside the mount', *cabinet)


def build(variant):
    stage = STAGES[variant]
    a = Observatory()
    add_site(a)
    add_drum(a)
    add_dome(a, stage)
    add_telescope(a, stage)
    add_annex(a)
    add_balcony(a)
    add_instruments_and_garden(a, stage)
    add_furnishings(a, stage)
    if variant == 'section':
        indices = set(a.shell_indices)
        shell = a.u('sectionable-architecture', *[node for i, node in enumerate(a.parts) if i in indices])
        tool = a.b('architectural-section-tool', (0, .12, .20), (.34, .26, .30), 'plaster', .002)
        clipped = subtract(a.name('sectioned-architecture'), shell, tool)
        root = a.u('observatory-with-section', clipped, *[node for i, node in enumerate(a.parts) if i not in indices], scale=.68)
        a.history.append(dict(nodes=[tool['name']], label='Section the architecture while retaining the instruments and furniture', seconds=1.3))
    else:
        root = a.u('hill-observatory', *a.parts, scale=.68)
    names = leaf_names(root)
    assert len(names)==len(set(names)) and len(names)<=900
    assert sorted(names)==sorted(n for step in a.history for n in step['nodes'])
    palette = copy.deepcopy(PALETTE)
    palette = [m for m in palette if m['name']!='water']
    from sdf_author import material
    palette.append(material('patina-dark', '335E59', .78))
    return root, a.history, palette


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, data):
    Path(path).write_text(json.dumps(data, indent=2, allow_nan=False)+'\n')


def prepare():
    rows = []
    for variant in STAGES:
        root, history, palette = build(variant)
        scene, preset = SCENES/(variant+'.json'), PRESETS/(variant+'.json')
        write_scene(ROOT/scene, 'Hill observatory / '+TITLES[variant], palette, root, history)
        config = {'authored-scene': str(scene), 'atlas-axis': 64, 'motion': 'static',
                  'yaw': .53, 'pitch': .43, 'distance': 1.20, 'target-y': -.015,
                  'sun-elevation': 43, 'sun-azimuth': 135, 'shadows': 1, 'ao': True,
                  'ground': False, 'exposure': .4, 'supersample': 2}
        (ROOT/preset).parent.mkdir(parents=True, exist_ok=True)
        save(ROOT/preset, config)
        rows.append(dict(variant=variant, title=TITLES[variant], scene=str(scene), preset=str(preset),
                         leaves=len(leaf_names(root)), history_actions=len(history)))
    return rows


def capture(output, rows, only_hero=False):
    output.mkdir(parents=True, exist_ok=False)
    (output/'inputs').mkdir()
    binary = ROOT/'build/release/projects/sdf/sdf'
    shaders = {p.name: sha(p) for p in sorted((binary.parent/'shaders').glob('*.spv'))}
    receipt = dict(format='cubey.sdf.observatory-detail', version=1,
                   source_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                   generator_sha256=sha(__file__), binary_sha256=sha(binary), shaders=shaders,
                   runs=[], complete=False)
    shutil.copy2(__file__, output/Path(__file__).name)
    shutil.copy2(Path(__file__).with_name('author_diorama_drafts.py'), output/'author_diorama_drafts.py')
    shutil.copy2(Path(__file__).with_name('sdf_author.py'), output/'sdf_author.py')
    save(output/'captures.json', receipt)
    for row in rows:
        variant = row['variant']
        scene = output/'inputs'/(variant+'-scene.json')
        shutil.copy2(ROOT/row['scene'], scene)
        config = json.loads((ROOT/row['preset']).read_text())
        config['authored-scene'] = str(scene)
        config_path = output/'inputs'/(variant+'-config.json')
        save(config_path, config)
        views = {'hero': []}
        if not only_hero:
            if variant in ('furnished', 'section'):
                views['alternate'] = ['--yaw', '-.62', '--pitch', '.48']
            if variant == 'furnished':
                views['dome-close'] = ['--yaw', '.37', '--pitch', '.24', '--distance', '.70', '--target-y', '.12']
            if variant == 'section':
                views['interior-close'] = ['--yaw', '.14', '--pitch', '.30', '--distance', '.72', '--target-y', '.035']
        for view, options in views.items():
            image = output/(variant+'-'+view+'.png')
            command = [str(binary), '--config', str(config_path), '--headless', '--require-validation',
                       '--frames', '1', '--width', '1600', '--height', '1200', '--render-mode', 'balanced',
                       '--supersample', '2', '--output', str(image), *options]
            result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=240)
            log = result.stdout+result.stderr
            image.with_suffix('.log').write_text(log)
            if result.returncode or 'VUID-' in log or 'Validation Error' in log:
                receipt['failure'] = dict(variant=variant, view=view, returncode=result.returncode,
                                         log=str(image.with_suffix('.log')))
                save(output/'captures.json', receipt)
                raise RuntimeError(log[-4000:])
            assert image.is_file()
            receipt['runs'].append(dict(**row, view=view, command=command, image=str(image),
                                        image_sha256=sha(image), scene_input=str(scene), scene_sha256=sha(scene),
                                        config_input=str(config_path), config_sha256=sha(config_path),
                                        stats=[line for line in log.splitlines() if line.startswith('sdf:')]))
            save(output/'captures.json', receipt)
            print(variant, view, row['leaves'], 'leaves PASS', flush=True)
    assert sha(binary)==receipt['binary_sha256']
    assert all(sha(binary.parent/'shaders'/name)==h for name, h in shaders.items())
    receipt.update(complete=True, runtime_unchanged=True)
    save(output/'captures.json', receipt)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture-dir', type=Path)
    parser.add_argument('--variants', choices=list(STAGES), nargs='+', default=list(STAGES))
    parser.add_argument('--hero-only', action='store_true')
    args = parser.parse_args()
    records = prepare()
    print(json.dumps(records), flush=True)
    if args.capture_dir:
        capture(args.capture_dir.resolve(), [r for r in records if r['variant'] in args.variants], args.hero_only)
