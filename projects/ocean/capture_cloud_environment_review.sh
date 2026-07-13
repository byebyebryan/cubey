#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/ocean/ocean}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/ocean-cloud-reflection-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMES="${FRAMES:-18}"
MAP_SIZE="${MAP_SIZE:-512}"
PROBE_EXTENT="${PROBE_EXTENT:-64}"
PLANAR_SCALE="${PLANAR_SCALE:-0.5}"
PLANAR_STEPS="${PLANAR_STEPS:-32}"
PLANAR_GUARD="${PLANAR_GUARD:-0.15}"

sources=(current-view cached hybrid planar)
cameras=(mid high)
scenarios=(noon sunset night)
scenario_times=(12.0 17.8 23.0)

mkdir -p "${OUT_DIR}"
printf 'file\ttitle\tsource\tcamera\ttime\tdebug_view\n' >"${OUT_DIR}/manifest.tsv"

capture() {
    local name="$1"
    local title="$2"
    local source="$3"
    local camera="$4"
    local time="$5"
    local debug_view="${6:-final}"
    "${APP}" \
        --headless \
        --frames "${FRAMES}" \
        --width "${WIDTH}" \
        --height "${HEIGHT}" \
        --ocean-map-size "${MAP_SIZE}" \
        --ocean-camera-preset "${camera}" \
        --time-of-day-mode solar \
        --time-hours "${time}" \
        --day-of-year 172 \
        --latitude-degrees 30 \
        --ocean-cloud-reflection-source "${source}" \
        --ocean-cloud-environment-extent "${PROBE_EXTENT}" \
        --ocean-cloud-environment-update-hz 4 \
        --ocean-cloud-planar-resolution-scale "${PLANAR_SCALE}" \
        --ocean-cloud-planar-view-steps "${PLANAR_STEPS}" \
        --ocean-cloud-planar-guard-band "${PLANAR_GUARD}" \
        --debug-view "${debug_view}" \
        --output "${OUT_DIR}/${name}.png"
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "${name}.png" "${title}" "${source}" \
        "${camera}" "${time}" "${debug_view}" >>"${OUT_DIR}/manifest.tsv"
}

for camera in "${cameras[@]}"; do
    montage_inputs=()
    for scenario_index in "${!scenarios[@]}"; do
        scenario="${scenarios[${scenario_index}]}"
        time="${scenario_times[${scenario_index}]}"
        for source in "${sources[@]}"; do
            name="${camera}-${scenario}-${source}"
            title="${camera} ${scenario} ${source}"
            capture "${name}" "${title}" "${source}" "${camera}" "${time}"
            montage_inputs+=("-label" "${title}" "${OUT_DIR}/${name}.png")
        done
    done
    if command -v magick >/dev/null 2>&1; then
        magick montage "${montage_inputs[@]}" -geometry 320x180+8+24 -tile 4x3 \
            "${OUT_DIR}/${camera}-contact-sheet.png"
    fi
done

capture "mid-planar-validity" "mid planar validity" planar mid 12.0 \
    cloud-reflection-validity
capture "high-planar-validity" "high planar validity" planar high 12.0 \
    cloud-reflection-validity
capture "mid-planar-reflection" "mid planar reflection" planar mid 12.0 cloud-reflection
capture "high-planar-reflection" "high planar reflection" planar high 12.0 cloud-reflection

cat >"${OUT_DIR}/index.md" <<EOF
# Ocean Cloud Reflection Review

- Resolution: ${WIDTH}x${HEIGHT}
- Ocean map: ${MAP_SIZE}
- Planar product: scale ${PLANAR_SCALE}, ${PLANAR_STEPS} steps, guard ${PLANAR_GUARD}
- Sources: current-view, cached, hybrid, planar
- Lighting: noon, sunset, night
- Cameras: mid, high

Review the two contact sheets for coverage, source handoff, lighting agreement, and
offscreen reflection continuity. The planar validity images use red for valid
planar cloud samples and green for cached or clear fallback. The reflection images
isolate the selected reflected lighting from the rest of the water material.
EOF

printf 'Ocean cloud reflection review written to %s\n' "${OUT_DIR}"
