#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BAKE_SCRIPT="${ROOT_DIR}/projects/terrain/tools/terrain_diffusion_bake.py"
REFERENCE_OVERRIDE="${CUBEY_TERRAIN_DIFFUSION_ROOT:-}"
REFERENCE_ROOT="${REFERENCE_OVERRIDE:-${CUBEY_TERRAIN_DIFFUSION_FALLBACK_ROOT:-${ROOT_DIR}/cache/terrain/tooling/v1/terrain-diffusion-src}}"
ENV_DIR="${CUBEY_TERRAIN_DIFFUSION_ENV:-${ROOT_DIR}/cache/terrain/tooling/v1/terrain-diffusion-venv}"
EXPECTED_REVISION="82a0431281f21a6ec3d691a12ee61525de5b0790"
REFERENCE_URL="https://github.com/xandergos/terrain-diffusion.git"

export PYTHONDONTWRITEBYTECODE=1

force_regeneration=0
for argument in "$@"; do
  if [[ "${argument}" == "--force" ]]; then
    force_regeneration=1
    break
  fi
done
if [[ ${force_regeneration} -eq 0 ]] && python3 "${BAKE_SCRIPT}" "$@" --validate-only; then
  exit 0
fi

if [[ ! -d "${REFERENCE_ROOT}/.git" ]]; then
  if [[ -n "${REFERENCE_OVERRIDE}" ]]; then
    printf 'terrain diffusion bakeoff: reference checkout not found: %s\n' "${REFERENCE_ROOT}" >&2
    exit 2
  fi
  mkdir -p "$(dirname "${REFERENCE_ROOT}")"
  git clone "${REFERENCE_URL}" "${REFERENCE_ROOT}"
  git -C "${REFERENCE_ROOT}" checkout --detach "${EXPECTED_REVISION}"
fi
actual_revision="$(git -C "${REFERENCE_ROOT}" rev-parse HEAD)"
if [[ "${actual_revision}" != "${EXPECTED_REVISION}" ]]; then
  if [[ -n "${REFERENCE_OVERRIDE}" ]]; then
    printf 'terrain diffusion bakeoff: expected reference %s, got %s\n' \
      "${EXPECTED_REVISION}" "${actual_revision}" >&2
    exit 2
  fi
  git -C "${REFERENCE_ROOT}" fetch origin "${EXPECTED_REVISION}"
  git -C "${REFERENCE_ROOT}" checkout --detach "${EXPECTED_REVISION}"
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
  "${BAKE_SCRIPT}" \
  --reference-root "${REFERENCE_ROOT}" "$@"
