"""Prepare only the intended assets for the qualified Q0.1 HTML report."""

import argparse
import html
import json
import shutil
from pathlib import Path


def read(path):
    return json.loads(Path(path).read_text())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    evidence = Path(__file__).parent
    completion = evidence / "completion"
    qualification = read(completion / "qualification.json")
    assert qualification["passed"]
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / "images").mkdir()
    shutil.copy2(
        args.input.parent / "diagnostic-controls/baseline-canonical-4096-s19.png",
        args.output / "images/baseline-4096-s19.png",
    )
    shutil.copy2(
        args.input / "qualification15/canonical-4096-s19.png",
        args.output / "images/corrected-4096-s19.png",
    )
    rows = []
    for capture in qualification["captures"]:
        command = capture["identity"]["command"]
        name = Path(command[command.index("--output") + 1]).stem
        forbidden = sum(
            count for bit, count in capture["status_or_pixels"].items() if bit != "8"
        )
        assert forbidden == 0 and capture["transport_accepted"]
        rows.append(
            f"<tr><td>{html.escape(name)}</td><td>{capture['samples']:,}</td>"
            f'<td>{capture["seed"]}</td><td>0</td><td class="pass">Pass</td></tr>'
        )
    template = (evidence / "report-template.html").read_text()
    assert template.count("__QUALIFICATION_ROWS__") == 1
    (args.output / "index.html").write_text(
        template.replace("__QUALIFICATION_ROWS__", "".join(rows))
    )
    for source, target in (
        ("qualification.json", "qualification.json"),
        ("fixture-and-fidelity.json", "fixtures.json"),
        ("performance.json", "performance.json"),
    ):
        shutil.copy2(completion / source, args.output / target)
    validation = {
        name.removesuffix(".json"): read(completion / name)
        for name in (
            "runtime-audit.json",
            "normal-build-identity.json",
            "checks.json",
            "preservation.json",
            "optical.json",
            "opaque.json",
            "native.json",
        )
    }
    validation["replay_fidelity"] = read(evidence / "fidelity.json")
    validation["optional_passive_probe_stopped"] = read(
        args.input / "corrected-passive-stopped.json"
    )
    (args.output / "validation.json").write_text(
        json.dumps(validation, indent=2) + "\n"
    )
    print(args.output.resolve())


if __name__ == "__main__":
    main()
