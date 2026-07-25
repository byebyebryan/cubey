#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/terrain/terrain}"
STUDY_ROOT="${STUDY_ROOT:-${ROOT_DIR}/cache/terrain/sources/v1/desert-canyon-study}"
CONTROL_ROOT="${CONTROL_ROOT:-${ROOT_DIR}/cache/terrain/sources/v1/landscape-variations}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/terrain/desert-canyon-study-v1}"
WIDTH="${WIDTH:-1600}"
HEIGHT="${HEIGHT:-900}"
FRAMES="${FRAMES:-90}"
WARMUP_FRAMES="${WARMUP_FRAMES:-30}"
FPS="${FPS:-60}"

STUDY_INDEX="${STUDY_ROOT}/study-index.json"
CONTROL_INDEX="${CONTROL_ROOT}/variation-index.json"
CAPTURE_MANIFEST="${OUT_DIR}/capture-manifest.tsv"
METRICS="${OUT_DIR}/study-metrics.tsv"
REPORT="${OUT_DIR}/study-report.json"
INDEX="${OUT_DIR}/index.md"

if [[ ! -x "${APP}" ]]; then
    printf 'terrain executable not found: %s\n' "${APP}" >&2
    printf 'Build it with: cmake --build --preset dev --target cubey_project_terrain\n' >&2
    exit 1
fi
for manifest in "${STUDY_INDEX}" "${CONTROL_INDEX}"; do
    if [[ ! -f "${manifest}" ]]; then
        printf 'terrain study input not found: %s\n' "${manifest}" >&2
        exit 1
    fi
done

mapfile -t VARIANT_RECORDS < <(
    jq -r --arg root "${STUDY_ROOT}" \
        '.variants[] | [.id, .label, .kind, (.seed | tostring),
                        ($root + "/" + .directory)] | @tsv' \
        "${STUDY_INDEX}"
)
while IFS= read -r record; do
    VARIANT_RECORDS+=("${record}")
done < <(
    jq -r --arg root "${CONTROL_ROOT}" '
        .variants[]
        | select(.id == "dry-upland" or .id == "temperate-mountain-valley")
        | [.id, .label, "control", (.seed | tostring), ($root + "/" + .directory)]
        | @tsv
    ' "${CONTROL_INDEX}"
)
if [[ "${#VARIANT_RECORDS[@]}" -ne 9 ]]; then
    printf 'expected seven candidates and two controls, found %d\n' \
        "${#VARIANT_RECORDS[@]}" >&2
    exit 1
fi

mkdir -p "${OUT_DIR}/captures" "${OUT_DIR}/profiles"
find "${OUT_DIR}/captures" -mindepth 1 -delete
find "${OUT_DIR}/profiles" -mindepth 1 -delete
find "${OUT_DIR}" -mindepth 1 -maxdepth 1 ! -name captures ! -name profiles -delete

printf 'file\tvariant\tkind\tplacement\tview\theading\tforeground_height_m\tdebug_view\n' \
    >"${CAPTURE_MANIFEST}"
printf '%s\n' \
    $'variant\tkind\tseed\tselected_placement_pass\tgeometry_hash\tcontent_hash\tdirectional_contract\tsource_focus_x_m\tsource_focus_z_m\tlocal_relief_m\tlocal_p95_slope\tmean_rock\tmean_snow\tmean_vegetation\tmean_moisture\tmean_moisture_weight\tmean_cover_weight\tcombined_mean_ms\tcombined_p50_ms\tcombined_p95_ms' \
    >"${METRICS}"

COMMON_ARGS=(
    --terrain-surface-model climate-transition
    --terrain-camera-preset backdrop
    --terrain-render-stride 3
    --terrain-backdrop-orbit-radius 100
    --terrain-backdrop-elevation 8
    --terrain-surface-detail filtered-detail
    --terrain-shadows
    --time-of-day-mode manual
    --sun-elevation 38
    --sun-azimuth -42
    --pause-time
    --no-clouds
)

capture_variant() {
    local variant="$1"
    local kind="$2"
    local seed="$3"
    local source_dir="$4"
    local name="$5"
    local heading="$6"
    local foreground_height="$7"
    local debug_view="$8"
    local output="${OUT_DIR}/captures/${variant}/${name}.png"
    local debug_args=()
    if [[ "${debug_view}" != "surface" ]]; then
        debug_args=(--debug-view "${debug_view}")
    fi

    mkdir -p "${OUT_DIR}/captures/${variant}"
    "${APP}" --headless --width "${WIDTH}" --height "${HEIGHT}" \
        --terrain-heightfield "${source_dir}" \
        --terrain-surface-fields "${source_dir}" \
        --terrain-seed "${seed}" \
        --terrain-placement raw-center \
        --terrain-foreground-height "${foreground_height}" \
        --terrain-backdrop-azimuth "${heading}" \
        "${COMMON_ARGS[@]}" "${debug_args[@]}" --output "${output}"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "${output#"${OUT_DIR}/"}" "${variant}" "${kind}" raw-center "${name}" \
        "${heading}" "${foreground_height}" "${debug_view}" >>"${CAPTURE_MANIFEST}"
}

probe_selected_placement() {
    local variant="$1"
    local kind="$2"
    local seed="$3"
    local source_dir="$4"
    local output="${OUT_DIR}/captures/${variant}/selected-500m.png"

    mkdir -p "${OUT_DIR}/captures/${variant}"
    if "${APP}" --headless --width "${WIDTH}" --height "${HEIGHT}" \
        --terrain-heightfield "${source_dir}" \
        --terrain-surface-fields "${source_dir}" \
        --terrain-seed "${seed}" \
        --terrain-placement selected \
        --terrain-foreground-height 500 \
        --terrain-backdrop-azimuth 90 \
        "${COMMON_ARGS[@]}" --output "${output}" >/dev/null; then
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "${output#"${OUT_DIR}/"}" "${variant}" "${kind}" selected \
            selected-500m 90 500 surface >>"${CAPTURE_MANIFEST}"
        printf '1'
    else
        rm -f "${output}"
        printf '0'
    fi
}

metric_last() {
    local metrics="$1"
    local category="$2"
    local name="$3"
    awk -F, -v category="${category}" -v name="${name}" \
        '$2 == category && $3 == name { value = $4 }
         END {
             if (value == "") exit 1
             printf "%.6f", value
         }' "${metrics}"
}

metric_hash() {
    local metrics="$1"
    local prefix="$2"
    local low high
    low="$(metric_last "${metrics}" terrain.backdrop "${prefix}_low32")"
    high="$(metric_last "${metrics}" terrain.backdrop "${prefix}_high32")"
    printf '0x%08x%08x' "${high%%.*}" "${low%%.*}"
}

combined_frame_stats() {
    local passes="$1"
    awk -F, '
        $2 == "gpu" && ($3 == "terrain atmosphere" || $3 == "terrain shadow" ||
                        $3 == "terrain surface" || $3 == "terrain stage proxy" ||
                        $3 == "terrain post") {
            total[$1] += $5
        }
        END { for (frame in total) print total[frame] }
    ' "${passes}" | sort -n | awk '
        { values[NR] = $1; sum += $1 }
        END {
            if (NR == 0) exit 1
            p50_index = int((NR - 1) * 0.50) + 1
            p95_index = int((NR - 1) * 0.95) + 1
            printf "%.6f %.6f %.6f", sum / NR, values[p50_index], values[p95_index]
        }
    '
}

profile_variant() {
    local variant="$1"
    local kind="$2"
    local seed="$3"
    local source_dir="$4"
    local selected_placement_pass="$5"
    local prefix="${OUT_DIR}/profiles/${variant}"
    local video="${OUT_DIR}/profiles/${variant}.mp4"

    "${APP}" --headless --capture video --frames "${FRAMES}" --fps "${FPS}" \
        --width "${WIDTH}" --height "${HEIGHT}" \
        --terrain-heightfield "${source_dir}" \
        --terrain-surface-fields "${source_dir}" \
        --terrain-seed "${seed}" \
        --terrain-placement raw-center \
        --terrain-foreground-height 500 \
        --terrain-backdrop-azimuth 90 \
        "${COMMON_ARGS[@]}" \
        --profile-output "${prefix}" \
        --profile-warmup-frames "${WARMUP_FRAMES}" \
        --output "${video}"
    rm -f "${video}"

    local metrics="${prefix}.metrics.csv"
    local passes="${prefix}.passes.csv"
    local combined_mean combined_p50 combined_p95
    read -r combined_mean combined_p50 combined_p95 \
        <<<"$(combined_frame_stats "${passes}")"
    printf '%s\t%s\t%s\t%s\t%s\t%s' \
        "${variant}" "${kind}" "${seed}" "${selected_placement_pass}" \
        "$(metric_hash "${metrics}" geometry_hash)" \
        "$(metric_hash "${metrics}" content_hash)" >>"${METRICS}"
    for metric in directional_contract source_focus_x_m source_focus_z_m local_relief_m \
        local_p95_slope; do
        printf '\t%s' "$(metric_last "${metrics}" terrain.placement "${metric}")" \
            >>"${METRICS}"
    done
    for metric in mean_rock mean_snow mean_vegetation mean_moisture; do
        printf '\t%s' "$(metric_last "${metrics}" terrain.surface "${metric}")" \
            >>"${METRICS}"
    done
    for metric in mean_moisture_weight mean_cover_weight; do
        printf '\t%s' "$(metric_last "${metrics}" terrain.climate "${metric}")" \
            >>"${METRICS}"
    done
    printf '\t%s\t%s\t%s\n' \
        "${combined_mean}" "${combined_p50}" "${combined_p95}" >>"${METRICS}"
}

for record in "${VARIANT_RECORDS[@]}"; do
    IFS=$'\t' read -r variant _label kind seed source_dir <<<"${record}"
    for manifest in heightfield.json surface-fields.json region-summary.json; do
        if [[ ! -f "${source_dir}/${manifest}" ]]; then
            printf 'terrain study source is incomplete: %s/%s\n' \
                "${source_dir}" "${manifest}" >&2
            exit 1
        fi
    done

    selected_placement_pass="$(
        probe_selected_placement "${variant}" "${kind}" "${seed}" "${source_dir}"
    )"
    capture_variant "${variant}" "${kind}" "${seed}" "${source_dir}" \
        surface-500m-heading-90 90 500 surface
    capture_variant "${variant}" "${kind}" "${seed}" "${source_dir}" \
        surface-500m-heading-270 270 500 surface
    capture_variant "${variant}" "${kind}" "${seed}" "${source_dir}" \
        clay-500m 90 500 clay
    capture_variant "${variant}" "${kind}" "${seed}" "${source_dir}" \
        surface-stress-200m 90 200 surface
    capture_variant "${variant}" "${kind}" "${seed}" "${source_dir}" \
        diagnostic-material-weights 90 500 material-weights
    profile_variant "${variant}" "${kind}" "${seed}" "${source_dir}" \
        "${selected_placement_pass}"
done

awk -F '\t' '
    NR == 1 { next }
    {
        geometry[$5] = 1
        if ($18 + 0 > 1.10 || $19 + 0 > 1.10) {
            print "terrain timing gate failed for " $1 > "/dev/stderr"
            failed = 1
        }
    }
    END {
        count = 0
        for (hash in geometry) count += 1
        if (count != 9) {
            print "expected nine unique terrain geometry hashes" > "/dev/stderr"
            failed = 1
        }
        exit failed ? 1 : 0
    }
' "${METRICS}"

jq -Rn \
    --slurpfile study "${STUDY_INDEX}" \
    --slurpfile controls "${CONTROL_INDEX}" \
    --arg git_revision "$(git -C "${ROOT_DIR}" rev-parse HEAD)" \
    --argjson width "${WIDTH}" \
    --argjson height "${HEIGHT}" '
    def typed:
        if test("^0x") then .
        elif test("^-?[0-9]+([.][0-9]+)?$") then tonumber
        else . end;
    (input | split("\t")) as $header |
    [inputs | split("\t") as $row |
        reduce range(0; $header | length) as $index
            ({}; .[$header[$index]] = ($row[$index] | typed))] as $rows |
    {
        schema: "cubey.terrain.desert-canyon-render-study.v1",
        git_revision: $git_revision,
        capture: {
            width: $width,
            height: $height,
            comparison_height_m: 500,
            stress_height_m: 200,
            headings_degrees: [90, 270],
            placement: "raw-center-with-selected-compatibility-probe",
            render_stride: 3,
            combined_timing_gate_ms: 1.10
        },
        source_study: $study[0],
        control_catalog: {
            schema: $controls[0].schema,
            variants: [
                $controls[0].variants[]
                | select(.id == "dry-upland" or
                         .id == "temperate-mountain-valley")
            ]
        },
        results: $rows
    }
' <"${METRICS}" >"${REPORT}"

montage_group() {
    local output="$1"
    local tile="$2"
    local geometry="$3"
    local files_name="$4"
    local labels_name="$5"
    declare -n files_ref="${files_name}"
    declare -n labels_ref="${labels_name}"
    local inputs=()
    for index in "${!files_ref[@]}"; do
        inputs+=(-label "${labels_ref[${index}]}" "${files_ref[${index}]}")
    done
    magick montage "${inputs[@]}" -geometry "${geometry}" -tile "${tile}" "${output}"
}

if command -v magick >/dev/null 2>&1; then
    SOURCE_FILES=()
    SOURCE_LABELS=()
    CLIMATE_FILES=()
    CLIMATE_LABELS=()
    MACRO_FILES=()
    MACRO_LABELS=()
    SHAPE_FILES=()
    SHAPE_LABELS=()
    DIAGNOSTIC_FILES=()
    DIAGNOSTIC_LABELS=()
    for record in "${VARIANT_RECORDS[@]}"; do
        IFS=$'\t' read -r variant label kind _seed source_dir <<<"${record}"
        SOURCE_FILES+=("${source_dir}/height.png" "${source_dir}/slope.png")
        SOURCE_LABELS+=("${label}: height" "${label}: slope")
        CLIMATE_FILES+=(
            "${source_dir}/climate-temperature-mean.png"
            "${source_dir}/climate-precipitation-annual.png"
        )
        CLIMATE_LABELS+=("${label}: temperature" "${label}: precipitation")
        for heading in 90 270; do
            MACRO_FILES+=(
                "${OUT_DIR}/captures/${variant}/surface-500m-heading-${heading}.png"
            )
            MACRO_LABELS+=("${label}: surface ${heading} deg")
        done
        SHAPE_FILES+=(
            "${OUT_DIR}/captures/${variant}/clay-500m.png"
            "${OUT_DIR}/captures/${variant}/surface-stress-200m.png"
        )
        SHAPE_LABELS+=("${label}: clay 500 m" "${label}: surface 200 m")
        DIAGNOSTIC_FILES+=(
            "${OUT_DIR}/captures/${variant}/diagnostic-material-weights.png"
        )
        DIAGNOSTIC_LABELS+=("${label}: ${kind} material weights")
    done
    montage_group "${OUT_DIR}/source-morphology.png" 2x9 300x300+8+26 \
        SOURCE_FILES SOURCE_LABELS
    montage_group "${OUT_DIR}/source-climate.png" 2x9 300x300+8+26 \
        CLIMATE_FILES CLIMATE_LABELS
    montage_group "${OUT_DIR}/macro-surface.png" 2x9 480x270+8+26 \
        MACRO_FILES MACRO_LABELS
    montage_group "${OUT_DIR}/shape-and-stress.png" 2x9 480x270+8+26 \
        SHAPE_FILES SHAPE_LABELS
    montage_group "${OUT_DIR}/surface-diagnostics.png" 3x3 400x225+8+26 \
        DIAGNOSTIC_FILES DIAGNOSTIC_LABELS

    PROBE_FILES=()
    PROBE_LABELS=()
    while IFS=$'\t' read -r variant label height depth; do
        PROBE_FILES+=("${STUDY_ROOT}/${height}" "${STUDY_ROOT}/${depth}")
        PROBE_LABELS+=("${label}: probe height" "${label}: valley depth")
    done < <(
        jq -r '
            .variants[]
            | [.id, .label, .probe_previews.height, .probe_previews.valley_depth]
            | @tsv
        ' "${STUDY_INDEX}"
    )
    montage_group "${OUT_DIR}/selected-probes.png" 2x7 300x300+8+26 \
        PROBE_FILES PROBE_LABELS
fi

# Backticks are intentional Markdown literals in the generated index.
# shellcheck disable=SC2016
{
    printf '# Terrain Diffusion Desert and Canyon Study\n\n'
    printf 'Start with `source-morphology.png` and `selected-probes.png`. They test '
    printf 'whether the source fields contain distinct desert relief and connected '
    printf 'incision before rendering. Canyon labels remain candidate labels.\n\n'
    printf '`macro-surface.png` compares two headings at 500 m. '
    printf '`shape-and-stress.png` separates clay morphology from the 200 m material '
    printf 'stress view. `source-climate.png` and `surface-diagnostics.png` show '
    printf 'which differences come from climate fields and material mapping.\n\n'
    printf -- '- Study assets: `%s`\n' "${STUDY_ROOT}"
    printf -- '- Controls: dry upland and temperate mountain valley\n'
    printf -- '- Resolution: %sx%s\n' "${WIDTH}" "${HEIGHT}"
    printf -- '- Placement: raw-center comparison with selected-placement compatibility probe\n'
    printf -- '- Comparison / stress height: 500 m / 200 m\n'
    printf -- '- Performance gate: combined terrain mean and p50 <= 1.10 ms\n'
    printf -- '- Metrics: `study-metrics.tsv` and `study-report.json`\n'
} >"${INDEX}"

printf 'terrain desert/canyon review: wrote %s\n' "${OUT_DIR}"
