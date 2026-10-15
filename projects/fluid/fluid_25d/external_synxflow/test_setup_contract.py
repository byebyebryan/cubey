# SPDX-License-Identifier: GPL-3.0-only
"""Dependency handoff contracts; stdlib only, no downloads, installs or CUDA imports."""
import ast
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import check_runtime as runtime

HERE = Path(__file__).resolve().parent


def constant(path, name):
    tree = ast.parse(path.read_text())
    return next(ast.literal_eval(node.value) for node in tree.body
                if isinstance(node, ast.Assign)
                and any(isinstance(target, ast.Name) and target.id == name
                        for target in node.targets))


class SetupContract(unittest.TestCase):
    def test_profile_matches_enforced_build_and_launcher_pins(self):
        profile = json.loads(runtime.PROFILE.read_text())
        self.assertEqual(profile['schema'], 'cubey.fluid25d.synxflow-runtime-profile.v1')
        build = HERE / 'build_synxflow_service_v1.py'
        self.assertEqual(profile['upstream']['commit'], constant(build, 'UPSTREAM_COMMIT'))
        self.assertEqual(profile['upstream']['archive_sha256'], constant(build, 'ARCHIVE_SHA256'))
        self.assertEqual(profile['qualified_service']['extension_sha256'],
                         constant(HERE.parent / 'run_mountain_rain_demo.py', 'EXTENSION_SHA'))
        self.assertEqual(profile['python_environment']['lock'], runtime.LOCK.name)

    def test_lock_retains_compatibility_versions_and_wheel_identity(self):
        text = runtime.LOCK.read_text()
        packages = runtime.locked_packages(text)
        self.assertEqual(packages['numpy'], '1.26.4')
        self.assertEqual(packages['pandas'], '2.2.3')
        self.assertEqual(packages['setuptools'], '80.9.0')
        self.assertEqual(packages['synxflow'], '1.0.1')
        profile = json.loads(runtime.PROFILE.read_text())
        self.assertIn('synxflow==1.0.1 --hash=sha256:' +
                      profile['python_environment']['synxflow_wheel_sha256'], text)

    def test_handoff_has_upstream_credit_and_no_automatic_candidate_admission(self):
        text = (HERE / 'README.md').read_text()
        profile = json.loads(runtime.PROFILE.read_text())
        self.assertIn(profile['upstream']['repository'], text)
        self.assertIn(profile['upstream']['paper'], text)
        self.assertIn('Build success is not admission', text)
        self.assertIn('no self-service arbitrary-build admission switch', text)
        self.assertIn('## Without SynxFlow', text)

    def test_unbounded_unhashed_duplicate_and_empty_lock_rejected(self):
        row = 'numpy==1.26.4 --hash=sha256:' + '0' * 64
        for text in ('', 'numpy', 'numpy>=1', 'numpy==1.26.4', row + '\n' + row):
            with self.subTest(text=text), self.assertRaises(ValueError):
                runtime.locked_packages(text)

    def test_bad_hash_rejected_before_optional_imports(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'flood.so'
            path.write_bytes(b'unqualified')
            with patch.object(runtime, 'check_environment') as environment, \
                    patch.object(runtime.importlib, 'import_module') as imports:
                with self.assertRaisesRegex(ValueError, 'unqualified extension hash'):
                    runtime.check(path)
                environment.assert_not_called()
                imports.assert_not_called()

    def test_symlink_rejected_before_hash_or_import(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'source.so'
            source.write_bytes(b'native')
            link = Path(directory) / 'link.so'
            link.symlink_to(source)
            with self.assertRaisesRegex(ValueError, 'non-symlink'):
                runtime.check(link)

    def test_environment_rejects_version_drift(self):
        with patch.object(runtime.platform, 'python_implementation', return_value='CPython'), \
                patch.object(runtime.sys, 'version_info', (3, 11, 17)), \
                patch.object(runtime.platform, 'system', return_value='Linux'), \
                patch.object(runtime.platform, 'machine', return_value='x86_64'), \
                patch.object(runtime.platform, 'libc_ver', return_value=('glibc', '2.28')), \
                patch.object(runtime.importlib.metadata, 'version', return_value='2.0'):
            with self.assertRaisesRegex(ValueError, 'installed version differs'):
                runtime.check_environment({'numpy': '1.26.4'})

    def test_no_native_execution_or_installation_in_checker(self):
        tree = ast.parse((HERE / 'check_runtime.py').read_text())
        for node in ast.walk(tree):
            if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute):
                self.assertNotIn(node.func.attr, ('run', 'run_service', 'Popen', 'check_call'))


if __name__ == '__main__':
    unittest.main()
