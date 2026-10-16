#!/usr/bin/env python3
"""Compatibility entry; canonical managed demo is projects/terrain_water/."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from projects.terrain_water import run_terrain_rain_demo as _implementation

if __name__ == "__main__":
    raise SystemExit(_implementation.main())
# Preserve module patching/import behavior for existing study/test consumers.
sys.modules[__name__] = _implementation
