"""Capture the pinned NVIDIA material donor on an explicitly owned Xvfb display.

The donor's source/shaders stay unchanged. A generated glTF contains rough metal,
opaque dielectric and a closed rough dielectric. This is a visual reproduction,
not a claim that the donor's Disney model equals Cubey's material conventions.
"""
from __future__ import annotations

import argparse
import base64
import ctypes as C
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import time


def control_scene(path):
    positions, normals, indices = [], [], []
    latitude, longitude, radius = 64, 128, .23
    for row in range(latitude + 1):
        theta = math.pi * row / latitude
        for column in range(longitude + 1):
            phi = 2 * math.pi * column / longitude
            n = [math.sin(theta) * math.cos(phi), math.cos(theta),
                 math.sin(theta) * math.sin(phi)]
            positions.extend(radius * x for x in n)
            normals.extend(n)
    for row in range(latitude):
        for column in range(longitude):
            a, b = row * (longitude + 1) + column, (row + 1) * (longitude + 1) + column
            for triangle in ([a, b, a + 1], [a + 1, b, b + 1]):
                p, q, r = [positions[3*i:3*i+3] for i in triangle]
                u, v = [q[i]-p[i] for i in range(3)], [r[i]-p[i] for i in range(3)]
                cross = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
                if sum(x*x for x in cross) < 1e-20:
                    continue
                if sum(cross[i]*p[i] for i in range(3)) < 0:
                    triangle[1], triangle[2] = triangle[2], triangle[1]
                indices.extend(triangle)
    data = (struct.pack('<'+'f'*len(positions), *positions) +
            struct.pack('<'+'f'*len(normals), *normals) +
            struct.pack('<'+'I'*len(indices), *indices))
    position_bytes = len(positions) * 4
    bronze = [.6795424696330938, .386429433787049, .13843161503245183, 1]
    materials = [
        {'name': 'Rough bronze', 'pbrMetallicRoughness': {
            'baseColorFactor': bronze, 'metallicFactor': 1, 'roughnessFactor': .32}},
        {'name': 'Opaque dielectric', 'pbrMetallicRoughness': {
            'baseColorFactor': [.64, .76, .84, 1], 'metallicFactor': 0, 'roughnessFactor': .45}},
        {'name': 'Rough dielectric', 'pbrMetallicRoughness': {
            'baseColorFactor': [1, 1, 1, 1], 'metallicFactor': 0, 'roughnessFactor': .32},
         'extensions': {'KHR_materials_transmission': {'transmissionFactor': 1},
                        'KHR_materials_ior': {'ior': 1.5},
                        'KHR_materials_volume': {'thicknessFactor': .46,
                                                'attenuationColor': [.95, .98, .96],
                                                'attenuationDistance': .7}}},
    ]
    scene = {
        'asset': {'version': '2.0', 'generator': 'Cubey RT donor control authoring'},
        'extensionsUsed': ['KHR_materials_transmission', 'KHR_materials_ior', 'KHR_materials_volume'],
        'scene': 0, 'scenes': [{'nodes': [0, 1, 2, 3]}],
        'nodes': [{'mesh': i, 'translation': [(i-1)*.55, 0, 0]} for i in range(3)] +
                 [{'camera': 0, 'translation': [0, 0, 1.65]}],
        'cameras': [{'type': 'perspective', 'perspective': {
            'yfov': math.pi/4, 'znear': .01, 'zfar': 100}}],
        'materials': materials,
        'meshes': [{'primitives': [{'attributes': {'POSITION': 0, 'NORMAL': 1},
                                    'indices': 2, 'material': i}]} for i in range(3)],
        'buffers': [{'byteLength': len(data), 'uri': 'data:application/octet-stream;base64,' +
                     base64.b64encode(data).decode()}],
        'bufferViews': [{'buffer': 0, 'byteOffset': 0, 'byteLength': position_bytes},
                        {'buffer': 0, 'byteOffset': position_bytes, 'byteLength': position_bytes},
                        {'buffer': 0, 'byteOffset': position_bytes*2,
                         'byteLength': len(indices)*4}],
        'accessors': [{'bufferView': 0, 'componentType': 5126, 'count': len(positions)//3,
                       'type': 'VEC3', 'min': [-radius]*3, 'max': [radius]*3},
                      {'bufferView': 1, 'componentType': 5126, 'count': len(normals)//3,
                       'type': 'VEC3'},
                      {'bufferView': 2, 'componentType': 5125, 'count': len(indices),
                       'type': 'SCALAR'}],
    }
    path.write_text(json.dumps(scene))


def capture(reference, output):
    if os.environ.get('CUBEY_RT_PRIVATE_XVFB') != '1':
        raise RuntimeError('Run under an owned xvfb-run with CUBEY_RT_PRIVATE_XVFB=1')
    output.mkdir(parents=True, exist_ok=False)
    scene = output / 'material-control.gltf'
    control_scene(scene)
    binary = reference / 'bin_x64/Release/vk_raytrace_app'
    command = [str(binary), '-f', str(scene), '-e', str(reference / 'downloaded_resources/std_env.hdr')]
    x, t = C.CDLL('libX11.so.6'), C.CDLL('libXtst.so.6')
    D, W, U, I = C.c_void_p, C.c_ulong, C.c_uint, C.c_int
    for lib, name, args, result in [
        (x, 'XOpenDisplay', [C.c_char_p], D), (x, 'XDefaultRootWindow', [D], W),
        (x, 'XQueryTree', [D, W, C.POINTER(W), C.POINTER(W), C.POINTER(C.POINTER(W)), C.POINTER(U)], I),
        (x, 'XFetchName', [D, W, C.POINTER(C.c_char_p)], I), (x, 'XFree', [D], I),
        (x, 'XSetInputFocus', [D, W, I, W], I), (x, 'XSync', [D, I], I),
        (x, 'XStringToKeysym', [C.c_char_p], W), (x, 'XKeysymToKeycode', [D, W], C.c_ubyte),
        (t, 'XTestFakeKeyEvent', [D, U, I, W], I), (x, 'XCloseDisplay', [D], I),
    ]:
        f = getattr(lib, name)
        f.argtypes, f.restype = args, result
    display = x.XOpenDisplay(None)
    if not display:
        raise RuntimeError('Cannot open owned Xvfb display')
    root, window = x.XDefaultRootWindow(display), None
    env = dict(os.environ, XDG_SESSION_TYPE='x11')
    env.pop('WAYLAND_DISPLAY', None)
    log = output / 'native.log'
    with log.open('w') as stream:
        process = subprocess.Popen(command, cwd=output, env=env, stdout=stream, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 40
            while window is None:
                if process.poll() is not None:
                    raise RuntimeError('Donor exited: ' + log.read_text()[-5000:])
                a, b, children, count = W(), W(), C.POINTER(W)(), U()
                x.XQueryTree(display, root, C.byref(a), C.byref(b), C.byref(children), C.byref(count))
                for i in range(count.value):
                    title = C.c_char_p()
                    if x.XFetchName(display, children[i], C.byref(title)) and title.value:
                        name = title.value.decode(errors='replace')
                        x.XFree(C.cast(title, D))
                        if 'vk_raytrace' in name.lower():
                            window = children[i]
                if children:
                    x.XFree(C.cast(children, D))
                if time.monotonic() > deadline:
                    raise TimeoutError('Donor window')
                time.sleep(.1)

            def key(name):
                x.XSetInputFocus(display, window, 1, 0)
                code = x.XKeysymToKeycode(display, x.XStringToKeysym(name.encode()))
                for pressed in (1, 0):
                    t.XTestFakeKeyEvent(display, code, pressed, 0)
                    x.XSync(display, 0)
                    time.sleep(.1)

            time.sleep(8)
            subprocess.run(['import', '-window', hex(window), str(output / 'native-ui.png')], check=True, timeout=15)
            key('F11')
            time.sleep(6)
            subprocess.run(['import', '-window', hex(window), str(output / 'materials.png')], check=True, timeout=15)
            key('Escape')
            result = process.wait(timeout=15)
            if result:
                raise RuntimeError('Donor exit code: ' + str(result))
            def revision(folder):
                return subprocess.check_output(['git', '-C', str(folder), 'rev-parse', 'HEAD'], text=True).strip()
            receipt = {
                'command': command, 'exit': result,
                'method': 'unmodified NVIDIA shaders, native external X11 capture on owned Xvfb',
                'model': 'default Disney; visual reference only, not Cubey BSDF parity',
                'source': revision(reference / 'vk_raytrace'),
                'core': revision(reference / 'nvpro_core'),
                'vulkan_headers': revision(reference / 'vulkan-headers'),
                'loader': 'Vulkan_generate_extensions=OFF; pinned official enum helper header',
                'sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in [scene, binary, output / 'materials.png', output / 'native-ui.png']},
                'validation_messages': [line for line in log.read_text().splitlines()
                                        if 'VUID-' in line or 'Validation Error' in line],
            }
            (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
            print('Captured native rough metal/dielectric donor:', output)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=15)
            x.XCloseDisplay(display)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    capture(args.reference.resolve(), args.output.resolve())
