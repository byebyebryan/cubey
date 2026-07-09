#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/atmosphere/atmosphere}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/atmosphere-milky-way-layers-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-2}"
VARIATION="${VARIATION:-0.0}"
FORMULA="${FORMULA:-v1}"

LAYERS=(
    final
    stellar-emission
    dust-tau
    star-clouds
    hii-emission
    speckles
)

mkdir -p "${OUT_DIR}"

if [[ ! -x "${APP}" ]]; then
    printf 'missing atmosphere app: %s\n' "${APP}" >&2
    exit 1
fi

MANIFEST="${OUT_DIR}/manifest.tsv"
INDEX="${OUT_DIR}/index.md"
CAPTURE_FILES=()
CAPTURE_LABELS=()

{
    printf 'file\tlayer\targs\n'
} >"${MANIFEST}"

{
    printf '# Milky Way Layer Captures\n\n'
    printf -- '- Size: %sx%s\n' "${WIDTH}" "${HEIGHT}"
    printf -- '- Frames: %s\n' "${FRAMES}"
    printf -- '- Procedural variation: %s\n\n' "${VARIATION}"
    printf -- '- Formula: %s\n\n' "${FORMULA}"
    printf 'This pack captures the generated night-sky atlas through the atmosphere '
    printf '`milky-way` debug view. It is intended as a fast layer-composition '
    printf 'baseline before changing the procedural Milky Way recipe.\n\n'
    printf '| Capture | Layer | Args |\n'
    printf '|---|---|---|\n'
} >"${INDEX}"

for layer in "${LAYERS[@]}"; do
    rel_file="${layer}.png"
    args=(
        --headless
        --frames "${FRAMES}"
        --width "${WIDTH}"
        --height "${HEIGHT}"
        --debug-view milky-way
        --milky-way-layer "${layer}"
        --milky-way-formula "${FORMULA}"
        --milky-way-variation "${VARIATION}"
        --output "${OUT_DIR}/${rel_file}"
    )
    "${APP}" "${args[@]}"
    printf '%s\t%s\t%s\n' "${rel_file}" "${layer}" "${args[*]}" >>"${MANIFEST}"
    printf '| [%s](%s) | %s | `%s` |\n' "${layer}" "${rel_file}" "${layer}" \
        "${args[*]}" >>"${INDEX}"
    CAPTURE_FILES+=("${OUT_DIR}/${rel_file}")
    CAPTURE_LABELS+=("${layer}")
done

if command -v magick >/dev/null 2>&1; then
    rm -f "${OUT_DIR}/contact-sheet.png"
    montage_inputs=()
    for index in "${!CAPTURE_FILES[@]}"; do
        montage_inputs+=("-label" "${CAPTURE_LABELS[${index}]}" "${CAPTURE_FILES[${index}]}")
    done
    magick montage "${montage_inputs[@]}" -geometry 360x203+8+26 -tile 3x \
        "${OUT_DIR}/contact-sheet.png"
fi

printf 'milky way layer captures written to %s\n' "${OUT_DIR}"
