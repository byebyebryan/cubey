# /// script
# dependencies = ["numpy==2.4.6"]
# ///
"""Read-only geometric and historical-window analysis for G3 planning."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import numpy as np

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = args.output
root.mkdir(parents=True, exist_ok=True)
path = Path('cache/raytracing/stanford-dragon/optical-v1/dragon-optical.glb')
raw = path.read_bytes()
assert hashlib.sha256(raw).hexdigest() == 'bfc08772bd797a356e9b61a400b5bda735270940b55cba3fbf4fd08ae8c01c47'
length = struct.unpack_from('<I', raw, 12)[0]
doc = json.loads(raw[20:20+length])
data = raw[28+length:]
primitive = doc['meshes'][0]['primitives'][0]


def accessor(index):
    a = doc['accessors'][index]
    v = doc['bufferViews'][a['bufferView']]
    width = 3 if a['type'] == 'VEC3' else 1
    return np.frombuffer(data, dtype={5126:'<f4',5125:'<u4'}[a['componentType']],
                         count=a['count']*width,
                         offset=v.get('byteOffset',0)+a.get('byteOffset',0)).reshape(-1,width)


def percentiles(values):
    return {str(p): float(np.percentile(values,p)) for p in [0,50,95,99,99.9,100]}


positions = accessor(primitive['attributes']['POSITION']).astype(float)
normals = accessor(primitive['attributes']['NORMAL']).astype(float)
normals /= np.linalg.norm(normals,axis=1)[:,None]
faces = accessor(primitive['indices']).reshape(-1,3)
triangles = positions[faces]
geometric = np.cross(triangles[:,1]-triangles[:,0], triangles[:,2]-triangles[:,0])
geometric /= np.linalg.norm(geometric,axis=1)[:,None]
corner_dot = np.einsum('ijk,ik->ij',normals[faces],geometric)
smooth = normals[faces].mean(axis=1)
smooth /= np.linalg.norm(smooth,axis=1)[:,None]
center_dot = np.einsum('ij,ij->i',smooth,geometric)
_,welded = np.unique(positions,axis=0,return_inverse=True)
welded_faces = welded[faces]
edges = np.concatenate([welded_faces[:,[1,2]],welded_faces[:,[2,0]],welded_faces[:,[0,1]]])
edges.sort(axis=1)
order = np.lexsort((edges[:,1],edges[:,0]))
assert np.array_equal(edges[order[::2]],edges[order[1::2]])
face_ids = np.tile(np.arange(len(faces)),3)[order].reshape(-1,2)
adjacent_dot = np.einsum('ij,ij->i',geometric[face_ids[:,0]],geometric[face_ids[:,1]])
patch = (faces>=437645).any(axis=1)
report = dict(model_sha256=hashlib.sha256(raw).hexdigest(),triangles=len(faces),
              scope='Float64 calculations on the accepted serialized float32 mesh. Stored corner normals normalized before comparison; no ray traversal or image capture.',
              corner_normal_angle_degrees=percentiles(np.degrees(np.arccos(np.clip(corner_dot,-1,1)))),
              centroid_normal_angle_degrees=percentiles(np.degrees(np.arccos(np.clip(center_dot,-1,1)))),
              adjacent_face_angle_degrees=percentiles(np.degrees(np.arccos(np.clip(adjacent_dot,-1,1)))),
              corners_opposed_or_tangent_to_geometric=int(np.count_nonzero(corner_dot<=0)),
              faces_with_an_opposed_or_tangent_corner=int(np.count_nonzero((corner_dot<=0).any(axis=1))),
              centroid_normals_opposed_or_tangent=int(np.count_nonzero(center_dot<=0)),
              patch_triangles=int(patch.sum()))

# This is an unoccluded centroid thought experiment, not a visible-pixel audit.
center = triangles.mean(axis=1)
center[:,1] += float(np.float32(-np.float32(positions[:,1].min())+np.float32(.001)))
yaw,pitch,distance = .45,.35,1.75
eye = np.array([0,.32,0])+distance*np.array([np.cos(pitch)*np.sin(yaw),np.sin(pitch),np.cos(pitch)*np.cos(yaw)])
direction = center-eye
direction /= np.linalg.norm(direction,axis=1)[:,None]
smooth = np.where((center_dot<0)[:,None],-smooth,smooth)
ng_dot = np.einsum('ij,ij->i',direction,geometric)
ns_dot = np.einsum('ij,ij->i',direction,smooth)
front = ng_dot<0
admissible = front & (ns_dot<0)
reflected = direction-2*ns_dot[:,None]*smooth
eta = 1/1.5
cosine = -ns_dot
k = 1-eta*eta*(1-cosine*cosine)
transmitted = eta*direction+(eta*cosine-np.sqrt(np.maximum(k,0)))[:,None]*smooth
report['centroid_direction_probe'] = dict(
    scope='Unoccluded triangle centroids at the default camera, IOR 1.5 entering. Counts are not observed visible-path errors.',
    front_facing_centroids=int(front.sum()),
    incident_shading_hemisphere_disagreement=int(np.count_nonzero(front & (ns_dot>=0))),
    naive_reflections_on_wrong_geometric_side=int(np.count_nonzero(admissible & (np.einsum('ij,ij->i',reflected,geometric)<=0))),
    naive_transmissions_on_wrong_geometric_side=int(np.count_nonzero(admissible & (np.einsum('ij,ij->i',transmitted,geometric)>=0))))
(root/'normal-audit.json').write_text(json.dumps(report,indent=2)+'\n')

source = Path('outputs/raytracing/dragon-glass-first-pass-20261108/window-final/metrics.json')
metrics = json.loads(source.read_text())
journal = {f['frame']: f for f in metrics['sampling_frames'] if f['selected_material']=='glass'}
groups = {}
for name,predicate in [('reset',lambda f:f['new_samples']>0 and f['samples_before']==0),
                       ('accumulating',lambda f:f['new_samples']>0 and f['samples_before']>0),
                       ('settled',lambda f:f['new_samples']==0)]:
    ids = {frame for frame,f in journal.items() if predicate(f)}
    passes = {}
    for pass_name in sorted({p['pass'] for p in metrics['gpu_samples']}):
        values = [p['milliseconds'] for p in metrics['gpu_samples'] if p['frame'] in ids and p['pass']==pass_name]
        if values:
            passes[pass_name] = dict(count=len(values),median_ms=float(np.median(values)),p95_ms=float(np.percentile(values,95)))
    groups[name] = dict(journal_frames=len(ids),gpu_passes=passes)
timing = dict(source=str(source),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
              scope='Historical mixed native UI exercise: changing views, IOR, exposure and extents. Not a fresh steady-state benchmark or wall-clock/input-latency measurement. Reset includes non-motion edits. Memory values predate the accounting fix.',
              device=metrics['device'],sample_target=32,
              all_history_epochs=metrics['history_epochs'],
              maximum_new_samples_per_native_frame=max(f['new_samples'] for f in journal.values()),
              groups=groups)
(root/'historical-window-analysis.json').write_text(json.dumps(timing,indent=2)+'\n')
print(json.dumps(report,indent=2))
print(json.dumps(timing,indent=2))
