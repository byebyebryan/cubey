"""Independent controls for the offline study's volume and topology checks."""
from pathlib import Path
import runpy
import tempfile
import unittest

import numpy as np

study = runpy.run_path(str(Path(__file__).with_name("study.py")))
tetra_points = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]], dtype=float)
tetra_faces = np.array([[0, 2, 1], [0, 1, 3], [0, 3, 2], [1, 2, 3]])


class BoundaryControls(unittest.TestCase):
    def test_closed_tetrahedron(self):
        result = study["topology"](tetra_points, tetra_faces)
        self.assertEqual(result["connected_components"], 1)
        self.assertEqual(result["genus"], 0)
        self.assertAlmostEqual(result["component_details"][0]["signed_volume"], 1 / 6)

    def test_closed_components_are_counted_separately(self):
        points = np.concatenate([tetra_points, tetra_points + 2])
        faces = np.concatenate([tetra_faces, tetra_faces + 4])
        result = study["topology"](points, faces)
        self.assertEqual(result["connected_components"], 2)
        self.assertEqual(len(result["component_details"]), 2)

    def test_open_boundary_is_rejected(self):
        with self.assertRaises(AssertionError):
            study["topology"](tetra_points, tetra_faces[:-1])

    def test_reversed_face_is_rejected(self):
        faces = tetra_faces.copy()
        faces[0] = faces[0, ::-1]
        with self.assertRaises(AssertionError):
            study["topology"](tetra_points, faces)

    def test_pinched_vertex_is_rejected_despite_closed_edges(self):
        points = np.concatenate([tetra_points, -tetra_points[1:]])
        second = np.array([0, 4, 5, 6])[tetra_faces]
        with self.assertRaisesRegex(AssertionError, "non-disk"):
            study["topology"](points, np.concatenate([tetra_faces, second]))

    def test_float32_collapse_is_seen_after_serialization(self):
        positions = np.array([[1, 0, 0], [1 + 1e-9, 0, 0], [1, 1, 0]], dtype="<f4")
        _, _, faces, valid = study["canonical"](positions, np.array([[0, 1, 2]]))
        self.assertFalse(valid.any())
        self.assertEqual(len(faces), 0)

    def test_exact_predicate_positive_and_negative_controls(self):
        with tempfile.TemporaryDirectory() as directory:
            for crossing in [False, True]:
                points, faces = tetra_points, tetra_faces
                if crossing:
                    points = np.concatenate([points, points + .2])
                    faces = np.concatenate([faces, faces + 4])
                path = Path(directory) / "control.off"
                study["write_off"](path, points, faces)
                mesh = study["Polyhedron_3"](str(path))
                self.assertTrue(mesh.is_valid() and mesh.is_closed())
                self.assertEqual(study["pmp"].does_self_intersect(mesh), crossing)

    def test_source_identity_survives_component_filter(self):
        with tempfile.TemporaryDirectory() as directory:
            points = np.concatenate([tetra_points, tetra_points + 2])
            path = Path(directory) / 'two.off'
            study['write_off'](path, points, np.concatenate([tetra_faces, tetra_faces + 4]))
            mesh = study['Polyhedron_3'](str(path))
            for v in mesh.vertices():
                v.set_id(999)
            study['pmp'].keep_largest_connected_components(mesh, 1)
            study['restore_source_vertex_ids'](mesh, points)
            self.assertEqual(mesh.size_of_facets(), 4)
            for v in mesh.vertices():
                p = v.point()
                self.assertEqual(tuple(points[v.id()]), (p.x(), p.y(), p.z()))


if __name__ == "__main__":
    unittest.main()
