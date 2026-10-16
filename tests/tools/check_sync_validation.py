"""Prove synchronization validation by toggling detection of one deliberate hazard."""
import os
import re
import subprocess
import sys

for enabled in (False, True):
    environment = dict(os.environ, VK_LAYER_VALIDATE_SYNC="1" if enabled else "0")
    result = subprocess.run([sys.argv[1], "--probe"], capture_output=True, text=True,
                            env=environment, timeout=25)
    errors = re.findall(r"^vulkan validation error: (.*)$", result.stderr, re.MULTILINE)
    expected = (len(errors) == 1 and ("SYNC-HAZARD-WRITE-AFTER-WRITE" in errors[0]
                or "vkCmdFillBuffer(): WRITE_AFTER_WRITE hazard detected" in errors[0])) if enabled else not errors
    if result.returncode or "SYNC_PROBE_RECORDED" not in result.stdout or not expected:
        print(result.stdout + result.stderr)
        raise SystemExit(f"Synchronization validation probe failed with validate_sync={int(enabled)}")
print("PASS: missing-barrier WAW hazard detected only with synchronization validation enabled")
