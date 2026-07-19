#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
REFERENCE_ROOT="${CUBEY_TERRAIN_DIFFUSION_ROOT:-${HOME}/code/ref/terrain-diffusion}"
ENV_DIR="${CUBEY_TERRAIN_DIFFUSION_ENV:-${ROOT_DIR}/outputs/terrain/.terrain-diffusion-venv}"
EXPECTED_REVISION="82a0431281f21a6ec3d691a12ee61525de5b0790"

if [[ ! -d "${REFERENCE_ROOT}/.git" ]]; then
  printf 'terrain diffusion bakeoff: reference checkout not found: %s\n' "${REFERENCE_ROOT}" >&2
  exit 2
fi
actual_revision="$(git -C "${REFERENCE_ROOT}" rev-parse HEAD)"
if [[ "${actual_revision}" != "${EXPECTED_REVISION}" ]]; then
  printf 'terrain diffusion bakeoff: expected reference %s, got %s\n' \
    "${EXPECTED_REVISION}" "${actual_revision}" >&2
  exit 2
fi
if ! command -v uv >/dev/null 2>&1; then
  printf 'terrain diffusion bakeoff: uv is required\n' >&2
  exit 2
fi

uv python install 3.12
if [[ ! -x "${ENV_DIR}/bin/python" ]]; then
  uv venv --python 3.12 "${ENV_DIR}"
fi
uv pip install --python "${ENV_DIR}/bin/python" -r "${REFERENCE_ROOT}/requirements.txt"

export PYTHONPATH="${REFERENCE_ROOT}${PYTHONPATH:+:${PYTHONPATH}}"
if [[ "${1:-}" == "--test" ]]; then
  shift
  exec "${ENV_DIR}/bin/python" -m unittest \
    "${ROOT_DIR}/projects/terrain/tools/test_terrain_diffusion_bake.py" "$@"
fi

exec "${ENV_DIR}/bin/python" \
  "${ROOT_DIR}/projects/terrain/tools/terrain_diffusion_bake.py" \
  --reference-root "${REFERENCE_ROOT}" "$@"
