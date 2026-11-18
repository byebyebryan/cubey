#!/usr/bin/env python3
"""Build an optional, hash-pinned HLSL oracle. Ordinary builds never invoke DXC."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

SOURCE_SHA256 = "3be4d6e4b923fda7f24556e3f5b3e7460f1370e9c1103181373601afa5559730"
COMMIT = "52efbe37d7f550ea1c5c7ee02b75404ec7c72c09"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--dxc", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    original = args.source.read_bytes()
    if hashlib.sha256(original).hexdigest() != SOURCE_SHA256:
        raise SystemExit("reference source does not match the reviewed public MIT shader")
    source = original.decode()
    source = re.sub(r'#include[^\n]*\n', '', source)
    source = re.sub(r'cbuffer c_TemporalAA.*?\n};', '''
[[vk::binding(0,0)]] cbuffer Constants : register(b0) {
    float4x4 inverse_jittered_vp, current_vp, previous_vp;
    float4 eye, size, controls;
};''', source, flags=re.S)
    source = source.replace('VK_IMAGE_FORMAT("rgba16f")', '[[vk::image_format("rgba16f")]]')
    for name, binding in [('t_UnfilteredRT', 1), ('t_MotionVectors', 2), ('t_FeedbackInput', 3),
                          ('u_ColorOutput', 4), ('u_FeedbackOutput', 5)]:
        source = re.sub(r'^(.*\b' + name + r'\s*: register[^\n]*)$',
                        r'[[vk::binding(' + str(binding) + r',0)]] \1', source, flags=re.M)
    source = source.replace('SamplerState s_Sampler : register(s0);',
                            '[[vk::binding(3,0)]] SamplerState s_Sampler : register(s0);')
    source = source.replace('Texture2D<float> t_HistoryClampRelaxMask : register(t3);', '')
    source = re.sub(r'    if \(g_TemporalAA.useHistoryClampRelax\).*?\n    }', '', source, flags=re.S)
    replacements = {
        'pqC': '10000.0', 'invPqC': '0.0001', 'inputOverOutputViewSize': 'float2(1,1)',
        'outputOverInputViewSize': 'float2(1,1)', 'inputViewOrigin': 'float2(0,0)',
        'inputPixelOffset': 'float2(0,0)', 'outputViewOrigin': 'float2(0,0)',
        'outputViewSize': 'size.xy', 'outputTextureSizeInv': 'size.zw',
        'clampingFactor': 'controls.y', 'newFrameWeight': 'controls.x',
    }
    for field, value in replacements.items():
        source = source.replace('g_TemporalAA.' + field, '(' + value + ')')
    if 'g_TemporalAA.' in source:
        raise SystemExit('unhandled reference constant')
    # Identical Cubey boundary guards; donor distribution/filter/blend code is retained.
    source = source.replace('float3 color = PQEncode(t_UnfilteredRT[globalID].rgb);', '''
    float3 raw = 0;
    if (all(globalID >= 0) && all(globalID < int2(size.xy))) raw = t_UnfilteredRT[globalID].rgb;
    if (any(isnan(raw)) || any(isinf(raw))) raw = 0;
    float3 color = PQEncode(clamp(raw, 0.0, controls.z));''')
    source = source.replace('float2 motion = t_MotionVectors[globalID].rg;', '''
    float2 motion = 0;
    if (all(globalID >= 0) && all(globalID < int2(size.xy))) motion = t_MotionVectors[globalID].rg;
    if (any(isnan(motion)) || any(isinf(motion))) motion = size.xy + 16;''')
    source = source.replace('&& all(sourcePos.xy > (float2(0,0)))',
                            '&& all(sourcePos.xy > float2(1,1))')
    source = source.replace('&& all(sourcePos.xy < (float2(0,0)) + (size.xy))',
                            '&& all(sourcePos.xy < size.xy - 1)')
    source = source.replace('GroupMemoryBarrierWithGroupSync();',
        'GroupMemoryBarrierWithGroupSync();\n    if (any(i_globalIdx >= int2(size.xy))) return;')
    args.output.mkdir(parents=True, exist_ok=True)
    shader = args.output / 'donut_native_oracle.hlsl'
    shader.write_text(source)
    binary = args.output / 'donut_native_oracle.spv'
    command = [str(args.dxc.resolve()), '-spirv', '-fspv-target-env=vulkan1.2',
               '-fvk-use-dx-layout', '-T', 'cs_6_0', '-E', 'main',
               '-D', 'SAMPLE_COUNT=1', '-D', 'USE_CATMULL_ROM_FILTER=1',
               '-Fo', str(binary), str(shader)]
    env = os.environ.copy()
    env['LD_LIBRARY_PATH'] = str(args.dxc.resolve().parent.parent / 'lib')
    subprocess.run(command, check=True, env=env)
    (args.output / 'identity.json').write_text(json.dumps({
        'source_commit': COMMIT, 'source_sha256': SOURCE_SHA256,
        'source_url': f'https://github.com/NVIDIA-RTX/Donut/blob/{COMMIT}/shaders/passes/taa_cs.hlsl',
        'adapted_sha256': hashlib.sha256(shader.read_bytes()).hexdigest(),
        'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'compiler_sha256': hashlib.sha256(args.dxc.read_bytes()).hexdigest(),
        'command': command,
        'specialization': 'native resolution, zero resolve input offset, no relax mask, PQ C=10000',
        'boundary_changes': 'finite/clamp input guards, explicit OOB loads, one pixel inset, dispatch bounds',
    }, indent=2) + '\n')


if __name__ == '__main__':
    main()
