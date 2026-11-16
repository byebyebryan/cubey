"""Build a private RT executable/shader set using the configured dev toolchain.

Reuses already-built foundation libraries, never overwrites normal objects/SPIR-V.
Source snapshots and live project sources are both supported explicitly.
"""

import argparse
import hashlib
import json
import shlex
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path("projects/raytracing"))
    parser.add_argument("--build", type=Path, default=Path("build/dev"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--journal", action="store_true")
    args = parser.parse_args()
    project = Path("projects/raytracing").resolve()
    source, build, output = (
        args.source.resolve(),
        args.build.resolve(),
        args.output.resolve(),
    )
    output.mkdir(parents=True, exist_ok=False)
    (output / "objects").mkdir()
    (output / "shaders").mkdir()
    commands = json.loads((build / "compile_commands.json").read_text())
    entries = [e for e in commands if "cubey_project_raytracing.dir/" in e["command"]]
    assert entries, "configure/build the normal dev RT target first"
    records = []

    def private_path(path):
        path = Path(path)
        return (
            source / path.relative_to(project) if path.is_relative_to(project) else path
        )

    def run(command, label):
        result = subprocess.run(
            command, cwd=build, capture_output=True, text=True, check=False
        )
        (output / (label + ".log")).write_text(result.stdout + result.stderr)
        assert result.returncode == 0, (label, result.stdout + result.stderr)
        return command

    object_map = {}
    compile_calls = []
    for entry in entries:
        command = shlex.split(entry["command"])
        old_object = command[command.index("-o") + 1]
        obj = output / "objects" / (Path(entry["file"]).name + ".o")
        object_map[old_object] = str(obj)
        command[command.index("-o") + 1] = str(obj)
        for flag, value in [("-MF", str(obj) + ".d"), ("-MT", str(obj))]:
            if flag in command:
                command[command.index(flag) + 1] = value
        command = [str(private_path(p)) if p == entry["file"] else p for p in command]
        command = [
            '-DCUBEY_RAYTRACING_SHADER_DIR="' + str(output / "shaders") + '"'
            if p.startswith("-DCUBEY_RAYTRACING_SHADER_DIR=")
            else p
            for p in command
        ]
        if args.journal:
            command.insert(1, "-DCUBEY_RT_RAY_JOURNAL=1")
        compile_calls.append((command, "compile-" + Path(entry["file"]).stem))
    with ThreadPoolExecutor(max_workers=8) as executor:
        records.extend(executor.map(lambda call: run(*call), compile_calls))

    generated = subprocess.check_output(
        [
            "ninja",
            "-C",
            str(build),
            "-t",
            "commands",
            "cubey_project_raytracing_shaders",
        ],
        text=True,
    ).splitlines()
    for line in generated:
        tokens = shlex.split(line)
        indexes = [i for i, p in enumerate(tokens) if p.endswith("/glslangValidator")]
        if not indexes:
            continue
        command = tokens[indexes[0] :]
        if "&&" in command:
            command = command[: command.index("&&")]
        target = output / "shaders" / Path(command[command.index("-o") + 1]).name
        command[command.index("-o") + 1] = str(target)
        if "--depfile" in command:
            command[command.index("--depfile") + 1] = str(target) + ".d"
        command = [
            "-I" + str(private_path(p[2:]))
            if p.startswith("-I")
            else str(private_path(p))
            if Path(p).is_absolute()
            else p
            for p in command
        ]
        if args.journal:
            command.insert(1, "-DCUBEY_RT_RAY_JOURNAL=1")
        records.append(run(command, "shader-" + target.name))

    lines = subprocess.check_output(
        ["ninja", "-C", str(build), "-t", "commands", "cubey_project_raytracing"],
        text=True,
    ).splitlines()
    tokens = shlex.split(lines[-1])
    start = next(i for i, token in enumerate(tokens) if token.endswith("/clang++"))
    command = tokens[start:]
    if "&&" in command:
        command = command[: command.index("&&")]
    # Do not let the private link overwrite the normal target's link depfile.
    for index in reversed(range(1, len(command))):
        if command[index].startswith("--dependency-file="):
            assert command[index - 1] == "-Xlinker"
            del command[index - 1 : index + 1]
    command = [object_map.get(p, p) for p in command]
    command[command.index("-o") + 1] = str(output / "raytracing")
    records.append(run(command, "link"))
    libraries = {
        str((build / p).resolve()): digest(build / p)
        for p in command
        if p.endswith(".a")
    }
    manifest = {
        "source": str(source),
        "journal": args.journal,
        "commands": records,
        "binary_sha256": digest(output / "raytracing"),
        "libraries": libraries,
        "shader_sha256": {
            p.name: digest(p) for p in (output / "shaders").glob("*.spv")
        },
        "source_sha256": {
            str(p.relative_to(source)): digest(p)
            for p in source.rglob("*")
            if p.is_file() and "__pycache__" not in str(p)
        },
    }
    (output / "build.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Private RT build:", output)


if __name__ == "__main__":
    main()
