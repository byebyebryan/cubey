"""Asset-independent acceptance policies for the opt-in dragon glass checks."""

STATUS_NAMES = {
    1: 'total-query limit', 2: 'event limit', 4: 'radiance-query limit',
    8: 'zero throughput', 16: 'invalid medium', 32: 'shadow limit', 64: 'path stack',
}
REQUESTED_MEMORY_FIELDS = (
    'geometry_bytes', 'as_bytes', 'sbt_bytes', 'uniform_bytes', 'material_bytes',
    'environment_uniform_bytes', 'target_bytes', 'query_bytes',
    'workshop_history_bytes', 'workshop_recipe_bytes', 'workshop_uniform_bytes',
)


def require_transport_status(name, counts, *, allow_truncation=False):
    # With the preset's raw cutoff fixed at zero, bit 8 denotes a zero-weight
    # branch, not discarded positive throughput. Event/shadow exhaustion is
    # allowed only in explicitly marked shallow diagnostic controls.
    allowed = {8, 2, 32} if allow_truncation else {8}
    failures = {STATUS_NAMES[bit]: counts[str(bit)] for bit in STATUS_NAMES
                if bit not in allowed and counts[str(bit)]}
    if failures:
        raise AssertionError(f'{name}: forbidden transport status {failures}')


def require_requested_memory(metrics):
    # geometry_bytes already includes edge_adjacency_bytes; don't count it twice.
    expected = sum(metrics[field] for field in REQUESTED_MEMORY_FIELDS)
    actual = metrics['resident_project_requested_bytes']
    if actual != expected:
        raise AssertionError(f'resident requested bytes: {actual}, component sum: {expected}')
    if metrics['peak_simultaneous_project_requested_bytes'] < actual:
        raise AssertionError('peak requested bytes is smaller than resident requested bytes')
    return expected
