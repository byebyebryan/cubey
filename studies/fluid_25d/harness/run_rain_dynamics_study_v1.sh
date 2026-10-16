#!/usr/bin/env bash
set -euo pipefail

# Reproducible RainPulse dynamics study. This runner deliberately
# stays outside the solver and presentation implementation: it runs the
# existing terrain-case finite-volume protocol over one immutable matrix,
# captures truthful post-step checkpoints, and assembles review evidence.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/fluid/fluid_25d/fluid_25d}"
RAIN_STUDY_TIER="${RAIN_STUDY_TIER:-neutral}"
case "${RAIN_STUDY_TIER}" in
    neutral)
        TIER_LABEL=neutral
        TIER_ROLE='neutral-discovery-baseline'
        RAIN_MM_PER_HOUR=12
        DEFAULT_OUT_DIR="${ROOT_DIR}/outputs/fluid/rain-dynamics-study-v1-neutral-20260921-final"
        ;;
    visibility-stress)
        TIER_LABEL=visibility-stress
        TIER_ROLE='product-visibility-stress-not-climate-or-hydrology-evidence'
        RAIN_MM_PER_HOUR=60
        DEFAULT_OUT_DIR="${ROOT_DIR}/outputs/fluid/rain-dynamics-study-v1-visibility-stress-20260921-final"
        ;;
    *)
        printf 'RAIN_STUDY_TIER must be exactly neutral or visibility-stress: %s\n' \
            "${RAIN_STUDY_TIER}" >&2
        exit 1
        ;;
esac
OUT_DIR="${1:-${DEFAULT_OUT_DIR}}"
WIDTH=1280
HEIGHT=720
GRID_WIDTH=256
GRID_HEIGHT=128
CELL_SIZE_M=30
FIXED_DELTA_SECONDS=2
SUBSTEPS=8
RAIN_DURATION_SECONDS=600
TOTAL_FRAMES=600
SEQUENCE_INTERVAL_FRAMES=15
VIDEO_FPS=4
SLOW_POOLED_SPEED_THRESHOLD_M_PER_S=0.02
MAX_CONSERVATION_RESIDUAL_FRACTION_OF_SOURCE=0.0001
EXPECTED_SEQUENCE_FRAMES=$((1 + TOTAL_FRAMES / SEQUENCE_INTERVAL_FRAMES))
EXPECTED_VIDEO_DURATION_SECONDS="$(awk \
    -v count="${EXPECTED_SEQUENCE_FRAMES}" -v fps="${VIDEO_FPS}" \
    'BEGIN { printf "%.6f", count / fps }')"

[[ -x "${APP}" ]] || {
    printf 'fluid 2.5D app is missing or not executable: %s\n' "${APP}" >&2
    exit 1
}
[[ -x /usr/bin/ffmpeg && -x /usr/bin/ffprobe ]] || {
    printf 'ffmpeg and ffprobe are required at /usr/bin\n' >&2
    exit 1
}
[[ -x /usr/bin/magick ]] || {
    printf 'ImageMagick magick is required at /usr/bin/magick\n' >&2
    exit 1
}

mkdir -p "${OUT_DIR}/profiles" "${OUT_DIR}/videos" "${OUT_DIR}/logs" "${OUT_DIR}/review"
COMMAND_LOG="${OUT_DIR}/commands.txt"
: >"${COMMAND_LOG}"

declare -a CASE_NAMES=(
    rolling-hills-catchment
    rolling-lowland-branching
    canyon-one-flash
)
declare -a CASE_PATHS=(
    cache/terrain/sources/v1/presets/rolling-hills-1
    cache/terrain/sources/v1/presets/rolling-lowland-1
    cache/terrain/sources/v1/desert-canyon-study/canyon-candidate-1
)
declare -a CASE_CROP_X=(128 1216 1344)
declare -a CASE_CROP_Z=(1728 1664 320)

record_command() {
    local value
    for value in "$@"; do
        printf '%q ' "${value}" >>"${COMMAND_LOG}"
    done
    printf '\n' >>"${COMMAND_LOG}"
}

run_app() {
    local log_path="$1"
    shift
    local -a command=("${APP}" "$@")
    record_command "${command[@]}"
    "${command[@]}" >"${log_path}" 2>&1
}

run_tool() {
    local log_path="$1"
    shift
    record_command "$@"
    "$@" >"${log_path}" 2>&1
}

terrain_manifest_value() {
    local manifest="$1"
    local key="$2"
    awk -F'"' -v wanted="${key}" '$2 == wanted { print $4; exit }' "${manifest}"
}

common_args() {
    local terrain_path="$1"
    local crop_x="$2"
    local crop_z="$3"
    printf '%s\n' \
        --headless --capture png --width "${WIDTH}" --height "${HEIGHT}" \
        --fluid25d-solver finite-volume \
        --fluid25d-fixed-delta-seconds "${FIXED_DELTA_SECONDS}" \
        --fluid25d-substeps "${SUBSTEPS}" \
        --fluid25d-scenario terrain-case \
        --terrain-heightfield "${ROOT_DIR}/${terrain_path}" \
        --grid-width "${GRID_WIDTH}" --grid-height "${GRID_HEIGHT}" \
        --fluid25d-terrain-crop-x "${crop_x}" \
        --fluid25d-terrain-crop-z "${crop_z}" \
        --fluid25d-terrain-water-protocol rain-pulse \
        --fluid25d-rainfall-rate-mm-per-hour "${RAIN_MM_PER_HOUR}" \
        --fluid25d-source-active-duration-seconds "${RAIN_DURATION_SECONDS}"
}

run_capture() {
    local case_name="$1"
    local terrain_path="$2"
    local crop_x="$3"
    local crop_z="$4"
    local capture_frame="$5"
    local view_kind="$6"
    local output_path="$7"
    local log_path="$8"
    local -a args=()
    mapfile -t args < <(common_args "${terrain_path}" "${crop_x}" "${crop_z}")
    args+=(--frames "${capture_frame}")
    case "${view_kind}" in
        composite)
            args+=(--fluid25d-view catchment --fluid25d-catchment-view composite)
            ;;
        water-depth)
            args+=(--fluid25d-view diagnostics --debug-view depth)
            ;;
        wet-dry)
            args+=(--fluid25d-view diagnostics --debug-view wet-dry)
            ;;
        flow-magnitude)
            args+=(--fluid25d-view diagnostics --debug-view flow)
            ;;
        *)
            printf 'unsupported capture view: %s\n' "${view_kind}" >&2
            exit 1
            ;;
    esac
    args+=(--output "${output_path}")
    run_app "${log_path}" "${args[@]}"
}

write_metadata_header() {
    local metadata_path="${OUT_DIR}/metadata.txt"
    {
        printf 'schema=fluid_25d_rain_dynamics_study_v1\n'
        printf 'study_tier=%s\n' "${TIER_LABEL}"
        printf 'tier_role=%s\n' "${TIER_ROLE}"
        printf 'runner=%s\n' "${BASH_SOURCE[0]}"
        printf 'runner_sha256=%s\n' "$(sha256sum "${BASH_SOURCE[0]}" | awk '{print $1}')"
        printf 'app=%s\n' "${APP}"
        printf 'app_sha256=%s\n' "$(sha256sum "${APP}" | awk '{print $1}')"
        printf 'cubey_head=%s\n' "$(git -C "${ROOT_DIR}" rev-parse HEAD)"
        printf 'cubey_branch=%s\n' "$(git -C "${ROOT_DIR}" branch --show-current)"
        printf 'cubey_worktree=%s changed_paths\n' "$(git -C "${ROOT_DIR}" status --porcelain | wc -l | tr -d ' ')"
        printf 'host=%s\n' "$(hostname)"
        printf 'gpu=%s\n' "$(nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null | head -n 1 || printf unavailable)"
        printf 'grid=%sx%s native-%sm\n' "${GRID_WIDTH}" "${GRID_HEIGHT}" "${CELL_SIZE_M}"
        printf 'solver=finite-volume\n'
        printf 'fixed_delta_seconds=%s\n' "${FIXED_DELTA_SECONDS}"
        printf 'substeps=%s\n' "${SUBSTEPS}"
        printf 'substep_seconds=%s\n' "$(awk -v delta="${FIXED_DELTA_SECONDS}" -v steps="${SUBSTEPS}" 'BEGIN { printf "%.9f", delta / steps }')"
        printf 'rainfall_rate_mm_per_hour=%s\n' "${RAIN_MM_PER_HOUR}"
        printf 'rainfall_duration_seconds=%s\n' "${RAIN_DURATION_SECONDS}"
        printf 'rainfall_stop_capture_frame=%s\n' "$((RAIN_DURATION_SECONDS / FIXED_DELTA_SECONDS))"
        printf 'rainfall_total_depth_mm=%s\n' "$(awk -v rate="${RAIN_MM_PER_HOUR}" -v seconds="${RAIN_DURATION_SECONDS}" 'BEGIN { printf "%.9f", rate * seconds / 3600.0 }')"
        printf 'total_capture_frames=%s\n' "${TOTAL_FRAMES}"
        printf 'total_simulated_seconds=%s\n' "$((TOTAL_FRAMES * FIXED_DELTA_SECONDS))"
        printf 'sequence_first_capture=frame-0001-after-2-seconds\n'
        printf 'sequence_interval_frames=%s\n' "${SEQUENCE_INTERVAL_FRAMES}"
        printf 'sequence_interval_seconds=%s\n' "$((SEQUENCE_INTERVAL_FRAMES * FIXED_DELTA_SECONDS))"
        printf 'sequence_frame_count=%s\n' "${EXPECTED_SEQUENCE_FRAMES}"
        printf 'video_fps=%s\n' "${VIDEO_FPS}"
        printf 'video_presentation_seconds=%s\n' "${EXPECTED_VIDEO_DURATION_SECONDS}"
        printf 'profile_metric_frame_mapping=profile_frame_index_plus_one_equals_capture_frame\n'
        printf 'outer_boundary=existing-terrain-case-outflow-only\n'
        printf 'slow_pooled_speed_threshold_m_per_s=%s\n' "${SLOW_POOLED_SPEED_THRESHOLD_M_PER_S}"
        printf 'max_conservation_residual_fraction_of_source=%s\n' \
            "${MAX_CONSERVATION_RESIDUAL_FRACTION_OF_SOURCE}"
        printf 'evidence_boundary=%s\n' "${TIER_ROLE}"
    } >"${metadata_path}"
}

write_metadata_header

selected_cases=0
for index in "${!CASE_NAMES[@]}"; do
    case_name="${CASE_NAMES[index]}"
    terrain_path="${CASE_PATHS[index]}"
    crop_x="${CASE_CROP_X[index]}"
    crop_z="${CASE_CROP_Z[index]}"
    terrain_root="${ROOT_DIR}/${terrain_path}"
    manifest="${terrain_root}/heightfield.json"
    [[ -f "${manifest}" && -f "${terrain_root}/elevation.f32" ]] || {
        printf 'terrain manifest/elevation missing for %s: %s\n' "${case_name}" "${terrain_root}" >&2
        exit 1
    }
    selected_cases=$((selected_cases + 1))

    case_root="${OUT_DIR}/${case_name}"
    mkdir -p "${case_root}/captures/composite" "${case_root}/captures/water-depth" \
        "${case_root}/captures/selected" "${case_root}/logs"
    : >"${case_root}/sequence.csv"
    printf 'sequence_index,capture_frame,simulation_seconds,composite_path,water_depth_path\n' \
        >"${case_root}/sequence.csv"

    {
        printf '\n[%s]\n' "${case_name}"
        printf 'terrain_path=%s\n' "${terrain_path}"
        printf 'crop_x=%s\n' "${crop_x}"
        printf 'crop_z=%s\n' "${crop_z}"
        printf 'manifest_sha256=%s\n' "$(sha256sum "${manifest}" | awk '{print $1}')"
        printf 'elevation_sha256=%s\n' "$(sha256sum "${terrain_root}/elevation.f32" | awk '{print $1}')"
        printf 'manifest_elevation_sha256=%s\n' "$(terrain_manifest_value "${manifest}" sha256)"
    } >>"${OUT_DIR}/metadata.txt"

    sequence_index=1
    capture_frame=1
    while (( capture_frame <= TOTAL_FRAMES )); do
        composite_path="${case_root}/captures/composite/frame-$(printf '%04d' "${sequence_index}").png"
        depth_path="${case_root}/captures/water-depth/frame-$(printf '%04d' "${sequence_index}").png"
        run_capture "${case_name}" "${terrain_path}" "${crop_x}" "${crop_z}" \
            "${capture_frame}" composite "${composite_path}" \
            "${case_root}/logs/composite-f${capture_frame}.log"
        run_capture "${case_name}" "${terrain_path}" "${crop_x}" "${crop_z}" \
            "${capture_frame}" water-depth "${depth_path}" \
            "${case_root}/logs/water-depth-f${capture_frame}.log"
        printf '%d,%d,%d,%s,%s\n' "${sequence_index}" "${capture_frame}" \
            "$((capture_frame * FIXED_DELTA_SECONDS))" "${composite_path}" "${depth_path}" \
            >>"${case_root}/sequence.csv"
        sequence_index=$((sequence_index + 1))
        if (( capture_frame == 1 )); then
            capture_frame=${SEQUENCE_INTERVAL_FRAMES}
        else
            capture_frame=$((capture_frame + SEQUENCE_INTERVAL_FRAMES))
        fi
    done
    (( sequence_index - 1 == EXPECTED_SEQUENCE_FRAMES )) || {
        printf 'unexpected sequence frame count for %s: %s\n' \
            "${case_name}" "$((sequence_index - 1))" >&2
        exit 1
    }

    # The selected diagnostic captures are deliberately separate from the
    # side-by-side sequence so reviewers can compare a wet/dry mask and local
    # flow magnitude at the fixed review checkpoints.
    for selected_frame in 1 60 150 300 450 600; do
        for selected_view in wet-dry flow-magnitude; do
            selected_path="${case_root}/captures/selected/f${selected_frame}-${selected_view}.png"
            run_capture "${case_name}" "${terrain_path}" "${crop_x}" "${crop_z}" \
                "${selected_frame}" "${selected_view}" "${selected_path}" \
                "${case_root}/logs/selected-f${selected_frame}-${selected_view}.log"
        done
    done

    profile_prefix="${OUT_DIR}/profiles/${case_name}"
    profile_output="${case_root}/captures/composite/frame-0041.png"
    profile_log="${OUT_DIR}/logs/${case_name}-profile.log"
    mapfile -t profile_args < <(common_args "${terrain_path}" "${crop_x}" "${crop_z}")
    profile_args+=(
        --frames "${TOTAL_FRAMES}"
        --fluid25d-view catchment --fluid25d-catchment-view composite
        --profile-output "${profile_prefix}" --profile-diagnostics
        --profile-diagnostic-interval "${SEQUENCE_INTERVAL_FRAMES}"
        --output "${profile_output}"
    )
    run_app "${profile_log}" "${profile_args[@]}"

    profile_identity="$(sed -n 's/^fluid_25d: terrain-backed identity=\([^ ]*\).*/\1/p' "${profile_log}" | head -n 1)"
    [[ -n "${profile_identity}" ]] || {
        printf 'terrain identity was not emitted for %s\n' "${case_name}" >&2
        exit 1
    }
    printf 'terrain_runtime_identity[%s]=%s\n' "${case_name}" "${profile_identity}" >>"${OUT_DIR}/metadata.txt"

    review_path="${OUT_DIR}/review/${case_name}-overview.png"
    review_log="${OUT_DIR}/logs/${case_name}-overview.log"
    run_tool "${review_log}" /usr/bin/magick montage \
        "${case_root}/captures/composite/frame-0001.png" \
        "${case_root}/captures/composite/frame-0021.png" \
        "${case_root}/captures/composite/frame-0041.png" \
        "${case_root}/captures/water-depth/frame-0001.png" \
        "${case_root}/captures/water-depth/frame-0021.png" \
        "${case_root}/captures/water-depth/frame-0041.png" \
        -thumbnail 480x270 -tile 3x2 -geometry +4+4 -background '#101820' "${review_path}"

    video_path="${OUT_DIR}/videos/${case_name}-composite-water-depth.mp4"
    video_log="${OUT_DIR}/logs/${case_name}-video.log"
    run_tool "${video_log}" /usr/bin/ffmpeg -y -hide_banner -loglevel error \
        -framerate "${VIDEO_FPS}" -start_number 1 \
        -i "${case_root}/captures/composite/frame-%04d.png" \
        -framerate "${VIDEO_FPS}" -start_number 1 \
        -i "${case_root}/captures/water-depth/frame-%04d.png" \
        -filter_complex \
        "[0:v]scale=640:360:force_original_aspect_ratio=decrease,pad=640:360:(ow-iw)/2:(oh-ih)/2[left];[1:v]scale=640:360:force_original_aspect_ratio=decrease,pad=640:360:(ow-iw)/2:(oh-ih)/2[right];[left][right]hstack=inputs=2,format=yuv420p[v]" \
        -map '[v]' -an -c:v libx264 -preset medium -crf 18 -movflags +faststart "${video_path}"
    ffprobe_command=(
        /usr/bin/ffprobe -v error
        -show_entries 'format=duration:stream=codec_name,width,height,pix_fmt'
        -of default=noprint_wrappers=1 "${video_path}"
    )
    record_command "${ffprobe_command[@]}"
    "${ffprobe_command[@]}" \
        >"${OUT_DIR}/videos/${case_name}.ffprobe.txt" \
        2>"${OUT_DIR}/logs/${case_name}-ffprobe.log"
    ffprobe_path="${OUT_DIR}/videos/${case_name}.ffprobe.txt"
    grep -Fxq 'codec_name=h264' "${ffprobe_path}"
    grep -Fxq 'width=1280' "${ffprobe_path}"
    grep -Fxq 'height=360' "${ffprobe_path}"
    grep -Fxq 'pix_fmt=yuv420p' "${ffprobe_path}"
    grep -Fxq "duration=${EXPECTED_VIDEO_DURATION_SECONDS}" "${ffprobe_path}"
done

(( selected_cases == 3 )) || {
    printf 'rain dynamics study selected an unexpected number of cases: %s\n' "${selected_cases}" >&2
    exit 1
}

comparison_path="${OUT_DIR}/comparison.csv"
printf '%s\n' \
    'candidate,profile_frame,capture_frame,simulation_seconds,finite_volume_status_flags,wet_cell_count,wet_cell_ratio,total_water_volume_m3,maximum_depth_m,wet_mean_depth_m,active_flow_cell_count,active_flow_cell_ratio,maximum_speed_m_per_s,active_mean_speed_m_per_s,slow_pooled_wet_fraction,cumulative_source_volume_m3,cumulative_sink_volume_m3,cumulative_boundary_outflow_volume_m3,conservation_residual_m3' \
    >"${comparison_path}"
for case_name in "${CASE_NAMES[@]}"; do
    awk -F, -v candidate="${case_name}" -v delta="${FIXED_DELTA_SECONDS}" '
        NR == 1 { next }
        $2 == "fluid_25d.water" {
            frame = $1
            seen[frame] = 1
            value[frame SUBSEP $3] = $4
        }
        $2 == "fluid_25d.solver" && $3 == "finite_volume_status_flags" {
            frame = $1
            seen[frame] = 1
            value[frame SUBSEP $3] = $4
        }
        END {
            for (frame in seen) {
                printf "%s,%d,%d,%d,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n",
                    candidate, frame, frame + 1, (frame + 1) * delta,
                    value[frame SUBSEP "finite_volume_status_flags"],
                    value[frame SUBSEP "wet_cell_count"],
                    value[frame SUBSEP "wet_cell_ratio"],
                    value[frame SUBSEP "total_water_volume_m3"],
                    value[frame SUBSEP "maximum_depth_m"],
                    value[frame SUBSEP "wet_mean_depth_m"],
                    value[frame SUBSEP "active_flow_cell_count"],
                    value[frame SUBSEP "active_flow_cell_ratio"],
                    value[frame SUBSEP "maximum_speed_m_per_s"],
                    value[frame SUBSEP "active_mean_speed_m_per_s"],
                    value[frame SUBSEP "slow_pooled_wet_fraction"],
                    value[frame SUBSEP "cumulative_source_volume_m3"],
                    value[frame SUBSEP "cumulative_sink_volume_m3"],
                    value[frame SUBSEP "cumulative_boundary_outflow_volume_m3"],
                    value[frame SUBSEP "conservation_residual_m3"]
            }
        }
    ' "${OUT_DIR}/profiles/${case_name}.metrics.csv" >>"${comparison_path}.unsorted"
done
{
    head -n 1 "${comparison_path}"
    sort -t, -k1,1 -k2,2n "${comparison_path}.unsorted"
} >"${comparison_path}.sorted"
mv "${comparison_path}.sorted" "${comparison_path}"
rm -f "${comparison_path}.unsorted"

acceptance_path="${OUT_DIR}/acceptance.txt"
{
    printf 'schema=fluid_25d_rain_dynamics_study_v1_acceptance\n'
    printf 'required_candidates=3\n'
    printf 'required_sequence_frames=%s\n' "${EXPECTED_SEQUENCE_FRAMES}"
    printf 'required_profile_interval_frames=%s\n' "${SEQUENCE_INTERVAL_FRAMES}"
    printf 'max_conservation_residual_fraction_of_source=%s\n' \
        "${MAX_CONSERVATION_RESIDUAL_FRACTION_OF_SOURCE}"
    printf 'profile_endpoint_note=last profile row is capture f586 because frame indices are zero-based and 599 is not divisible by 15; f600 is separately captured and final status-checked by the app\n'
    all_status_zero=1
    all_profiles_present=1
    all_conservation_within_limit=1
    for case_name in "${CASE_NAMES[@]}"; do
        metrics="${OUT_DIR}/profiles/${case_name}.metrics.csv"
        if [[ ! -s "${metrics}" ]]; then
            printf '%s=missing_profile\n' "${case_name}"
            all_profiles_present=0
            continue
        fi
        max_status="$(awk -F, '$2 == "fluid_25d.solver" && $3 == "finite_volume_status_flags" { if ($4 > max) max = $4 } END { printf "%.0f", max + 0 }' "${metrics}")"
        max_abs_residual="$(awk -F, '$2 == "fluid_25d.water" && $3 == "conservation_residual_m3" { value = $4 < 0 ? -$4 : $4; if (value > max) max = value } END { printf "%.9f", max + 0 }' "${metrics}")"
        final_outflow="$(awk -F, '$1 == 585 && $2 == "fluid_25d.water" && $3 == "cumulative_boundary_outflow_volume_m3" { print $4 }' "${metrics}")"
        final_source="$(awk -F, '$1 == 585 && $2 == "fluid_25d.water" && $3 == "cumulative_source_volume_m3" { print $4 }' "${metrics}")"
        final_volume="$(awk -F, '$1 == 585 && $2 == "fluid_25d.water" && $3 == "total_water_volume_m3" { print $4 }' "${metrics}")"
        residual_fraction="$(awk -v residual="${max_abs_residual}" -v source="${final_source}" \
            'BEGIN { if (source <= 0) exit 1; printf "%.9f", residual / source }')"
        printf '%s_status_flags_max=%s\n' "${case_name}" "${max_status}"
        printf '%s_max_abs_conservation_residual_m3=%s\n' "${case_name}" "${max_abs_residual}"
        printf '%s_max_conservation_residual_fraction_of_source=%s\n' \
            "${case_name}" "${residual_fraction}"
        printf '%s_profile_f586_source_volume_m3=%s\n' "${case_name}" "${final_source}"
        printf '%s_profile_f586_boundary_outflow_volume_m3=%s\n' "${case_name}" "${final_outflow}"
        printf '%s_profile_f586_total_water_volume_m3=%s\n' "${case_name}" "${final_volume}"
        if [[ "${max_status}" != 0 ]]; then
            all_status_zero=0
        fi
        if ! awk -v value="${residual_fraction}" \
            -v limit="${MAX_CONSERVATION_RESIDUAL_FRACTION_OF_SOURCE}" \
            'BEGIN { exit !(value <= limit) }'; then
            all_conservation_within_limit=0
        fi
    done
    printf 'all_profile_status_flags_zero=%s\n' "${all_status_zero}"
    printf 'all_profiles_present=%s\n' "${all_profiles_present}"
    printf 'all_conservation_within_limit=%s\n' "${all_conservation_within_limit}"
    if (( all_status_zero == 0 || all_profiles_present == 0 || all_conservation_within_limit == 0 )); then
        printf 'acceptance=FAIL\n'
        exit 1
    fi
    printf 'acceptance=PASS\n'
} >"${acceptance_path}"

sha256sum "${OUT_DIR}"/*/captures/*/*.png "${OUT_DIR}/review"/*.png \
    >"${OUT_DIR}/captures.sha256"
sha256sum "${OUT_DIR}/videos"/*.mp4 >"${OUT_DIR}/videos.sha256"

if [[ "${TIER_LABEL}" == visibility-stress ]]; then
    neutral_root="${ROOT_DIR}/outputs/fluid/rain-dynamics-study-v1-neutral-20260921-final"
    neutral_comparison="${neutral_root}/comparison.csv"
    [[ -s "${neutral_comparison}" ]] || {
        printf 'visibility-stress cross-tier comparison requires neutral evidence: %s\n' \
            "${neutral_comparison}" >&2
        exit 1
    }
    cross_tier_path="${OUT_DIR}/cross-tier-comparison.csv"
    {
        printf 'tier,%s\n' "$(head -n 1 "${neutral_comparison}")"
        awk -F, -v tier=neutral 'NR > 1 { print tier "," $0 }' "${neutral_comparison}"
        awk -F, -v tier=visibility-stress 'NR > 1 { print tier "," $0 }' \
            "${OUT_DIR}/comparison.csv"
    } >"${cross_tier_path}"
    printf 'cross_tier_comparison=%s\n' "${cross_tier_path}" >>"${OUT_DIR}/metadata.txt"
fi

printf 'fluid 2.5D rain dynamics study wrote %s\n' "${OUT_DIR}"
