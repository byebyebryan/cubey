#!/usr/bin/env python3
"""Build the pinned optional GPL solver in a fresh private directory.

No installation or native execution. No CUDA library enters Cubey. Requires
an explicitly supplied, already downloaded toolkit and CPython 3.11 executable.
Compatibility edits are confined to upstream CMake files, not numerical code.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile


SDIST_SHA256 = "dd99a5b424fd4cad0a38f5e91d6b4ac09db4ae8dbf8d8c2fb4af669a105bd281"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replace_exact(path: Path, old: str, new: str, expected_count: int = 1) -> None:
    value = path.read_text()
    if value.count(old) != expected_count:
        raise ValueError(f"unexpected pinned source spelling in {path}")
    path.write_text(value.replace(old, new))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdist", type=Path, required=True)
    parser.add_argument("--cuda-root", type=Path, required=True)
    parser.add_argument("--python", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--toolchain", choices=("legacy11", "blackwell13"), default="legacy11",
                        help="legacy11 retains stock sm50/C++11; blackwell13 is diagnostic")
    parser.add_argument("--cc", type=Path)
    parser.add_argument("--cxx", type=Path)
    args = parser.parse_args()
    source_archive = args.sdist.resolve(strict=True)
    if sha256(source_archive) != SDIST_SHA256:
        raise ValueError("source archive does not match the pinned SynxFlow1.0.1 sdist")
    cuda = args.cuda_root.resolve(strict=True)
    python = args.python.absolute()
    if not 1 <= args.jobs <= 32 or not (cuda / "bin/nvcc").is_file():
        raise ValueError("invalid build jobs or missing explicitly supplied nvcc")
    python_info = json.loads(subprocess.check_output(
        [str(python), "-c", "import json,sys,sysconfig; print(json.dumps({'version':list(sys.version_info[:2]),'include':sysconfig.get_path('include'),'libdir':sysconfig.get_config_var('LIBDIR'),'ldlibrary':sysconfig.get_config_var('LDLIBRARY')}))"], text=True))
    if python_info["version"] != [3, 11]:
        raise ValueError("this pinned build requires CPython3.11")
    out = args.out.absolute()
    if out.exists() or out.is_symlink() or not out.parent.is_dir():
        raise ValueError("output must be a fresh leaf below an existing directory")
    out = out.parent.resolve() / out.name
    out.mkdir()
    with tarfile.open(source_archive) as archive:
        # The immutable hash is checked first; still reject link/path members.
        for member in archive.getmembers():
            path = Path(member.name)
            if path.is_absolute() or ".." in path.parts or member.issym() or member.islnk():
                raise ValueError("unsafe source archive member")
        archive.extractall(out)
    source = out / "synxflow-1.0.1"
    before = {str(path.relative_to(source)): sha256(path) for path in source.rglob("*") if path.is_file()}
    if args.toolchain == "blackwell13":
        replace_exact(source / "CMakeLists.txt", "set(CMAKE_CXX_STANDARD 11)", "set(CMAKE_CXX_STANDARD 17)")
        cmake = source / "synxflow/CMakeLists.txt"
        replace_exact(cmake, "find_package(CUDA)", 'find_package(CUDA)\ninclude_directories("${CUDA_TOOLKIT_ROOT_DIR}/include/cccl")')
        replace_exact(cmake, "-std=c++11", "-std=c++17")
        replace_exact(cmake, "-arch=sm_50;--expt-extended-lambda", "-arch=sm_120;--expt-extended-lambda;-std=c++17", 2)
    changed = [name for name, checksum in before.items() if sha256(source / name) != checksum]
    expected_changes = ["CMakeLists.txt", "synxflow/CMakeLists.txt"] if args.toolchain == "blackwell13" else []
    if sorted(changed) != expected_changes:
        raise ValueError("unexpected changes outside build compatibility files")
    build = out / "build"
    configure = ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_POLICY_DEFAULT_CMP0146=OLD",
                 f"-DCUDA_TOOLKIT_ROOT_DIR={cuda}", f"-DPYTHON_EXECUTABLE={python}",
                 f"-DPYTHON_INCLUDE_DIR={python_info['include']}",
                 f"-DPYTHON_LIBRARY={Path(python_info['libdir']) / python_info['ldlibrary']}"]
    if args.cc:
        configure.append(f"-DCMAKE_C_COMPILER={args.cc.resolve(strict=True)}")
    if args.cxx:
        compiler = args.cxx.resolve(strict=True)
        configure.extend([f"-DCMAKE_CXX_COMPILER={compiler}", f"-DCUDA_HOST_COMPILER={compiler}"])
    compile_command = ["cmake", "--build", str(build), "--target", "flood", "-j", str(args.jobs)]
    record = {"schema": "cubey.fluid25d.synxflow-source-build.v1", "sdist_sha256": SDIST_SHA256,
              "cuda_root": str(cuda), "cuda_version": subprocess.check_output([str(cuda / "bin/nvcc"), "--version"], text=True),
              "toolchain": args.toolchain,
              "python": str(python), "build_compatibility_files": changed,
              "numerical_sources_unchanged": True, "configure": configure, "build": compile_command,
              "passed": False}
    try:
        with (out / "configure.log").open("wb") as log:
            subprocess.run(configure, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
        with (out / "build.log").open("wb") as log:
            subprocess.run(compile_command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=600)
        extension = build / "synxflow/apps/cudaFloodSolversPybind/flood.cpython-311-x86_64-linux-gnu.so"
        record.update(passed=True, extension=str(extension), extension_sha256=sha256(extension))
    finally:
        (out / "build-result.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
