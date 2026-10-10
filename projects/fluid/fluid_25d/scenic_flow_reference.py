"""Explicit pre-promotion flow appearance for isolated renderer comparisons.

No solver settings or input files. Merge before each test/study's own controls
so an omitted override never silently follows a newly promoted Macro default.
"""

RETAINED_FLOW_CUES = {
    "water_rapid_strength": 0,
    "water_cascade_strength": 0,
    "water_landing_strength": 0,
    "water_whitewater_strength": 0,
    "water_whitewater_speed": 1,
    "water_stream_foam_strength": 0,
    "water_stream_foam_patchiness": 0,
    "water_stream_foam_brightness": 1,
}
