#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/ocean/ocean}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/ocean/performance-baseline-$(date +%Y%m%d-%H%M%S)}"
WIDTH="${WIDTH:-1600}"
HEIGHT="${HEIGHT:-900}"
FRAMES="${FRAMES:-180}"
WARMUP_FRAMES="${WARMUP_FRAMES:-30}"
FPS="${FPS:-60}"
GPU_IDLE_ATTEMPTS="${GPU_IDLE_ATTEMPTS:-300}"
PROFILE_ATTEMPTS="${PROFILE_ATTEMPTS:-5}"
SUMMARIZE_ONLY="${SUMMARIZE_ONLY:-0}"

mkdir -p "${OUT_DIR}/profiles"

SUMMARY="${OUT_DIR}/summary.tsv"
SOURCE_COMMIT="$(git -C "${ROOT_DIR}" rev-parse HEAD)"

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

frame_stats() {
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
        group == "cloud" &&
            ($3 == "cloud shadow" || $3 == "cloud march" || $3 == "cloud composite" ||
             $3 == "ocean.cloud_environment" ||
             $3 == "ocean.cloud_planar_reflection") {
            total[$1] += $5
        }
        group == "background" && $3 == "ocean background" {
            total[$1] += $5
        }
        group == "surface" && $3 == "ocean surface" {
            total[$1] += $5
        }
        group == "core" &&
            ($3 ~ /^ocean\.(modulate|fft|unpack)\.c[0-9]+$/ || $3 == "ocean surface") {
            total[$1] += $5
        }
        group == "adapter" &&
            ($3 ~ /^ocean\.(modulate|fft|unpack)\.c[0-9]+$/ || $3 == "ocean surface" ||
             $3 == "cloud shadow" || $3 == "ocean.cloud_environment" ||
             $3 == "ocean.cloud_planar_reflection") {
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

run_lane() {
    local lane="$1"
    local map_size="$2"
    local precision="$3"
    local camera="$4"
    local cloud_mode="$5"
    local reflection="$6"
    local prefix="${OUT_DIR}/profiles/${lane}"
    local video="${prefix}.mp4"
    local cloud_args=()

    if [[ "${cloud_mode}" == "off" ]]; then
        cloud_args=(--no-clouds)
    else
        cloud_args=(--cloud-quality full --cloud-weather-preset surface-volume)
    fi

    for attempt in $(seq 1 "${PROFILE_ATTEMPTS}"); do
        if ! wait_for_gpu_idle; then
            printf 'GPU remained busy before ocean profile lane %s\n' "${lane}" >&2
            return 1
        fi

        "${APP}" \
            --headless \
            --capture video \
            --frames "${FRAMES}" \
            --fps "${FPS}" \
            --width "${WIDTH}" \
            --height "${HEIGHT}" \
            --ocean-map-size "${map_size}" \
            --ocean-field-precision "${precision}" \
            --ocean-sea-state windy \
            --ocean-camera-preset "${camera}" \
            --ocean-cloud-reflection-source "${reflection}" \
            --time-of-day-mode manual \
            --sun-elevation 42 \
            --sun-azimuth -20 \
            --pause-time \
            --profile-output "${prefix}" \
            --profile-warmup-frames "${WARMUP_FRAMES}" \
            "${cloud_args[@]}" \
            --output "${video}"
        rm -f "${video}"

        if ! external_compute_busy; then
            return 0
        fi
        printf 'external GPU work followed ocean profile lane %s; retrying (%d/%d)\n' \
            "${lane}" "${attempt}" "${PROFILE_ATTEMPTS}" >&2
    done
    printf 'external GPU work repeatedly overlapped ocean profile lane %s\n' "${lane}" >&2
    return 1
}

if [[ "${SUMMARIZE_ONLY}" != "1" ]]; then
    run_lane surface-256-half-mid 256 half mid off cached
    run_lane surface-512-half-mid 512 half mid off cached
    run_lane surface-1024-half-mid 1024 half mid off cached
    run_lane surface-512-full-mid 512 full mid off cached
    run_lane surface-512-half-low 512 half low off cached
    run_lane surface-512-half-high 512 half high off cached
    run_lane composed-512-half-cached 512 half mid on cached
    run_lane composed-512-half-planar 512 half mid on planar
fi

printf 'lane\tmap_size\tprecision\tcamera\tclouds\treflection\ttotal_mean_ms\ttotal_p50_ms\ttotal_p95_ms\twave_p50_ms\tbackground_p50_ms\tsurface_p50_ms\tcore_owned_p50_ms\tadapter_p50_ms\tcloud_p50_ms\tpost_p50_ms\tsamples\n' \
    >"${SUMMARY}"

summarize_lane() {
    local lane="$1"
    local map_size="$2"
    local precision="$3"
    local camera="$4"
    local clouds="$5"
    local reflection="$6"
    local passes="${OUT_DIR}/profiles/${lane}.passes.csv"
    local suffix

    for suffix in frames.csv passes.csv metrics.csv trace.json summary.txt; do
        if [[ ! -f "${OUT_DIR}/profiles/${lane}.${suffix}" ]]; then
            printf 'missing ocean profile artifact: %s\n' \
                "${OUT_DIR}/profiles/${lane}.${suffix}" >&2
            exit 1
        fi
    done

    local total_mean total_p50 total_p95 samples
    local _ wave_p50 background_p50 surface_p50 core_p50 adapter_p50 cloud_p50 post_p50
    read -r total_mean total_p50 total_p95 samples <<<"$(frame_stats "${passes}" total)"
    read -r _ wave_p50 _ _ <<<"$(frame_stats "${passes}" wave)"
    read -r _ background_p50 _ _ <<<"$(frame_stats "${passes}" background)"
    read -r _ surface_p50 _ _ <<<"$(frame_stats "${passes}" surface)"
    read -r _ core_p50 _ _ <<<"$(frame_stats "${passes}" core)"
    read -r _ adapter_p50 _ _ <<<"$(frame_stats "${passes}" adapter)"
    read -r _ cloud_p50 _ _ <<<"$(frame_stats "${passes}" cloud)"
    read -r _ post_p50 _ _ <<<"$(frame_stats "${passes}" post)"

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "${lane}" "${map_size}" "${precision}" "${camera}" "${clouds}" "${reflection}" \
        "${total_mean}" "${total_p50}" "${total_p95}" "${wave_p50}" "${background_p50}" \
        "${surface_p50}" "${core_p50}" "${adapter_p50}" "${cloud_p50}" "${post_p50}" \
        "${samples}" >>"${SUMMARY}"
}

summarize_lane surface-256-half-mid 256 half mid off cached
summarize_lane surface-512-half-mid 512 half mid off cached
summarize_lane surface-1024-half-mid 1024 half mid off cached
summarize_lane surface-512-full-mid 512 full mid off cached
summarize_lane surface-512-half-low 512 half low off cached
summarize_lane surface-512-half-high 512 half high off cached
summarize_lane composed-512-half-cached 512 half mid on cached
summarize_lane composed-512-half-planar 512 half mid on planar

{
    printf '# Ocean Performance Baseline\n\n'
    printf -- '- Source: `%s`\n' "${SOURCE_COMMIT}"
    printf -- '- Resolution: %sx%s\n' "${WIDTH}" "${HEIGHT}"
    printf -- '- Frames: %s; warmup: %s\n' "${FRAMES}" "${WARMUP_FRAMES}"
    printf -- '- Sea state: Windy\n'
    printf -- '- GPU samples are per-frame sums of non-overlapping leaf spans.\n'
    printf -- '- Core-owned time is wave compute plus the ocean surface pass.\n'
    printf -- '- Adapter time adds ocean-requested cloud shadow and reflection products.\n'
    printf -- '- Runs reject concurrent external GPU compute work.\n\n'
    printf '```tsv\n'
    cat "${SUMMARY}"
    printf '```\n'
} >"${OUT_DIR}/index.md"

printf 'Ocean performance baseline written to %s\n' "${OUT_DIR}"
