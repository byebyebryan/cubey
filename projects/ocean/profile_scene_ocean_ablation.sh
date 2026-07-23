#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/ocean/ocean}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/ocean/scene-ablation-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1600}"
HEIGHT="${HEIGHT:-900}"
FRAMES="${FRAMES:-180}"
STILL_FRAMES="${STILL_FRAMES:-90}"
WARMUP_FRAMES="${WARMUP_FRAMES:-30}"
FPS="${FPS:-60}"
GPU_IDLE_ATTEMPTS="${GPU_IDLE_ATTEMPTS:-300}"
PROFILE_ATTEMPTS="${PROFILE_ATTEMPTS:-5}"
SUMMARIZE_ONLY="${SUMMARIZE_ONLY:-0}"
LANE_FILTER="${LANE_FILTER:-}"

mkdir -p "${OUT_DIR}/profiles" "${OUT_DIR}/captures" "${OUT_DIR}/clips"

SUMMARY="${OUT_DIR}/summary.tsv"
SOURCE_COMMIT="$(git -C "${ROOT_DIR}" rev-parse HEAD)"

declare -A CAPTURE_BY_LANE=()
declare -A LABEL_BY_LANE=()

external_compute_busy() {
    command -v nvidia-smi >/dev/null 2>&1 || return 1
    nvidia-smi pmon -c 1 -s u 2>/dev/null | awk '
        $1 ~ /^[0-9]+$/ && $3 ~ /C/ && $4 ~ /^[0-9]+$/ && $4 + 0 >= 1 {
            busy = 1
        }
        END { exit busy ? 0 : 1 }
    '
}

gpu_busy() {
    command -v nvidia-smi >/dev/null 2>&1 || return 1
    if external_compute_busy; then
        return 0
    fi
    nvidia-smi dmon -c 1 -s u 2>/dev/null | awk '
        $1 ~ /^[0-9]+$/ && (($2 ~ /^[0-9]+$/ && $2 + 0 >= 10) ||
                            ($3 ~ /^[0-9]+$/ && $3 + 0 >= 10)) {
            busy = 1
        }
        END { exit busy ? 0 : 1 }
    '
}

wait_for_gpu_idle() {
    local idle_samples=0
    for _ in $(seq 1 "${GPU_IDLE_ATTEMPTS}"); do
        if gpu_busy; then
            idle_samples=0
        else
            idle_samples=$((idle_samples + 1))
            if ((idle_samples >= 2)); then
                return 0
            fi
        fi
        sleep 2
    done
    return 1
}

span_stats() {
    local passes="$1"
    local group="$2"
    awk -F, -v group="${group}" '
        NR == 1 || $2 != "gpu" { next }
        group == "total" {
            total[$1] += $5
        }
        group == "wave" && $3 ~ /^ocean\.(modulate|fft|unpack)\.c[0-9]+$/ {
            total[$1] += $5
        }
        group == "scene" && $3 == "ocean scene" {
            total[$1] += $5
        }
        group == "products" &&
            ($3 == "cloud shadow" || $3 == "ocean.cloud_environment" ||
             $3 == "ocean.cloud_planar_reflection") {
            total[$1] += $5
        }
        group == "cloud" &&
            ($3 == "cloud shadow" || $3 == "cloud march" || $3 == "cloud composite" ||
             $3 == "ocean.cloud_environment" || $3 == "ocean.cloud_planar_reflection") {
            total[$1] += $5
        }
        group == "post" && $3 == "ocean post" {
            total[$1] += $5
        }
        END {
            for (frame in total) print total[frame]
        }
    ' "${passes}" | sort -n | awk '
        { values[NR] = $1; sum += $1 }
        END {
            if (NR == 0) {
                printf "0.000000 0.000000 0.000000 0"
                exit
            }
            p50_index = int((NR - 1) * 0.50) + 1
            p95_index = int((NR - 1) * 0.95) + 1
            printf "%.6f %.6f %.6f %d", sum / NR, values[p50_index], values[p95_index], NR
        }
    '
}

sum_values() {
    awk -v first="$1" -v second="$2" 'BEGIN { printf "%.6f", first + second }'
}

subtract_nonnegative() {
    awk -v first="$1" -v second="$2" \
        'BEGIN { difference = first - second; printf "%.6f", (difference > 0 ? difference : 0) }'
}

profile_wave_is_stable() {
    local passes="$1"
    local _ wave_p50 wave_p95 samples
    read -r _ wave_p50 wave_p95 samples <<<"$(span_stats "${passes}" wave)"
    if ! awk -v p50="${wave_p50}" -v p95="${wave_p95}" -v samples="${samples}" \
        'BEGIN { exit !(samples > 0 && p50 > 0 && p95 <= p50 * 1.20) }'; then
        return 1
    fi

    local reference="${OUT_DIR}/profiles/control-close.passes.csv"
    if [[ ! -f "${reference}" || "${passes}" == "${reference}" ]]; then
        return 0
    fi
    local reference_p50
    read -r _ reference_p50 _ _ <<<"$(span_stats "${reference}" wave)"
    awk -v value="${wave_p50}" -v reference="${reference_p50}" \
        'BEGIN { ratio = value / reference; exit !(ratio >= 0.80 && ratio <= 1.25) }'
}

metric_last() {
    local metrics="$1"
    local name="$2"
    awk -F, -v name="${name}" '
        NR > 1 && $2 == "ocean.mesh" && $3 == name { value = $4 }
        END { printf "%s", value == "" ? "0" : value }
    ' "${metrics}"
}

common_args() {
    printf '%s\n' \
        --ocean-map-size 512 \
        --ocean-field-precision half \
        --ocean-sea-state windy \
        --ocean-surface-mode curved-far \
        --no-ocean-size-reference \
        --no-ocean-terrain-fields \
        --time-of-day-mode manual \
        --pause-time
}

run_lane() {
    local lane="$1"
    local camera="$2"
    local width="$3"
    local height="$4"
    local sun_elevation="$5"
    local clouds="$6"
    local reflection="$7"
    shift 7

    local prefix="${OUT_DIR}/profiles/${lane}"
    local clip="${OUT_DIR}/clips/${lane}.mp4"
    local cloud_args=()
    local base_args=()
    mapfile -t base_args < <(common_args)

    if [[ "${clouds}" == "on" ]]; then
        cloud_args=(--cloud-quality full --cloud-weather-preset surface-volume)
    else
        cloud_args=(--no-clouds)
    fi

    for attempt in $(seq 1 "${PROFILE_ATTEMPTS}"); do
        if ! wait_for_gpu_idle; then
            printf 'GPU remained busy before scene-ocean lane %s\n' "${lane}" >&2
            return 1
        fi

        "${APP}" \
            --headless \
            --capture video \
            --frames "${FRAMES}" \
            --fps "${FPS}" \
            --width "${width}" \
            --height "${height}" \
            --ocean-camera-preset "${camera}" \
            --ocean-cloud-reflection-source "${reflection}" \
            --sun-elevation "${sun_elevation}" \
            --sun-azimuth -20 \
            --profile-output "${prefix}" \
            --profile-warmup-frames "${WARMUP_FRAMES}" \
            "${base_args[@]}" \
            "${cloud_args[@]}" \
            "$@" \
            --output "${clip}"

        if ! profile_wave_is_stable "${prefix}.passes.csv"; then
            printf 'unstable wave timings followed lane %s; retrying (%d/%d)\n' \
                "${lane}" "${attempt}" "${PROFILE_ATTEMPTS}" >&2
            continue
        fi
        sleep 2
        if ! gpu_busy; then
            return 0
        fi
        printf 'GPU work followed lane %s; retrying (%d/%d)\n' \
            "${lane}" "${attempt}" "${PROFILE_ATTEMPTS}" >&2
    done
    printf 'unstable or external GPU work repeatedly overlapped lane %s\n' "${lane}" >&2
    return 1
}

capture_lane() {
    local lane="$1"
    local camera="$2"
    local width="$3"
    local height="$4"
    local sun_elevation="$5"
    local clouds="$6"
    local reflection="$7"
    shift 7

    local output="${OUT_DIR}/captures/${lane}.png"
    local cloud_args=()
    local base_args=()
    mapfile -t base_args < <(common_args)

    if [[ "${clouds}" == "on" ]]; then
        cloud_args=(--cloud-quality full --cloud-weather-preset surface-volume)
    else
        cloud_args=(--no-clouds)
    fi

    "${APP}" \
        --headless \
        --frames "${STILL_FRAMES}" \
        --width "${width}" \
        --height "${height}" \
        --ocean-camera-preset "${camera}" \
        --ocean-cloud-reflection-source "${reflection}" \
        --sun-elevation "${sun_elevation}" \
        --sun-azimuth -20 \
        "${base_args[@]}" \
        "${cloud_args[@]}" \
        "$@" \
        --output "${output}"
}

background_lane_name() {
    local camera="$1"
    local width="$2"
    local height="$3"
    local sun_elevation="$4"
    printf 'background-%s-%sx%s-sun%s' "${camera}" "${width}" "${height}" "${sun_elevation}"
}

ensure_background_lane() {
    local camera="$1"
    local width="$2"
    local height="$3"
    local sun_elevation="$4"
    local lane
    lane="$(background_lane_name "${camera}" "${width}" "${height}" "${sun_elevation}")"

    if [[ "${SUMMARIZE_ONLY}" == "1" ]]; then
        if [[ ! -f "${OUT_DIR}/profiles/${lane}.passes.csv" ]]; then
            printf 'missing scene-ocean background profile: %s\n' \
                "${OUT_DIR}/profiles/${lane}.passes.csv" >&2
            exit 1
        fi
        return
    fi
    if [[ -f "${OUT_DIR}/profiles/${lane}.passes.csv" ]]; then
        return
    fi
    run_lane "${lane}" "${camera}" "${width}" "${height}" "${sun_elevation}" off cached \
        --debug-view background
}

summarize_lane() {
    local lane="$1"
    local family="$2"
    local camera="$3"
    local width="$4"
    local height="$5"
    local sun_elevation="$6"
    local setting="$7"
    local clouds="$8"
    local reflection="$9"
    local passes="${OUT_DIR}/profiles/${lane}.passes.csv"
    local metrics="${OUT_DIR}/profiles/${lane}.metrics.csv"
    local suffix

    for suffix in frames.csv passes.csv metrics.csv trace.json summary.txt; do
        if [[ ! -f "${OUT_DIR}/profiles/${lane}.${suffix}" ]]; then
            printf 'missing scene-ocean profile artifact: %s\n' \
                "${OUT_DIR}/profiles/${lane}.${suffix}" >&2
            exit 1
        fi
    done

    local background_lane background_passes
    background_lane="$(background_lane_name "${camera}" "${width}" "${height}" "${sun_elevation}")"
    background_passes="${OUT_DIR}/profiles/${background_lane}.passes.csv"

    local total_mean total_p50 total_p95 samples
    local _ wave_p50 scene_p50 background_p50 surface_p50 core_p50 adapter_p50
    local products_p50 cloud_p50 post_p50
    read -r total_mean total_p50 total_p95 samples <<<"$(span_stats "${passes}" total)"
    read -r _ wave_p50 _ _ <<<"$(span_stats "${passes}" wave)"
    read -r _ scene_p50 _ _ <<<"$(span_stats "${passes}" scene)"
    read -r _ background_p50 _ _ <<<"$(span_stats "${background_passes}" scene)"
    surface_p50="$(subtract_nonnegative "${scene_p50}" "${background_p50}")"
    core_p50="$(sum_values "${wave_p50}" "${surface_p50}")"
    read -r _ products_p50 _ _ <<<"$(span_stats "${passes}" products)"
    adapter_p50="$(sum_values "${core_p50}" "${products_p50}")"
    read -r _ cloud_p50 _ _ <<<"$(span_stats "${passes}" cloud)"
    read -r _ post_p50 _ _ <<<"$(span_stats "${passes}" post)"

    printf '%s\t%s\t%s\t%sx%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "${lane}" "${family}" "${camera}" "${width}" "${height}" "${sun_elevation}" "${setting}" \
        "${clouds}" "${reflection}" "${total_mean}" "${total_p50}" "${total_p95}" "${wave_p50}" \
        "${background_p50}" "${surface_p50}" "${core_p50}" "${adapter_p50}" "${cloud_p50}" \
        "${post_p50}" "$(metric_last "${metrics}" effective_cells)" \
        "$(metric_last "${metrics}" effective_lod_levels)" \
        "$(metric_last "${metrics}" generated_triangles)" \
        "$(metric_last "${metrics}" submitted_triangles)" "${samples}" >>"${SUMMARY}"
}

study_lane() {
    local lane="$1"
    local family="$2"
    local camera="$3"
    local width="$4"
    local height="$5"
    local sun_elevation="$6"
    local setting="$7"
    local clouds="$8"
    local reflection="$9"
    shift 9

    if [[ -n "${LANE_FILTER}" && "${lane}" != *"${LANE_FILTER}"* ]]; then
        return
    fi

    ensure_background_lane "${camera}" "${width}" "${height}" "${sun_elevation}"
    CAPTURE_BY_LANE["${lane}"]="${OUT_DIR}/captures/${lane}.png"
    LABEL_BY_LANE["${lane}"]="${lane}: ${setting}"

    if [[ "${SUMMARIZE_ONLY}" != "1" ]]; then
        run_lane "${lane}" "${camera}" "${width}" "${height}" "${sun_elevation}" \
            "${clouds}" "${reflection}" "$@"
        capture_lane "${lane}" "${camera}" "${width}" "${height}" "${sun_elevation}" \
            "${clouds}" "${reflection}" "$@"
    fi
    summarize_lane "${lane}" "${family}" "${camera}" "${width}" "${height}" "${sun_elevation}" \
        "${setting}" "${clouds}" "${reflection}"
}

write_contact_sheet() {
    local output="$1"
    local tile="$2"
    shift 2

    if ! command -v magick >/dev/null 2>&1; then
        printf 'ImageMagick not found; skipped %s\n' "${output}" >&2
        return
    fi

    local montage_inputs=()
    local lane
    for lane in "$@"; do
        if [[ ! -f "${CAPTURE_BY_LANE[${lane}]}" ]]; then
            printf 'missing scene-ocean capture: %s\n' "${CAPTURE_BY_LANE[${lane}]}" >&2
            exit 1
        fi
        montage_inputs+=("-label" "${LABEL_BY_LANE[${lane}]}" "${CAPTURE_BY_LANE[${lane}]}")
    done
    magick montage "${montage_inputs[@]}" -geometry 400x225+8+26 -tile "${tile}" "${output}"
}

printf 'lane\tfamily\tcamera\tresolution\tsun_elevation\tsetting\tclouds\treflection\ttotal_mean_ms\ttotal_p50_ms\ttotal_p95_ms\twave_p50_ms\tbackground_p50_ms\tsurface_p50_ms\tcore_owned_p50_ms\tadapter_p50_ms\tcloud_p50_ms\tpost_p50_ms\teffective_cells\teffective_lod_levels\tgenerated_triangles\tsubmitted_triangles\tsamples\n' \
    >"${SUMMARY}"

study_lane control-close control close "${WIDTH}" "${HEIGHT}" 42 default off cached
study_lane control-low control low "${WIDTH}" "${HEIGHT}" 42 default off cached
study_lane control-mid control mid "${WIDTH}" "${HEIGHT}" 42 default off cached

for camera in low mid; do
    study_lane "mesh384-${camera}" geometry "${camera}" "${WIDTH}" "${HEIGHT}" 42 \
        "mesh cells 384" off cached --ocean-mesh-cells 384
    study_lane "mesh256-${camera}" geometry "${camera}" "${WIDTH}" "${HEIGHT}" 42 \
        "mesh cells 256" off cached --ocean-mesh-cells 256
    study_lane "mesh192-${camera}" geometry "${camera}" "${WIDTH}" "${HEIGHT}" 42 \
        "mesh cells 192" off cached --ocean-mesh-cells 192
    study_lane "near3-${camera}" geometry "${camera}" "${WIDTH}" "${HEIGHT}" 42 \
        "near cell 3 m" off cached --ocean-horizon-target-near-cell-m 3
    study_lane "near4-${camera}" geometry "${camera}" "${WIDTH}" "${HEIGHT}" 42 \
        "near cell 4 m" off cached --ocean-horizon-target-near-cell-m 4
done

for camera in low mid; do
    study_lane "shadow8-${camera}" shadow "${camera}" "${WIDTH}" "${HEIGHT}" 8 \
        "shadow 8 steps" off cached
    study_lane "shadow4-${camera}" shadow "${camera}" "${WIDTH}" "${HEIGHT}" 8 \
        "shadow 4 steps" off cached --ocean-self-shadow-steps 4
    study_lane "shadow2-${camera}" shadow "${camera}" "${WIDTH}" "${HEIGHT}" 8 \
        "shadow 2 steps" off cached --ocean-self-shadow-steps 2
    study_lane "shadow-off-${camera}" shadow "${camera}" "${WIDTH}" "${HEIGHT}" 8 \
        "shadow disabled" off cached --ocean-self-shadow-strength 0
done

for camera in low mid; do
    study_lane "filter-bilinear-${camera}" filter "${camera}" "${WIDTH}" "${HEIGHT}" 42 \
        "bilinear filter" off cached --ocean-detail-filter bilinear
    study_lane "filter-bicubic-${camera}" filter "${camera}" "${WIDTH}" "${HEIGHT}" 42 \
        "bicubic filter" off cached --ocean-detail-filter bicubic
    study_lane "detail-half-${camera}" anti-repeat "${camera}" "${WIDTH}" "${HEIGHT}" 42 \
        "detail anti-repeat 0.5" off cached --ocean-detail-anti-repeat-strength 0.5
    study_lane "detail-off-${camera}" anti-repeat "${camera}" "${WIDTH}" "${HEIGHT}" 42 \
        "detail anti-repeat disabled" off cached --ocean-detail-anti-repeat-strength 0
    study_lane "shape-off-${camera}" anti-repeat "${camera}" "${WIDTH}" "${HEIGHT}" 42 \
        "shape anti-repeat disabled" off cached --ocean-shape-anti-repeat-strength 0
done

study_lane cascade0-mid cascade mid "${WIDTH}" "${HEIGHT}" 42 "C0 surface only" off cached \
    --ocean-cascade 0
study_lane cascade1-mid cascade mid "${WIDTH}" "${HEIGHT}" 42 "C1 surface only" off cached \
    --ocean-cascade 1

for camera in low mid; do
    study_lane "composed-cached-${camera}" composed "${camera}" "${WIDTH}" "${HEIGHT}" 42 \
        "cached cloud reflection" on cached
    study_lane "composed-planar-${camera}" composed "${camera}" "${WIDTH}" "${HEIGHT}" 42 \
        "planar cloud reflection" on planar
done

study_lane scaling-low scaling low 2560 1440 42 "2560x1440 control" off cached
study_lane scaling-mid scaling mid 2560 1440 42 "2560x1440 control" off cached

if [[ -z "${LANE_FILTER}" ]]; then
    write_contact_sheet "${OUT_DIR}/control-contact-sheet.png" 3x1 \
        control-close control-low control-mid
    write_contact_sheet "${OUT_DIR}/geometry-contact-sheet.png" 4x3 \
        control-low mesh384-low mesh256-low mesh192-low near3-low near4-low \
        control-mid mesh384-mid mesh256-mid mesh192-mid near3-mid near4-mid
    write_contact_sheet "${OUT_DIR}/shadow-contact-sheet.png" 4x2 \
        shadow8-low shadow4-low shadow2-low shadow-off-low \
        shadow8-mid shadow4-mid shadow2-mid shadow-off-mid
    write_contact_sheet "${OUT_DIR}/filter-contact-sheet.png" 3x2 \
        control-low filter-bilinear-low filter-bicubic-low \
        control-mid filter-bilinear-mid filter-bicubic-mid
    write_contact_sheet "${OUT_DIR}/anti-repeat-contact-sheet.png" 3x2 \
        control-low detail-half-low detail-off-low \
        control-mid detail-half-mid detail-off-mid
    write_contact_sheet "${OUT_DIR}/shape-cascade-contact-sheet.png" 3x2 \
        control-low shape-off-low control-mid shape-off-mid cascade0-mid cascade1-mid
    write_contact_sheet "${OUT_DIR}/composed-contact-sheet.png" 2x2 \
        composed-cached-low composed-planar-low composed-cached-mid composed-planar-mid
    write_contact_sheet "${OUT_DIR}/scaling-contact-sheet.png" 2x2 \
        control-low scaling-low control-mid scaling-mid
fi

{
    printf '# Scene Ocean Ablation Study\n\n'
    printf -- '- Source: `%s`\n' "${SOURCE_COMMIT}"
    printf -- '- Primary resolution: %sx%s\n' "${WIDTH}" "${HEIGHT}"
    printf -- '- Frames: %s; warmup: %s; still frames: %s\n' \
        "${FRAMES}" "${WARMUP_FRAMES}" "${STILL_FRAMES}"
    printf -- '- Source: Windy, 512 half, curved far surface, paused time.\n'
    printf -- '- Totals use the original non-overlapping render-graph spans.\n'
    printf -- '- Surface time is estimated by subtracting a matched background-only run.\n'
    printf -- '- Core-owned time is wave compute plus the estimated ocean surface time.\n'
    printf -- '- Adapter time: core plus ocean-requested cloud shadow/reflection products.\n'
    printf -- '- Shared atmosphere background, cloud march/composite, and post remain separate.\n'
    printf -- '- Runs reject concurrent external GPU compute work.\n\n'
    if [[ -n "${LANE_FILTER}" ]]; then
        printf -- '- Lane filter: `%s`\n\n' "${LANE_FILTER}"
    fi
    printf 'Review contact sheets in this order: control, geometry, shadow, filter, anti-repeat, '
    printf 'shape/cascade, composed, scaling.\n\n'
    printf '```tsv\n'
    cat "${SUMMARY}"
    printf '```\n'
} >"${OUT_DIR}/index.md"

printf 'Scene-ocean ablation study written to %s\n' "${OUT_DIR}"
