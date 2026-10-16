"""Historical import/command compatibility; current owner is Terrain Water."""
from pathlib import Path
import sys
import importlib
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
implementation = importlib.import_module("projects.terrain_water.run_terrain_rain_demo")
if __name__ == "__main__" and hasattr(implementation, "main"):
    implementation.main()
else:
    sys.modules[__name__] = implementation
