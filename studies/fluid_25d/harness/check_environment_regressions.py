"""Run the shared environment's named, headless-only regression set serially."""
import argparse
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

from report_patch_only_v1 import REQUIRED_TESTS

TESTS = REQUIRED_TESTS | {
    "terrain_surface_environment_cpu", "cubey_core_tests",
    "fluid_25d_environment_review_helpers", "atmosphere_config_tests",
    "ocean_config_tests", "water_3d_config_tests",
    "atmosphere_headless_writes_png", "atmosphere_night_writes_png",
    "water_3d_cloud_headless_writes_png", "ocean_cached_cloud_environment_writes_png",
}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out",type=Path)
    args=parser.parse_args()
    output=args.out.resolve()
    if (output/"regression-tests.xml").exists():
        raise ValueError("retain previous evidence; use a fresh output")
    # Deliberately no reaches_glfw or windowed smoke tests.
    command=["rtk","proxy","ctest","--test-dir","build/release","-j1",
        "--output-on-failure","--output-junit",str(output/"regression-tests.xml"),
        "-R","^("+"|".join(sorted(TESTS))+")$"]
    with (output/"regression-tests.log").open("x") as log:
        subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
    tests=ET.parse(output/"regression-tests.xml").getroot().findall(".//testcase")
    if {t.get("name") for t in tests}!=TESTS or any(
        t.find(tag) is not None for t in tests for tag in ("error","failure","skipped")):
        raise ValueError("missing, failed or skipped named regression")
    print("PASS",len(tests),"named regression groups; no skips")


if __name__ == "__main__":
    main()
