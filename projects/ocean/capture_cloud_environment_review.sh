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

production_sources=(cached planar)
cameras=(mid high)
scenarios=(noon sunset night)
scenario_times=(12.0 17.8 23.0)

mkdir -p "${OUT_DIR}"
printf 'file\ttitle\trole\tsource\tcamera\ttime\tdebug_view\n' >"${OUT_DIR}/manifest.tsv"

capture() {
    local name="$1"
    local title="$2"
    local role="$3"
    local source="$4"
    local camera="$5"
    local time="$6"
    local debug_view="${7:-final}"
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
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "${name}.png" "${title}" "${role}" \
        "${source}" "${camera}" "${time}" "${debug_view}" >>"${OUT_DIR}/manifest.tsv"
}

for camera in "${cameras[@]}"; do
    production_montage_inputs=()
    reference_montage_inputs=()
    for scenario_index in "${!scenarios[@]}"; do
        scenario="${scenarios[${scenario_index}]}"
        time="${scenario_times[${scenario_index}]}"
        for source in "${production_sources[@]}"; do
            name="${camera}-${scenario}-${source}"
            title="${camera} ${scenario} ${source}"
            capture "${name}" "${title}" production "${source}" "${camera}" "${time}"
            production_montage_inputs+=("-label" "${title}" "${OUT_DIR}/${name}.png")
        done
        name="${camera}-${scenario}-current-view-reference"
        title="${camera} ${scenario} current-view reference"
        capture "${name}" "${title}" reference current-view "${camera}" "${time}"
        reference_montage_inputs+=("-label" "${title}" "${OUT_DIR}/${name}.png")
    done
    if command -v magick >/dev/null 2>&1; then
        magick montage "${production_montage_inputs[@]}" -geometry 480x270+8+24 -tile 2x3 \
            "${OUT_DIR}/${camera}-production-contact-sheet.png"
        magick montage "${reference_montage_inputs[@]}" -geometry 480x270+8+24 -tile 1x3 \
            "${OUT_DIR}/${camera}-current-view-reference-contact-sheet.png"
    fi
done

capture "mid-planar-validity" "mid planar validity" diagnostic planar mid 12.0 \
    cloud-reflection-validity
capture "high-planar-validity" "high planar validity" diagnostic planar high 12.0 \
    cloud-reflection-validity
capture "mid-planar-reflection" "mid planar reflection" diagnostic planar mid 12.0 \
    cloud-reflection
capture "high-planar-reflection" "high planar reflection" diagnostic planar high 12.0 \
    cloud-reflection

cat >"${OUT_DIR}/index.md" <<EOF
# Ocean Cloud Reflection Review

- Resolution: ${WIDTH}x${HEIGHT}
- Ocean map: ${MAP_SIZE}
- Planar product: scale ${PLANAR_SCALE}, ${PLANAR_STEPS} steps, guard ${PLANAR_GUARD}
- Production sources: cached, planar
- Reference source: current-view
- Lighting: noon, sunset, night
- Cameras: mid, high

Review the production contact sheets for coverage, lighting agreement, and offscreen
reflection continuity. Current-view sheets remain a bounded quality reference, not
a production candidate. The planar validity images use red for valid planar cloud
samples and green for cached or clear fallback. The reflection images isolate the
selected reflected lighting from the rest of the water material.
EOF

printf 'Ocean cloud reflection review written to %s\n' "${OUT_DIR}"
