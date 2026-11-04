"""Current paths for explicitly invoked SDF study/preparation tools (stdlib).

Native startup never imports this module. Historical capture commands/receipts
keep their old identities; current tools resolve those content paths here.
"""
from functools import lru_cache
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


@lru_cache(maxsize=1)
def _relocations():
    return json.loads((ROOT / "studies/relocations.json").read_text())


def resolve_sdf_path(value):
    """Return a canonical path, retaining the caller's absolute/relative form."""
    path = Path(value)
    absolute = path.is_absolute()
    if absolute:
        try:
            relative = path.relative_to(ROOT)
        except ValueError:
            return path
    else:
        relative = path
    current = _relocations().get(relative.as_posix())
    if current is None:
        # Generators may produce a new Guardian audition not in the old roster.
        parts = relative.parts
        if (len(parts) >= 4 and parts[:2] == ("projects", "sdf")
                and parts[2] in {"scenes", "presets"}
                and "guardian" in path.name.lower()):
            current = str(Path("studies/sdf/guardian") / Path(*parts[2:]))
    result = Path(current) if current else relative
    return ROOT / result if absolute else result
