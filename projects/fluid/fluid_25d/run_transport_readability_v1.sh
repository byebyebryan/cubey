#!/usr/bin/env bash
set -euo pipefail

# Reproducible Transport Inspection evidence runner. The run is intentionally
# one deterministic finite-volume source/outlet trace: the video, profile
# rows, and semantic checkpoint images all come from the same 900-frame app
# invocation. This runner does not select terrain, alter solver defaults, or
# promote the dye experiment into the product default.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/fluid/fluid_25d/fluid_25d}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/fluid/transport-readability-v1-$(date +%Y%m%d-%H%M%S)}"

WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
GRID_WIDTH=128
GRID_HEIGHT=64
CELL_SIZE_M=1
FRAMES=900
FPS=30
FIXED_DELTA_SECONDS=1
SUBSTEPS="${SUBSTEPS:-16}"
DYE_START_SECONDS=300
DYE_DURATION_SECONDS=60
DYE_SOURCE_CONCENTRATION=1.0
DYE_MATERIAL_REPORTING_THRESHOLD=0.01
SOURCE_RATE_M3_PER_S=0.03
SOURCE_X=14
SOURCE_Y=32
OUTLET_X=110
OUTLET_Y=32

PROFILE_INTERVAL=1
MAX_CONSERVATION_RESIDUAL_FRACTION=0.0001
BOUNDARY_TRACER_TOLERANCE_M3=0.000000001
PRE_DYE_TRACER_TOLERANCE_M3=0.000000001
SOURCE_RELATIVE_TOLERANCE=0.01

VIDEO_PATH="${OUT_DIR}/transport-inspection.mp4"
FFPROBE_PATH="${OUT_DIR}/transport-inspection.ffprobe.txt"
PROFILE_PREFIX="${OUT_DIR}/profile/transport-readability"
NORMALIZED_PROFILE="${OUT_DIR}/transport-profile.csv"
ACCEPTANCE_PATH="${OUT_DIR}/acceptance.txt"
METADATA_PATH="${OUT_DIR}/metadata.txt"
COMMAND_LOG="${OUT_DIR}/commands.txt"
CHECKPOINT_MANIFEST="${OUT_DIR}/checkpoints.csv"
CONTACT_SHEET="${OUT_DIR}/review/contact-sheet.png"

is_positive_integer() {
    [[ "$1" =~ ^[1-9][0-9]*$ ]]
}

is_positive_decimal() {
    [[ "$1" =~ ^([0-9]+([.][0-9]*)?|[.][0-9]+)$ ]] &&
        awk -v value="$1" 'BEGIN { exit !(value > 0.0) }'
}

[[ -x "${APP}" ]] || {
    printf 'fluid 2.5D app is missing or not executable: %s\n' "${APP}" >&2
    exit 1
}
[[ "${WIDTH}" =~ ^[1-9][0-9]*$ && "${HEIGHT}" =~ ^[1-9][0-9]*$ ]] || {
    printf 'WIDTH and HEIGHT must be positive integers\n' >&2
    exit 1
}
is_positive_integer "${SUBSTEPS}" && (( SUBSTEPS <= 64 )) || {
    printf 'SUBSTEPS must be an integer in 1..64: %s\n' "${SUBSTEPS}" >&2
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
is_positive_decimal "${FIXED_DELTA_SECONDS}" || {
    printf 'FIXED_DELTA_SECONDS must be a positive decimal number\n' >&2
    exit 1
}

[[ ! -e "${OUT_DIR}" ]] || {
    printf 'refusing to reuse pre-existing transport evidence directory: %s\n' "${OUT_DIR}" >&2
    exit 1
}

mkdir -p "${OUT_DIR}/profile" "${OUT_DIR}/checkpoints" "${OUT_DIR}/review" "${OUT_DIR}/logs"
: >"${COMMAND_LOG}"

record_command() {
    local value
    for value in "$@"; do
        printf '%q ' "${value}" >>"${COMMAND_LOG}"
    done
    printf '\n' >>"${COMMAND_LOG}"
}

run_logged() {
    local log_path="$1"
    shift
    record_command "$@"
    "$@" >"${log_path}" 2>&1
}

APP_ARGS=(
    --headless --capture video --frames "${FRAMES}" --fps "${FPS}"
    --width "${WIDTH}" --height "${HEIGHT}"
    --grid-width "${GRID_WIDTH}" --grid-height "${GRID_HEIGHT}"
    --fluid25d-scenario source-outlet-demo
    --fluid25d-solver finite-volume
    --fluid25d-catchment-view transport-inspection
    --fluid25d-fixed-delta-seconds "${FIXED_DELTA_SECONDS}"
    --fluid25d-substeps "${SUBSTEPS}"
    --fluid25d-dye-pulse-start-seconds "${DYE_START_SECONDS}"
    --fluid25d-dye-pulse-duration-seconds "${DYE_DURATION_SECONDS}"
    --profile-output "${PROFILE_PREFIX}"
    --profile-diagnostics --profile-diagnostic-interval "${PROFILE_INTERVAL}"
    --output "${VIDEO_PATH}"
)

run_logged "${OUT_DIR}/logs/app.log" "${APP}" "${APP_ARGS[@]}"

[[ -s "${VIDEO_PATH}" ]] || {
    printf 'fluid 2.5D transport video was not written: %s\n' "${VIDEO_PATH}" >&2
    exit 1
}
[[ -s "${PROFILE_PREFIX}.metrics.csv" ]] || {
    printf 'fluid 2.5D transport profile metrics were not written\n' >&2
    exit 1
}

ffprobe_command=(
    /usr/bin/ffprobe -v error -count_frames -select_streams v:0
    -show_entries 'stream=codec_name,width,height,pix_fmt,r_frame_rate,nb_read_frames:format=duration'
    -of default=noprint_wrappers=1 "${VIDEO_PATH}"
)
record_command "${ffprobe_command[@]}"
"${ffprobe_command[@]}" >"${FFPROBE_PATH}" 2>"${OUT_DIR}/logs/ffprobe.log"

grep -Fxq 'codec_name=h264' "${FFPROBE_PATH}"
grep -Fxq "width=${WIDTH}" "${FFPROBE_PATH}"
grep -Fxq "height=${HEIGHT}" "${FFPROBE_PATH}"
grep -Fxq 'pix_fmt=yuv420p' "${FFPROBE_PATH}"
grep -Fxq "r_frame_rate=${FPS}/1" "${FFPROBE_PATH}"
grep -Fxq "nb_read_frames=${FRAMES}" "${FFPROBE_PATH}"

VIDEO_CONTAINER_DURATION_SECONDS="$(awk -F= '$1 == "duration" { print $2; exit }' "${FFPROBE_PATH}")"
VIDEO_NOMINAL_DURATION_SECONDS="$(awk -v f="${FRAMES}" -v fps="${FPS}" 'BEGIN { printf "%.6f", f / fps }')"
VIDEO_FRAME_DURATION_SECONDS="$(awk -v fps="${FPS}" 'BEGIN { printf "%.9f", 1.0 / fps }')"
awk -v actual="${VIDEO_CONTAINER_DURATION_SECONDS}" \
    -v expected="${VIDEO_NOMINAL_DURATION_SECONDS}" \
    -v frame_duration="${VIDEO_FRAME_DURATION_SECONDS}" \
    'BEGIN {
        difference = actual - expected
        if (difference < 0.0) difference = -difference
        # H.264 container timestamps commonly end one frame before the
        # nominal frame_count / fps duration. Frame count and frame rate are
        # exact gates above; this duration gate accepts that standard interval.
        exit !(difference <= frame_duration + 0.000001)
    }'

# The app's profile recorder uses zero-based profile frames. Each profile row
# below is the state after one fixed step, so capture frame N maps to profile
# row N-1 and simulation time N*fixed_delta_seconds.
awk -F, -v expected_frames="${FRAMES}" -v delta="${FIXED_DELTA_SECONDS}" '
    BEGIN {
        required_count = 0
        required[++required_count] = "finite_volume_status_flags"
        required[++required_count] = "water_source"
        required[++required_count] = "water_sink"
        required[++required_count] = "water_boundary"
        required[++required_count] = "water_residual"
        required[++required_count] = "water_volume"
        required[++required_count] = "tracer_total"
        required[++required_count] = "tracer_maximum_concentration"
        required[++required_count] = "tracer_mean_concentration"
        required[++required_count] = "tracer_dyed_cell_count"
        required[++required_count] = "tracer_dyed_cell_ratio"
        required[++required_count] = "tracer_centroid_x"
        required[++required_count] = "tracer_centroid_y"
        required[++required_count] = "tracer_extent_x"
        required[++required_count] = "tracer_sink_region"
        required[++required_count] = "tracer_source"
        required[++required_count] = "tracer_sink"
        required[++required_count] = "tracer_boundary"
        required[++required_count] = "tracer_residual"
        print "profile_frame,capture_frame,simulation_seconds,finite_volume_status_flags,water_cumulative_source_volume_m3,water_cumulative_sink_volume_m3,water_cumulative_boundary_outflow_volume_m3,water_conservation_residual_m3,water_total_water_volume_m3,tracer_total_amount_m3,tracer_maximum_concentration,tracer_mean_concentration,tracer_dyed_wet_cell_count,tracer_dyed_wet_cell_ratio,tracer_amount_weighted_centroid_cell_x,tracer_amount_weighted_centroid_cell_y,tracer_downstream_extent_cell_x,tracer_in_explicit_sink_region_m3,tracer_cumulative_source_amount_m3,tracer_cumulative_sink_amount_m3,tracer_cumulative_boundary_outflow_amount_m3,tracer_conservation_residual_m3"
    }
    NR == 1 { next }
    NF < 4 { malformed = 1; next }
    {
        frame = $1 + 0
        span = $2
        name = $3
        value = $4
        seen[frame] = 1
        if (span == "fluid_25d.solver" && name == "finite_volume_status_flags") {
            metric[frame SUBSEP "finite_volume_status_flags"] = value
            have[frame SUBSEP "finite_volume_status_flags"] = 1
        }
        if (span == "fluid_25d.water" && name == "cumulative_source_volume_m3") {
            metric[frame SUBSEP "water_source"] = value
            have[frame SUBSEP "water_source"] = 1
        }
        if (span == "fluid_25d.water" && name == "cumulative_sink_volume_m3") {
            metric[frame SUBSEP "water_sink"] = value
            have[frame SUBSEP "water_sink"] = 1
        }
        if (span == "fluid_25d.water" && name == "cumulative_boundary_outflow_volume_m3") {
            metric[frame SUBSEP "water_boundary"] = value
            have[frame SUBSEP "water_boundary"] = 1
        }
        if (span == "fluid_25d.water" && name == "conservation_residual_m3") {
            metric[frame SUBSEP "water_residual"] = value
            have[frame SUBSEP "water_residual"] = 1
        }
        if (span == "fluid_25d.water" && name == "total_water_volume_m3") {
            metric[frame SUBSEP "water_volume"] = value
            have[frame SUBSEP "water_volume"] = 1
        }
        if (span == "fluid_25d.tracer" && name == "total_tracer_amount_m3") {
            metric[frame SUBSEP "tracer_total"] = value
            have[frame SUBSEP "tracer_total"] = 1
        }
        if (span == "fluid_25d.tracer" && name == "maximum_concentration") {
            metric[frame SUBSEP "tracer_maximum_concentration"] = value
            have[frame SUBSEP "tracer_maximum_concentration"] = 1
        }
        if (span == "fluid_25d.tracer" && name == "mean_concentration") {
            metric[frame SUBSEP "tracer_mean_concentration"] = value
            have[frame SUBSEP "tracer_mean_concentration"] = 1
        }
        if (span == "fluid_25d.tracer" && name == "dyed_wet_cell_count") {
            metric[frame SUBSEP "tracer_dyed_cell_count"] = value
            have[frame SUBSEP "tracer_dyed_cell_count"] = 1
        }
        if (span == "fluid_25d.tracer" && name == "dyed_wet_cell_ratio") {
            metric[frame SUBSEP "tracer_dyed_cell_ratio"] = value
            have[frame SUBSEP "tracer_dyed_cell_ratio"] = 1
        }
        if (span == "fluid_25d.tracer" && name == "amount_weighted_centroid_cell_x") {
            metric[frame SUBSEP "tracer_centroid_x"] = value
            have[frame SUBSEP "tracer_centroid_x"] = 1
        }
        if (span == "fluid_25d.tracer" && name == "amount_weighted_centroid_cell_y") {
            metric[frame SUBSEP "tracer_centroid_y"] = value
            have[frame SUBSEP "tracer_centroid_y"] = 1
        }
        if (span == "fluid_25d.tracer" && name == "downstream_extent_cell_x") {
            metric[frame SUBSEP "tracer_extent_x"] = value
            have[frame SUBSEP "tracer_extent_x"] = 1
        }
        if (span == "fluid_25d.tracer" && name == "tracer_in_explicit_sink_region_m3") {
            metric[frame SUBSEP "tracer_sink_region"] = value
            have[frame SUBSEP "tracer_sink_region"] = 1
        }
        if (span == "fluid_25d.tracer" && name == "cumulative_source_amount_m3") {
            metric[frame SUBSEP "tracer_source"] = value
            have[frame SUBSEP "tracer_source"] = 1
        }
        if (span == "fluid_25d.tracer" && name == "cumulative_sink_amount_m3") {
            metric[frame SUBSEP "tracer_sink"] = value
            have[frame SUBSEP "tracer_sink"] = 1
        }
        if (span == "fluid_25d.tracer" && name == "cumulative_boundary_outflow_amount_m3") {
            metric[frame SUBSEP "tracer_boundary"] = value
            have[frame SUBSEP "tracer_boundary"] = 1
        }
        if (span == "fluid_25d.tracer" && name == "conservation_residual_m3") {
            metric[frame SUBSEP "tracer_residual"] = value
            have[frame SUBSEP "tracer_residual"] = 1
        }
    }
    END {
        missing = malformed
        for (frame = 0; frame < expected_frames; ++frame) {
            if (!(frame in seen)) {
                missing = 1
            }
            for (index = 1; index <= required_count; ++index) {
                if (!(frame SUBSEP required[index] in have)) {
                    missing = 1
                }
            }
            printf "%d,%d,%.9f", frame, frame + 1, (frame + 1) * delta
            printf ",%s,%s,%s,%s,%s,%s", metric[frame SUBSEP "finite_volume_status_flags"], metric[frame SUBSEP "water_source"], metric[frame SUBSEP "water_sink"], metric[frame SUBSEP "water_boundary"], metric[frame SUBSEP "water_residual"], metric[frame SUBSEP "water_volume"]
            printf ",%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s", metric[frame SUBSEP "tracer_total"], metric[frame SUBSEP "tracer_maximum_concentration"], metric[frame SUBSEP "tracer_mean_concentration"], metric[frame SUBSEP "tracer_dyed_cell_count"], metric[frame SUBSEP "tracer_dyed_cell_ratio"], metric[frame SUBSEP "tracer_centroid_x"], metric[frame SUBSEP "tracer_centroid_y"], metric[frame SUBSEP "tracer_extent_x"], metric[frame SUBSEP "tracer_sink_region"], metric[frame SUBSEP "tracer_source"], metric[frame SUBSEP "tracer_sink"], metric[frame SUBSEP "tracer_boundary"], metric[frame SUBSEP "tracer_residual"]
            printf "\n"
        }
        if (missing) {
            exit 1
        }
    }
' "${PROFILE_PREFIX}.metrics.csv" >"${NORMALIZED_PROFILE}"

profile_value() {
    local frame="$1"
    local column="$2"
    awk -F, -v wanted="${frame}" -v column="${column}" \
        'NR > 1 && $1 == wanted { print $column; exit }' "${NORMALIZED_PROFILE}"
}

FINAL_PROFILE_FRAME=$((FRAMES - 1))
FINAL_WATER_SOURCE="$(profile_value "${FINAL_PROFILE_FRAME}" 5)"
FINAL_TRACER_TOTAL="$(profile_value "${FINAL_PROFILE_FRAME}" 10)"
FINAL_TRACER_SOURCE="$(profile_value "${FINAL_PROFILE_FRAME}" 19)"
FINAL_TRACER_SINK="$(profile_value "${FINAL_PROFILE_FRAME}" 20)"
FINAL_TRACER_BOUNDARY="$(profile_value "${FINAL_PROFILE_FRAME}" 21)"
FINAL_TRACER_CENTROID_X="$(profile_value "${FINAL_PROFILE_FRAME}" 15)"
FINAL_TRACER_CENTROID_Y="$(profile_value "${FINAL_PROFILE_FRAME}" 16)"
FINAL_TRACER_EXTENT_X="$(profile_value "${FINAL_PROFILE_FRAME}" 17)"
FINAL_TRACER_SINK_REGION="$(profile_value "${FINAL_PROFILE_FRAME}" 18)"
FINAL_TRACER_RESIDUAL="$(profile_value "${FINAL_PROFILE_FRAME}" 22)"
TRACER_ARRIVAL_THRESHOLD="$(awk -v source="${FINAL_TRACER_SOURCE}" \
    'BEGIN { threshold = source * 0.000001; if (threshold < 0.000001) threshold = 0.000001; printf "%.12f", threshold }')"

PRE_DYE_CAPTURE_FRAME="$(awk -v start="${DYE_START_SECONDS}" -v delta="${FIXED_DELTA_SECONDS}" \
    'BEGIN { printf "%.0f", start / delta }')"
PRE_DYE_PROFILE_FRAME=$((PRE_DYE_CAPTURE_FRAME - 1))
PRE_DYE_TRACER_TOTAL="$(profile_value "${PRE_DYE_PROFILE_FRAME}" 10)"
PRE_DYE_TRACER_SOURCE="$(profile_value "${PRE_DYE_PROFILE_FRAME}" 19)"
PRE_DYE_TRACER_SINK="$(profile_value "${PRE_DYE_PROFILE_FRAME}" 20)"
PRE_DYE_TRACER_BOUNDARY="$(profile_value "${PRE_DYE_PROFILE_FRAME}" 21)"

ARRIVAL_LINE="$(awk -F, -v threshold="${TRACER_ARRIVAL_THRESHOLD}" \
    'NR > 1 && $20 > threshold { printf "%s %s %s %s", $1, $2, $3, $20; exit }' \
    "${NORMALIZED_PROFILE}")"
ARRIVAL_PROFILE_FRAME=none
ARRIVAL_CAPTURE_FRAME=none
ARRIVAL_SECONDS=none
ARRIVAL_SINK_AMOUNT=none
if [[ -n "${ARRIVAL_LINE}" ]]; then
    read -r ARRIVAL_PROFILE_FRAME ARRIVAL_CAPTURE_FRAME ARRIVAL_SECONDS ARRIVAL_SINK_AMOUNT <<<"${ARRIVAL_LINE}"
fi

MAX_STATUS="$(awk -F, 'NR > 1 { if ($4 + 0 > max) max = $4 + 0 } END { printf "%.0f", max + 0 }' "${NORMALIZED_PROFILE}")"
MIN_CONCENTRATION="$(awk -F, 'NR > 1 { value = $11 + 0; if (!seen || value < min) min = value; seen = 1 } END { printf "%.9f", min + 0 }' "${NORMALIZED_PROFILE}")"
MAX_CONCENTRATION="$(awk -F, 'NR > 1 { value = $11 + 0; if (!seen || value > max) max = value; seen = 1 } END { printf "%.9f", max + 0 }' "${NORMALIZED_PROFILE}")"
MAX_TRACER_BOUNDARY="$(awk -F, 'NR > 1 { value = $21 + 0; if (value < 0) value = -value; if (value > max) max = value } END { printf "%.12f", max + 0 }' "${NORMALIZED_PROFILE}")"
MAX_ABS_TRACER_RESIDUAL="$(awk -F, 'NR > 1 { value = $22 + 0; if (value < 0) value = -value; if (value > max) max = value } END { printf "%.12f", max + 0 }' "${NORMALIZED_PROFILE}")"
MAX_ABS_WATER_RESIDUAL="$(awk -F, 'NR > 1 { value = $8 + 0; if (value < 0) value = -value; if (value > max) max = value } END { printf "%.12f", max + 0 }' "${NORMALIZED_PROFILE}")"
TRACER_RESIDUAL_FRACTION="$(awk -v residual="${MAX_ABS_TRACER_RESIDUAL}" -v source="${FINAL_TRACER_SOURCE}" \
    'BEGIN { if (source > 0.0) printf "%.12f", residual / source; else print "inf" }')"
EXPECTED_WATER_SOURCE="$(awk -v rate="${SOURCE_RATE_M3_PER_S}" -v frames="${FRAMES}" -v delta="${FIXED_DELTA_SECONDS}" \
    'BEGIN { printf "%.9f", rate * frames * delta }')"
EXPECTED_TRACER_SOURCE="$(awk -v rate="${SOURCE_RATE_M3_PER_S}" -v concentration="${DYE_SOURCE_CONCENTRATION}" -v duration="${DYE_DURATION_SECONDS}" \
    'BEGIN { printf "%.9f", rate * concentration * duration }')"

rows_ok=1
row_count="$(awk 'END { print NR - 1 }' "${NORMALIZED_PROFILE}")"
[[ "${row_count}" == "${FRAMES}" ]] || rows_ok=0

water_source_ok=1
if ! awk -v actual="${FINAL_WATER_SOURCE}" -v expected="${EXPECTED_WATER_SOURCE}" -v tolerance="${SOURCE_RELATIVE_TOLERANCE}" \
    'BEGIN { difference = actual - expected; if (difference < 0.0) difference = -difference; exit !(difference <= expected * tolerance) }'; then
    water_source_ok=0
fi
tracer_source_ok=1
if ! awk -v actual="${FINAL_TRACER_SOURCE}" -v expected="${EXPECTED_TRACER_SOURCE}" -v tolerance="${SOURCE_RELATIVE_TOLERANCE}" \
    'BEGIN { difference = actual - expected; if (difference < 0.0) difference = -difference; exit !(difference <= expected * tolerance) }'; then
    tracer_source_ok=0
fi
status_ok=1
[[ "${MAX_STATUS}" == "0" ]] || status_ok=0
concentration_ok=1
if ! awk -v minimum="${MIN_CONCENTRATION}" -v maximum="${MAX_CONCENTRATION}" \
    'BEGIN { exit !(minimum >= -0.000001 && maximum <= 1.000001) }'; then
    concentration_ok=0
fi
boundary_tracer_ok=1
if ! awk -v value="${MAX_TRACER_BOUNDARY}" -v tolerance="${BOUNDARY_TRACER_TOLERANCE_M3}" \
    'BEGIN { exit !(value <= tolerance) }'; then
    boundary_tracer_ok=0
fi
pre_dye_ok=1
if ! awk -v total="${PRE_DYE_TRACER_TOTAL}" -v source="${PRE_DYE_TRACER_SOURCE}" \
    -v sink="${PRE_DYE_TRACER_SINK}" -v boundary="${PRE_DYE_TRACER_BOUNDARY}" \
    -v tolerance="${PRE_DYE_TRACER_TOLERANCE_M3}" \
    'BEGIN {
        if (total < 0.0) total = -total
        if (source < 0.0) source = -source
        if (sink < 0.0) sink = -sink
        if (boundary < 0.0) boundary = -boundary
        exit !(total <= tolerance && source <= tolerance && sink <= tolerance && boundary <= tolerance)
    }'; then
    pre_dye_ok=0
fi
conservation_ok=1
if ! awk -v fraction="${TRACER_RESIDUAL_FRACTION}" -v limit="${MAX_CONSERVATION_RESIDUAL_FRACTION}" \
    'BEGIN { exit !(fraction <= limit) }'; then
    conservation_ok=0
fi
arrival_ok=1
if [[ "${ARRIVAL_CAPTURE_FRAME}" == none ]] ||
    ! awk -v frame="${ARRIVAL_CAPTURE_FRAME}" -v start="${DYE_START_SECONDS}" -v total="${FRAMES}" \
        'BEGIN { exit !(frame > start && frame <= total) }'; then
    arrival_ok=0
fi

acceptance_ok=1
for result in "${rows_ok}" "${water_source_ok}" "${tracer_source_ok}" "${status_ok}" \
    "${concentration_ok}" "${boundary_tracer_ok}" "${pre_dye_ok}" \
    "${conservation_ok}" "${arrival_ok}"; do
    [[ "${result}" == 1 ]] || acceptance_ok=0
done

{
    printf 'schema=fluid_25d_transport_readability_v1_acceptance\n'
    printf 'required_profile_rows=%s\n' "${FRAMES}"
    printf 'observed_profile_rows=%s\n' "${row_count}"
    printf 'all_profile_rows_present=%s\n' "${rows_ok}"
    printf 'finite_volume_status_flags_max=%s\n' "${MAX_STATUS}"
    printf 'all_status_flags_zero=%s\n' "${status_ok}"
    printf 'concentration_minimum=%s\n' "${MIN_CONCENTRATION}"
    printf 'concentration_maximum=%s\n' "${MAX_CONCENTRATION}"
    printf 'concentration_range_0_to_1=%s\n' "${concentration_ok}"
    printf 'expected_water_source_volume_m3=%s\n' "${EXPECTED_WATER_SOURCE}"
    printf 'final_water_source_volume_m3=%s\n' "${FINAL_WATER_SOURCE}"
    printf 'water_source_relative_tolerance=%s\n' "${SOURCE_RELATIVE_TOLERANCE}"
    printf 'water_source_amount_acceptance=%s\n' "${water_source_ok}"
    printf 'expected_tracer_source_amount_m3=%s\n' "${EXPECTED_TRACER_SOURCE}"
    printf 'final_tracer_source_amount_m3=%s\n' "${FINAL_TRACER_SOURCE}"
    printf 'tracer_source_amount_acceptance=%s\n' "${tracer_source_ok}"
    printf 'maximum_cumulative_tracer_boundary_outflow_m3=%s\n' "${MAX_TRACER_BOUNDARY}"
    printf 'boundary_tracer_tolerance_m3=%s\n' "${BOUNDARY_TRACER_TOLERANCE_M3}"
    printf 'boundary_tracer_acceptance=%s\n' "${boundary_tracer_ok}"
    printf 'pre_dye_capture_frame=%s\n' "${PRE_DYE_CAPTURE_FRAME}"
    printf 'pre_dye_profile_frame=%s\n' "${PRE_DYE_PROFILE_FRAME}"
    printf 'pre_dye_tracer_total_amount_m3=%s\n' "${PRE_DYE_TRACER_TOTAL}"
    printf 'pre_dye_tracer_source_amount_m3=%s\n' "${PRE_DYE_TRACER_SOURCE}"
    printf 'pre_dye_tracer_sink_amount_m3=%s\n' "${PRE_DYE_TRACER_SINK}"
    printf 'pre_dye_tracer_boundary_amount_m3=%s\n' "${PRE_DYE_TRACER_BOUNDARY}"
    printf 'pre_dye_tracer_tolerance_m3=%s\n' "${PRE_DYE_TRACER_TOLERANCE_M3}"
    printf 'pre_dye_zero_acceptance=%s\n' "${pre_dye_ok}"
    printf 'final_tracer_total_amount_m3=%s\n' "${FINAL_TRACER_TOTAL}"
    printf 'maximum_absolute_tracer_conservation_residual_m3=%s\n' "${MAX_ABS_TRACER_RESIDUAL}"
    printf 'final_tracer_conservation_residual_m3=%s\n' "${FINAL_TRACER_RESIDUAL}"
    printf 'final_tracer_sink_amount_m3=%s\n' "${FINAL_TRACER_SINK}"
    printf 'final_tracer_boundary_outflow_amount_m3=%s\n' "${FINAL_TRACER_BOUNDARY}"
    printf 'final_tracer_centroid_cell_x=%s\n' "${FINAL_TRACER_CENTROID_X}"
    printf 'final_tracer_centroid_cell_y=%s\n' "${FINAL_TRACER_CENTROID_Y}"
    printf 'final_tracer_downstream_extent_cell_x=%s\n' "${FINAL_TRACER_EXTENT_X}"
    printf 'final_tracer_in_explicit_sink_region_m3=%s\n' "${FINAL_TRACER_SINK_REGION}"
    printf 'tracer_conservation_residual_fraction_of_final_source=%s\n' "${TRACER_RESIDUAL_FRACTION}"
    printf 'maximum_absolute_water_conservation_residual_m3=%s\n' "${MAX_ABS_WATER_RESIDUAL}"
    printf 'maximum_conservation_residual_fraction=%s\n' "${MAX_CONSERVATION_RESIDUAL_FRACTION}"
    printf 'tracer_conservation_acceptance=%s\n' "${conservation_ok}"
    printf 'material_outlet_arrival_threshold_m3=%s\n' "${TRACER_ARRIVAL_THRESHOLD}"
    printf 'first_material_outlet_arrival_profile_frame=%s\n' "${ARRIVAL_PROFILE_FRAME}"
    printf 'first_material_outlet_arrival_capture_frame=%s\n' "${ARRIVAL_CAPTURE_FRAME}"
    printf 'first_material_outlet_arrival_simulation_seconds=%s\n' "${ARRIVAL_SECONDS}"
    printf 'first_material_outlet_arrival_sink_amount_m3=%s\n' "${ARRIVAL_SINK_AMOUNT}"
    printf 'first_material_outlet_arrival_after_dye_start=%s\n' "${arrival_ok}"
    printf 'acceptance=%s\n' "$([[ "${acceptance_ok}" == 1 ]] && printf PASS || printf FAIL)"
} >"${ACCEPTANCE_PATH}"

[[ "${acceptance_ok}" == 1 ]] || {
    printf 'fluid 2.5D transport readability acceptance failed; inspect %s\n' "${ACCEPTANCE_PATH}" >&2
    exit 1
}

declare -a CHECKPOINT_FRAMES=(1 300 301 360 361 480 540 660 900)
if [[ "${ARRIVAL_CAPTURE_FRAME}" != none ]]; then
    checkpoint_already_present=0
    for frame in "${CHECKPOINT_FRAMES[@]}"; do
        if [[ "${frame}" == "${ARRIVAL_CAPTURE_FRAME}" ]]; then
            checkpoint_already_present=1
        fi
    done
    [[ "${checkpoint_already_present}" == 1 ]] || CHECKPOINT_FRAMES+=("${ARRIVAL_CAPTURE_FRAME}")
fi

{
    printf 'capture_frame,decoded_video_index,simulation_seconds,path\n'
    for frame in "${CHECKPOINT_FRAMES[@]}"; do
        decoded_index=$((frame - 1))
        output_path="${OUT_DIR}/checkpoints/frame-$(printf '%04d' "${frame}").png"
        run_logged "${OUT_DIR}/logs/checkpoint-f${frame}.log" /usr/bin/ffmpeg -y -hide_banner -loglevel error \
            -i "${VIDEO_PATH}" -vf "select=eq(n\\,${decoded_index})" -frames:v 1 -vsync 0 \
            "${output_path}"
        [[ -s "${output_path}" ]] || {
            printf 'checkpoint extraction failed for capture frame %s\n' "${frame}" >&2
            exit 1
        }
        printf '%s,%s,%.9f,%s\n' "${frame}" "${decoded_index}" \
            "$(awk -v frame="${frame}" -v delta="${FIXED_DELTA_SECONDS}" 'BEGIN { printf "%.9f", frame * delta }')" \
            "${output_path}"
    done
} >"${CHECKPOINT_MANIFEST}"

checkpoint_paths=()
for frame in "${CHECKPOINT_FRAMES[@]}"; do
    checkpoint_paths+=("${OUT_DIR}/checkpoints/frame-$(printf '%04d' "${frame}").png")
done
run_logged "${OUT_DIR}/logs/contact-sheet.log" /usr/bin/magick montage "${checkpoint_paths[@]}" \
    -thumbnail 320x180 -tile 3x -geometry +8+28 -background '#101820' -fill white \
    -pointsize 18 -label '%t' "${CONTACT_SHEET}"
[[ -s "${CONTACT_SHEET}" ]] || {
    printf 'transport contact sheet was not written\n' >&2
    exit 1
}

GPU_INFO=unavailable
if command -v nvidia-smi >/dev/null 2>&1; then
    GPU_INFO="$(nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null | head -n 1 || printf unavailable)"
fi
{
    printf 'schema=fluid_25d_transport_readability_v1\n'
    printf 'status=complete/current-invocation\n'
    printf 'protocol_version=transport-readability-v1\n'
    printf 'runner=%s\n' "${BASH_SOURCE[0]}"
    printf 'runner_sha256=%s\n' "$(sha256sum "${BASH_SOURCE[0]}" | awk '{print $1}')"
    printf 'app=%s\n' "${APP}"
    printf 'app_sha256=%s\n' "$(sha256sum "${APP}" | awk '{print $1}')"
    printf 'cubey_head=%s\n' "$(git -C "${ROOT_DIR}" rev-parse HEAD)"
    printf 'cubey_branch=%s\n' "$(git -C "${ROOT_DIR}" branch --show-current)"
    printf 'cubey_worktree=%s changed_paths\n' "$(git -C "${ROOT_DIR}" status --porcelain | wc -l | tr -d ' ')"
    printf 'host=%s\n' "$(hostname)"
    printf 'gpu=%s\n' "${GPU_INFO}"
    printf 'ffmpeg_version=%s\n' "$(/usr/bin/ffmpeg -version | head -n 1)"
    printf 'grid=%sx%s cell_size_m=%s\n' "${GRID_WIDTH}" "${GRID_HEIGHT}" "${CELL_SIZE_M}"
    printf 'scenario=source-outlet-demo\n'
    printf 'solver=finite-volume\n'
    printf 'catchment_view=transport-inspection\n'
    printf 'outer_boundary=closed\n'
    printf 'fixed_delta_seconds=%s\n' "${FIXED_DELTA_SECONDS}"
    printf 'substeps=%s\n' "${SUBSTEPS}"
    printf 'substep_seconds=%s\n' "$(awk -v delta="${FIXED_DELTA_SECONDS}" -v steps="${SUBSTEPS}" 'BEGIN { printf "%.9f", delta / steps }')"
    printf 'frames=%s\n' "${FRAMES}"
    printf 'fps=%s\n' "${FPS}"
    printf 'video_nominal_presentation_duration_seconds=%s\n' "${VIDEO_NOMINAL_DURATION_SECONDS}"
    printf 'video_container_duration_seconds=%s\n' "${VIDEO_CONTAINER_DURATION_SECONDS}"
    printf 'video_frame_duration_seconds=%s\n' "${VIDEO_FRAME_DURATION_SECONDS}"
    printf 'full_args=%q ' "${APP}" "${APP_ARGS[@]}"
    printf '\n'
    printf 'source_centroid_cell=%s,%s\n' "${SOURCE_X}" "${SOURCE_Y}"
    printf 'outlet_centroid_cell=%s,%s\n' "${OUTLET_X}" "${OUTLET_Y}"
    printf 'source_rate_m3_per_s=%s\n' "${SOURCE_RATE_M3_PER_S}"
    printf 'outlet_rate_capacity_m3_per_s=%s\n' "${SOURCE_RATE_M3_PER_S}"
    printf 'dye_source_concentration=%s\n' "${DYE_SOURCE_CONCENTRATION}"
    printf 'dye_material_reporting_threshold=%s\n' "${DYE_MATERIAL_REPORTING_THRESHOLD}"
    printf 'dye_pulse_start_seconds=%s\n' "${DYE_START_SECONDS}"
    printf 'dye_pulse_duration_seconds=%s\n' "${DYE_DURATION_SECONDS}"
    printf 'dye_interval=[%s,%s)\n' "${DYE_START_SECONDS}" "$((DYE_START_SECONDS + DYE_DURATION_SECONDS))"
    printf 'frame_semantics=decoded_frame_N_is_post_step_capture_fN; profile_row_N_minus_1\n'
    printf 'dye_checkpoint_semantics=f300_last_pre_dye;f301_first_dyed;f360_last_dyed;f361_first_undyed\n'
    printf 'material_arrival_threshold=max(1e-6,1e-6*final_tracer_source_amount_m3)\n'
    printf 'material_arrival_capture_frame=%s\n' "${ARRIVAL_CAPTURE_FRAME}"
    printf 'material_arrival_simulation_seconds=%s\n' "${ARRIVAL_SECONDS}"
    printf 'profile_interval_frames=%s\n' "${PROFILE_INTERVAL}"
    printf 'max_conservation_residual_fraction=%s\n' "${MAX_CONSERVATION_RESIDUAL_FRACTION}"
    printf 'boundary_tracer_tolerance_m3=%s\n' "${BOUNDARY_TRACER_TOLERANCE_M3}"
    printf 'ffprobe=%s\n' "${FFPROBE_PATH}"
    printf 'acceptance=%s\n' "${ACCEPTANCE_PATH}"
} >"${METADATA_PATH}"

(
    cd "${OUT_DIR}"
    {
        sha256sum "acceptance.txt" "checkpoints.csv" "commands.txt" "metadata.txt" \
            "transport-inspection.ffprobe.txt" "transport-inspection.mp4" "transport-profile.csv"
        sha256sum profile/*
        sha256sum checkpoints/*.png review/*.png
    } >"sha256sums.txt"
    sha256sum -c "sha256sums.txt" >"sha256-verify.txt"
)

printf 'fluid 2.5D transport readability V1 wrote %s\n' "${OUT_DIR}"
