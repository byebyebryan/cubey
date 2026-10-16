"""Compatibility import for the project-owned Terrain Water supervisor."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from projects.terrain_water import terrain_demo_session as _implementation
sys.modules[__name__] = _implementation
