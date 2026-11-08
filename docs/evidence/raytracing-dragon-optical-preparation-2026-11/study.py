# /// script
# requires-python = "==3.11.*"
# dependencies = ["numpy==2.4.6", "cgal==5.6.post202402071826"]
# ///
"""Reproduce the rejected local-patch trial; never admit or replace a model.

Writes an opaque comparison GLB and measurements to a new output directory.
Distance samples are rejection witnesses, not a Hausdorff upper bound.
"""
import argparse
import hashlib
import importlib.metadata
import json
import math
from pathlib import Path
import struct

import numpy as np
from CGAL.CGAL_AABB_tree import AABB_tree_Polyhedron_3_Facet_handle as Tree
from CGAL.CGAL_Kernel import Point_3
from CGAL.CGAL_Polyhedron_3 import Polyhedron_3
import CGAL.CGAL_Polygon_mesh_processing as pmp

SOURCE_SHA = "3f9dc36d34ce148e92ac4d096339a5cc48a9c4a86126b9963b052676a8a2260e"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_glb(raw):
    magic, version, size = struct.unpack_from("<III", raw)
    assert (magic, version, size) == (0x46546C67, 2, len(raw))
    length, kind = struct.unpack_from("<II", raw, 12)
    assert kind == 0x4E4F534A
    doc = json.loads(raw[20:20 + length])
    length_b, kind_b = struct.unpack_from("<II", raw, 20 + length)
    assert kind_b == 0x004E4942
    binary = raw[28 + length:28 + length + length_b]

    def accessor(i):
        a = doc["accessors"][i]
        v = doc["bufferViews"][a["bufferView"]]
        assert "byteStride" not in v and "sparse" not in a
        width = {"VEC3": 3, "SCALAR": 1}[a["type"]]
        return np.frombuffer(binary, dtype={5126: "<f4", 5125: "<u4"}[a["componentType"]],
                             count=a["count"] * width,
                             offset=v.get("byteOffset", 0) + a.get("byteOffset", 0)).reshape(-1, width)

    primitive = doc["meshes"][0]["primitives"][0]
    return (doc, accessor(primitive["attributes"]["POSITION"]),
            accessor(primitive["attributes"]["NORMAL"]), accessor(primitive["indices"]).reshape(-1, 3))


def write_glb(doc, positions, normals, indices):
    doc = json.loads(json.dumps(doc))
    pieces = [positions.astype("<f4").tobytes(), normals.astype("<f4").tobytes(),
              indices.astype("<u4").tobytes()]
    offset = 0
    doc["bufferViews"] = []
    for piece in pieces:
        doc["bufferViews"].append({"buffer": 0, "byteOffset": offset, "byteLength": len(piece)})
        offset += len(piece)
    doc["buffers"] = [{"byteLength": offset}]
    doc["accessors"][2]["count"] = int(indices.size)
    doc["accessors"][0]["count"] = len(positions)
    doc["accessors"][1]["count"] = len(normals)
    doc["asset"]["generator"] = "Cubey REJECTED optical preparation trial; opaque comparison only"
    doc["extras"] = {"optical_admission": False, "purpose": "rejected local repair comparison"}
    j = json.dumps(doc, separators=(",", ":")).encode()
    j += b" " * (-len(j) % 4)
    b = b"".join(pieces)
    b += b"\0" * (-len(b) % 4)
    return (struct.pack("<III", 0x46546C67, 2, 28 + len(j) + len(b)) +
            struct.pack("<II", len(j), 0x4E4F534A) + j + struct.pack("<II", len(b), 0x004E4942) + b)


def canonical(positions, indices):
    unique, first, inverse = np.unique(positions.astype(float), axis=0, return_index=True, return_inverse=True)
    f = inverse[indices]
    cross = np.cross(unique[f[:, 1]] - unique[f[:, 0]], unique[f[:, 2]] - unique[f[:, 0]])
    nonzero = np.linalg.norm(cross, axis=1) > 0
    return unique, first, f[nonzero], nonzero


def write_off(path, points, faces):
    with path.open("w") as stream:
        stream.write(f"OFF\n{len(points)} {len(faces)} 0\n")
        np.savetxt(stream, points, fmt="%.17g")
        np.savetxt(stream, np.column_stack([np.full(len(faces), 3), faces]), fmt="%d")


def restore_source_vertex_ids(mesh, points):
    source_ids = {tuple(p): i for i, p in enumerate(points)}
    for v in mesh.vertices():
        p = v.point()
        v.set_id(source_ids[(p.x(), p.y(), p.z())])


def topology(points, faces):
    active, compact = np.unique(faces, return_inverse=True)
    f = compact.reshape(-1, 3)
    center = np.concatenate([f[:, i] for i in range(3)]).astype(np.uint64)
    link_from = np.concatenate([f[:, (i + 1) % 3] for i in range(3)]).astype(np.uint64)
    link_to = np.concatenate([f[:, (i + 2) % 3] for i in range(3)]).astype(np.uint64)
    keys = (center << np.uint64(32)) | link_from
    order = np.argsort(keys)
    keys = keys[order]
    assert np.all(keys[1:] != keys[:-1]), "duplicate directed edge"
    target = (center[order] << np.uint64(32)) | link_to[order]
    successor = np.searchsorted(keys, target)
    assert successor.max() < len(keys) and np.array_equal(keys[successor], target)
    assert len(np.unique(successor)) == len(successor)
    labels, jump = np.arange(len(keys)), successor
    degrees = np.bincount(center.astype(np.int64))
    for _ in range(math.ceil(math.log2(int(degrees.max()))) + 1):
        labels = np.minimum(labels, labels[jump])
        jump = jump[jump]
    cycles = np.unique((center[order] << np.uint64(32)) | labels.astype(np.uint64))
    link_cycles = np.bincount((cycles >> np.uint64(32)).astype(np.int64), minlength=len(active))
    assert np.all(link_cycles == 1), "non-disk vertex link"
    edges = np.concatenate([f[:, [i, (i + 1) % 3]] for i in range(3)])
    edge_keys = (edges.min(1).astype(np.uint64) << np.uint64(32)) | edges.max(1).astype(np.uint64)
    edge_order = np.argsort(edge_keys)
    _, counts = np.unique(edge_keys, return_counts=True)
    assert np.all(counts == 2)
    signs = np.where(edges[:, 0] < edges[:, 1], 1, -1)
    assert np.all(signs[edge_order].reshape(-1, 2).sum(1) == 0)
    parent, rank = list(range(len(active))), [0] * len(active)

    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    components = len(active)
    for a, b in edges[edge_order][::2]:
        a, b = root(int(a)), root(int(b))
        if a != b:
            if rank[a] < rank[b]:
                a, b = b, a
            parent[b] = a
            rank[a] += rank[a] == rank[b]
            components -= 1
    euler = len(active) - len(counts) + len(f)
    roots = np.array([root(i) for i in range(len(active))])
    component_records = []
    for label in np.unique(roots):
        member = roots[f[:, 0]] == label
        triangle_points = points[faces[member]]
        component_records.append(dict(triangles=int(member.sum()),
                                      signed_volume=float(np.einsum('ij,ij->i', triangle_points[:, 0],
                                          np.cross(triangle_points[:, 1], triangle_points[:, 2])).sum() / 6)))
    return dict(active_vertices=len(active), triangles=len(f), edges=len(counts),
                all_edges_two_faces=True, consistent_winding=True, non_disk_vertex_links=0,
                connected_components=components, euler_characteristic=euler,
                component_details=sorted(component_records, key=lambda c: -c['triangles']),
                genus=(2 - euler) // 2 if components == 1 else None)


def face_keys(faces):
    ids = np.sort(faces, axis=1).astype(np.uint64)
    assert ids.max() < 2**19
    return (ids[:, 0] << np.uint64(38)) | (ids[:, 1] << np.uint64(19)) | ids[:, 2]


def probe(points, faces, target, label):
    tree = Tree(target.facets())
    tree.accelerate_distance_queries()
    q = np.concatenate([points[np.unique(faces)], points[faces].mean(axis=1)])
    distances = np.array([float(tree.squared_distance(Point_3(*p)))**.5 for p in q])
    return dict(direction=label, changed_faces=len(faces), samples=len(q),
                maximum_sample_distance_metres=float(distances.max()),
                p95_sample_distance_metres=float(np.percentile(distances, 95)),
                worst_sample=q[distances.argmax()].tolist())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path("cache/raytracing/stanford-dragon/dragon.glb"))
    parser.add_argument("--output", type=Path, required=True, help="new, empty output directory")
    parser.add_argument("--keep-main-shell", action="store_true",
                        help="measured alternative: discard newly detached shells; flat new patch normals")
    args = parser.parse_args()
    raw = args.source.read_bytes()
    if sha(raw) != SOURCE_SHA:
        raise ValueError("pinned original GLB checksum mismatch")
    # Refuse reused paths, including symlinks and any source-cache ancestor.
    if args.output.exists() or args.output.is_symlink():
        raise ValueError("output must be a new directory")
    if args.source.resolve().is_relative_to(args.output.resolve()):
        raise ValueError("output cannot contain the original asset")
    args.output.mkdir(parents=True)
    doc, positions, normals, render_faces = read_glb(raw)
    points, first, faces, valid = canonical(positions, render_faces)
    assert (~valid).sum() == 108 and len(faces) == 871306
    source_topology = topology(points, faces)
    original_off = args.output / "original-boundary.off"
    write_off(original_off, points, faces)
    mesh = Polyhedron_3(str(original_off))
    for i, v in enumerate(mesh.vertices()):
        v.set_id(i)
    for i, f in enumerate(mesh.facets()):
        f.set_id(i)
    pairs = []
    pmp.self_intersections(mesh, pairs)
    bad = np.unique([i for pair in pairs for i in [pair.first.id(), pair.second.id()]])
    assert len(pairs) == 201 and len(bad) == 190
    selected = np.flatnonzero(np.isin(faces, np.unique(faces[bad])).any(axis=1))
    selected_set = set(selected.tolist())
    handles = [f for f in mesh.facets() if f.id() in selected_set]
    for f in handles:
        mesh.erase_facet(f.halfedge())
    assert mesh.is_valid()
    mesh.normalize_border()
    fills = []
    for h in list(mesh.border_halfedges()):
        if h.is_border():
            patch = []
            pmp.triangulate_hole(mesh, h, patch)
            assert patch, "hole triangulation failed"
            fills.append(len(patch))
    discarded_faces = 0
    if args.keep_main_shell:
        before = mesh.size_of_facets()
        pmp.keep_largest_connected_components(mesh, 1)
        pmp.remove_isolated_vertices(mesh)
        discarded_faces = before - mesh.size_of_facets()
        # Component processing uses the Polyhedron id fields as scratch indices.
        # Recover source identity from exact coordinates before extracting faces.
        restore_source_vertex_ids(mesh, points)
    result = []
    for f in mesh.facets():
        h = f.halfedge()
        tri = []
        for _ in range(3):
            tri.append(h.vertex().id())
            h = h.next()
        result.append(tri)
    result = np.array(result)
    old_keys, new_keys = face_keys(faces), face_keys(result)
    retained = np.isin(old_keys, new_keys)
    added = result[~np.isin(new_keys, old_keys)]
    # Preserve original triangle order, corner order and render attributes wherever retained.
    output_positions, output_normals = positions, normals
    patch_corners = first[added]
    if args.keep_main_shell:
        triangles = points[added]
        geometric = np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0])
        geometric /= np.linalg.norm(geometric, axis=1)[:, None]
        output_positions = np.concatenate([positions, triangles.reshape(-1, 3)]).astype("<f4")
        output_normals = np.concatenate([normals, np.repeat(geometric, 3, axis=0)]).astype("<f4")
        patch_corners = np.arange(len(positions), len(output_positions)).reshape(-1, 3)
    final_indices = np.concatenate([render_faces[valid][retained], patch_corners]).astype("<u4")
    encoded = write_glb(doc, output_positions, output_normals, final_indices)
    candidate_path = args.output / "dragon-patch-REJECTED.glb"
    candidate_path.write_bytes(encoded)
    _, final_positions, final_normals, final_render_faces = read_glb(candidate_path.read_bytes())
    assert np.array_equal(final_positions[:len(positions)], positions)
    assert np.array_equal(final_normals[:len(normals)], normals)
    q, _, g, final_valid = canonical(final_positions, final_render_faces)
    assert final_valid.all()
    topo = topology(q, g)
    active, inverse = np.unique(g, return_inverse=True)
    serialized_off = args.output / "serialized-boundary.off"
    write_off(serialized_off, q[active], inverse.reshape(-1, 3))
    final = Polyhedron_3(str(serialized_off))
    final_pairs = []
    pmp.self_intersections(final, final_pairs)
    source_mesh = Polyhedron_3(str(original_off))
    distances = [probe(points, faces[~retained], final, "original-to-candidate"),
                 probe(points, added, source_mesh, "candidate-to-original")]
    assert any(d["maximum_sample_distance_metres"] > .0001 for d in distances)
    assert final.is_valid() and final.is_closed() and final.is_pure_triangle()
    assert not final_pairs and pmp.is_outward_oriented(final) and pmp.volume(final) > 0
    if args.keep_main_shell:
        assert topo['connected_components'] == 1
    assert np.isfinite(final_positions).all() and np.isfinite(final_normals).all()
    assert np.all(np.abs(np.linalg.norm(final_normals.astype(float), axis=1) - 1) < 1e-6)
    # Record normal policy at new triangles as well as bit-identical raw normals.
    tri = points[added]
    geometric = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    geometric /= np.linalg.norm(geometric, axis=1)[:, None]
    shading = final_normals[patch_corners].astype(float).mean(axis=1)
    shading /= np.linalg.norm(shading, axis=1)[:, None]
    angles = np.degrees(np.arccos(np.clip((geometric * shading).sum(axis=1), -1, 1)))
    min_changed_y = float(points[faces[~retained], 1].min())
    contact_limit = float(positions[:, 1].min()) + .015
    record = dict(format="cubey.raytracing.optical-repair-study", version=1,
                  source_sha256=SOURCE_SHA, candidate_sha256=sha(encoded),
                  cgal=importlib.metadata.version("cgal"), numpy=np.__version__,
                  kernel="Exact_predicates_inexact_constructions_kernel (CGAL SWIG Polyhedron_3)",
                  source_intersection_pairs=len(pairs), affected_faces=len(bad),
                  source_topology=source_topology,
                  removed_nonzero_faces=int((~retained).sum()), added_faces=len(added),
                  keep_main_shell=args.keep_main_shell, discarded_detached_shell_triangles=discarded_faces,
                  removed_faces_outside_original_one_ring=int(np.count_nonzero(~retained & ~np.isin(np.arange(len(faces)), selected))),
                  removed_source_zero_area_faces=108, hole_fill_triangle_counts=fills,
                  operation="erase intersection faces plus one vertex ring; triangulate each boundary",
                  serialized_boundary={**topo, "self_intersection_pairs": len(final_pairs),
                                       "valid": True, "closed": True,
                                       "cgal_is_outward_oriented_predicate": True,
                                       "signed_volume": float(pmp.volume(final))},
                  original_vertex_attribute_prefix_bit_identical=True,
                  normal_policy=("original corners retained; new patch corners use flat geometric normals"
                                 if args.keep_main_shell else
                                 "original normals retained; new corners use first coincident source vertex"),
                  added_triangle_geometric_vs_shading_normal_degrees={str(p): float(np.percentile(angles, p)) for p in [0, 50, 95, 100]},
                  changed_face_minimum_y=min_changed_y, contact_band_maximum_y=contact_limit,
                  contact_band_faces_unchanged=min_changed_y > contact_limit,
                  surface_deviation_trial_budget_metres=.0001, distance_samples=distances,
                  distance_samples_are_not_a_hausdorff_upper_bound=True,
                  fidelity_gate_passed=False, optical_admission=False,
                  one_connected_embedded_boundary_checks_passed=topo['connected_components'] == 1,
                  rejection="sampled surface distance alone exceeds 0.1 mm trial budget",
                  additional_rejection_reasons=([] if args.keep_main_shell else
                                               ["one connected surface became three",
                                                "new patch geometric and stored shading normals can oppose"]),
                  original_asset_unchanged=sha(args.source.read_bytes()) == SOURCE_SHA)
    (args.output / "manifest.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record, indent=2))


if __name__ == "__main__":
    main()
