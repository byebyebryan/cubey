#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/terrain/terrain}"
ASSET_ROOT="${ASSET_ROOT:-${ROOT_DIR}/cache/terrain/sources/v1/landscape-variations}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/terrain/landscape-variations-v1}"
WIDTH="${WIDTH:-1600}"
HEIGHT="${HEIGHT:-900}"
FRAMES="${FRAMES:-90}"
WARMUP_FRAMES="${WARMUP_FRAMES:-30}"
FPS="${FPS:-60}"

INDEX_JSON="${ASSET_ROOT}/variation-index.json"
CAPTURE_MANIFEST="${OUT_DIR}/capture-manifest.tsv"
METRICS="${OUT_DIR}/variation-metrics.tsv"
REPORT="${OUT_DIR}/study-report.json"
INDEX="${OUT_DIR}/index.md"

if [[ ! -x "${APP}" ]]; then
    printf 'terrain executable not found: %s\n' "${APP}" >&2
    printf 'Build it with: cmake --build --preset dev --target cubey_project_terrain\n' >&2
    exit 1
fi
if [[ ! -f "${INDEX_JSON}" ]]; then
    printf 'terrain landscape variation assets not found: %s\n' "${ASSET_ROOT}" >&2
    printf '%s\n' \
        'Generate them with: cmake --build --preset dev --target cubey_terrain_generate_landscape_variation_assets' \
        >&2
    exit 1
fi

mapfile -t VARIANT_RECORDS < <(
    jq -r '.variants[] | [.id, .label, (.seed | tostring)] | @tsv' "${INDEX_JSON}"
)
if [[ "${#VARIANT_RECORDS[@]}" -ne 4 ]]; then
    printf 'expected four landscape variants, found %d\n' "${#VARIANT_RECORDS[@]}" >&2
    exit 1
fi

mkdir -p "${OUT_DIR}/captures" "${OUT_DIR}/profiles"
find "${OUT_DIR}/captures" -mindepth 1 -delete
find "${OUT_DIR}/profiles" -mindepth 1 -delete
find "${OUT_DIR}" -mindepth 1 -maxdepth 1 ! -name captures ! -name profiles -delete

printf 'file\tvariant\tview\theading\tforeground_height_m\tdebug_view\n' \
    >"${CAPTURE_MANIFEST}"
printf '%s\n' \
    $'variant\tseed\tgeometry_hash\tcontent_hash\tdirectional_contract\tsource_focus_x_m\tsource_focus_z_m\tlocal_relief_m\tlocal_p95_slope\tmean_rock\tmean_snow\tmean_vegetation\tmean_moisture\tmean_moisture_weight\tmean_cover_weight\tcombined_mean_ms\tcombined_p50_ms\tcombined_p95_ms' \
    >"${METRICS}"

COMMON_ARGS=(
    --terrain-surface-model climate-transition
    --terrain-placement selected
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
    local seed="$2"
    local name="$3"
    local heading="$4"
    local foreground_height="$5"
    local debug_view="$6"
    local source_dir="${ASSET_ROOT}/${variant}"
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
        --terrain-foreground-height "${foreground_height}" \
        --terrain-backdrop-azimuth "${heading}" \
        "${COMMON_ARGS[@]}" "${debug_args[@]}" --output "${output}"
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        "${output#"${OUT_DIR}/"}" "${variant}" "${name}" "${heading}" \
        "${foreground_height}" "${debug_view}" >>"${CAPTURE_MANIFEST}"
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
    local seed="$2"
    local source_dir="${ASSET_ROOT}/${variant}"
    local prefix="${OUT_DIR}/profiles/${variant}"
    local video="${OUT_DIR}/profiles/${variant}.mp4"

    "${APP}" --headless --capture video --frames "${FRAMES}" --fps "${FPS}" \
        --width "${WIDTH}" --height "${HEIGHT}" \
        --terrain-heightfield "${source_dir}" \
        --terrain-surface-fields "${source_dir}" \
        --terrain-seed "${seed}" \
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
    read -r combined_mean combined_p50 combined_p95 <<<"$(combined_frame_stats "${passes}")"
    printf '%s\t%s\t%s\t%s' \
        "${variant}" "${seed}" \
        "$(metric_hash "${metrics}" geometry_hash)" \
        "$(metric_hash "${metrics}" content_hash)" >>"${METRICS}"
    for metric in directional_contract source_focus_x_m source_focus_z_m local_relief_m \
        local_p95_slope; do
        printf '\t%s' "$(metric_last "${metrics}" terrain.placement "${metric}")" >>"${METRICS}"
    done
    for metric in mean_rock mean_snow mean_vegetation mean_moisture; do
        printf '\t%s' "$(metric_last "${metrics}" terrain.surface "${metric}")" >>"${METRICS}"
    done
    for metric in mean_moisture_weight mean_cover_weight; do
        printf '\t%s' "$(metric_last "${metrics}" terrain.climate "${metric}")" >>"${METRICS}"
    done
    printf '\t%s\t%s\t%s\n' \
        "${combined_mean}" "${combined_p50}" "${combined_p95}" >>"${METRICS}"
}

for record in "${VARIANT_RECORDS[@]}"; do
    IFS=$'\t' read -r variant _label seed <<<"${record}"
    source_dir="${ASSET_ROOT}/${variant}"
    for manifest in heightfield.json surface-fields.json region-summary.json; do
        if [[ ! -f "${source_dir}/${manifest}" ]]; then
            printf 'landscape variation is incomplete: %s/%s\n' \
                "${source_dir}" "${manifest}" >&2
            exit 1
        fi
    done

    capture_variant "${variant}" "${seed}" surface-500m-heading-90 90 500 surface
    capture_variant "${variant}" "${seed}" surface-500m-heading-270 270 500 surface
    capture_variant "${variant}" "${seed}" clay-500m 90 500 clay
    capture_variant "${variant}" "${seed}" surface-stress-200m 90 200 surface
    capture_variant "${variant}" "${seed}" diagnostic-material-weights 90 500 material-weights
    capture_variant "${variant}" "${seed}" diagnostic-vegetation 90 500 vegetation
    capture_variant "${variant}" "${seed}" diagnostic-moisture 90 500 moisture
    profile_variant "${variant}" "${seed}"
done

awk -F '\t' '
    NR == 1 { next }
    {
        geometry[$3] = 1
        if ($5 + 0 != 1) {
            print "selected placement failed for " $1 > "/dev/stderr"
            failed = 1
        }
        if ($16 + 0 > 1.10 || $17 + 0 > 1.10) {
            print "terrain timing gate failed for " $1 > "/dev/stderr"
            failed = 1
        }
        if ($1 == "alpine-range") alpine_snow = $11 + 0
        if ($1 == "temperate-mountain-valley") valley_snow = $11 + 0
        if ($1 == "dry-upland") {
            dry_snow = $11 + 0
            dry_moisture = $14 + 0
            dry_cover = $15 + 0
        }
        if ($1 == "rolling-wet-lowland") {
            wet_snow = $11 + 0
            wet_moisture = $14 + 0
            wet_cover = $15 + 0
        }
    }
    END {
        count = 0
        for (hash in geometry) count += 1
        if (count != 4) {
            print "expected four unique terrain geometry hashes" > "/dev/stderr"
            failed = 1
        }
        if (!(alpine_snow > valley_snow && alpine_snow > dry_snow &&
              alpine_snow > wet_snow)) {
            print "alpine snow did not exceed every comparison source" > "/dev/stderr"
            failed = 1
        }
        if (dry_snow > 0.01) {
            print "dry upland retained material snow" > "/dev/stderr"
            failed = 1
        }
        if (!(wet_moisture > dry_moisture && wet_cover > dry_cover)) {
            print "wet lowland did not exceed dry upland moisture and cover" > "/dev/stderr"
            failed = 1
        }
        exit failed ? 1 : 0
    }
' "${METRICS}"

jq -Rn \
    --slurpfile source "${INDEX_JSON}" \
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
        schema: "cubey.terrain.landscape-variation-study.v1",
        git_revision: $git_revision,
        capture: {
            width: $width,
            height: $height,
            comparison_height_m: 500,
            stress_height_m: 200,
            headings_degrees: [90, 270],
            placement: "selected",
            render_stride: 3,
            combined_timing_gate_ms: 1.10
        },
        source: {
            schema: $source[0].schema,
            producer: $source[0].source,
            seeds: $source[0].seeds,
            selection: {
                method: $source[0].selection.method,
                recipes: $source[0].selection.recipes,
                regions: $source[0].selection.regions
            },
            field_contract: $source[0].field_contract,
            variants: $source[0].variants,
            validation: $source[0].validation
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
    MACRO_FILES=()
    MACRO_LABELS=()
    SHAPE_FILES=()
    SHAPE_LABELS=()
    DIAGNOSTIC_FILES=()
    DIAGNOSTIC_LABELS=()
    for record in "${VARIANT_RECORDS[@]}"; do
        IFS=$'\t' read -r variant label _seed <<<"${record}"
        SOURCE_FILES+=("${ASSET_ROOT}/${variant}/height.png" "${ASSET_ROOT}/${variant}/slope.png")
        SOURCE_LABELS+=("${label}: height" "${label}: slope")
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
        for diagnostic in material-weights vegetation moisture; do
            DIAGNOSTIC_FILES+=(
                "${OUT_DIR}/captures/${variant}/diagnostic-${diagnostic}.png"
            )
            DIAGNOSTIC_LABELS+=("${label}: ${diagnostic}")
        done
    done
    montage_group "${OUT_DIR}/source-morphology.png" 2x4 360x360+8+26 \
        SOURCE_FILES SOURCE_LABELS
    montage_group "${OUT_DIR}/macro-surface.png" 2x4 480x270+8+26 \
        MACRO_FILES MACRO_LABELS
    montage_group "${OUT_DIR}/shape-and-stress.png" 2x4 480x270+8+26 \
        SHAPE_FILES SHAPE_LABELS
    montage_group "${OUT_DIR}/surface-diagnostics.png" 3x4 400x225+8+26 \
        DIAGNOSTIC_FILES DIAGNOSTIC_LABELS
fi

# Backticks are intentional Markdown literals in the generated index.
# shellcheck disable=SC2016
{
    printf '# Terrain Landscape Variations V1\n\n'
    printf 'Start with `source-morphology.png`; it compares height and slope at fixed scales '
    printf 'before rendering. `macro-surface.png` checks whether each landscape survives a '
    printf '180-degree heading change.\n\n'
    printf '`shape-and-stress.png` separates clay morphology from the 200 m material stress '
    printf 'view. `surface-diagnostics.png` shows material weights, vegetation, and moisture; '
    printf 'snow is the green channel of material weights and is also recorded numerically.\n\n'
    printf -- '- Assets: `%s`\n' "${ASSET_ROOT}"
    printf -- '- Resolution: %sx%s\n' "${WIDTH}" "${HEIGHT}"
    printf -- '- Placement: selected\n'
    printf -- '- Comparison / stress height: 500 m / 200 m\n'
    printf -- '- Performance gate: combined terrain mean and p50 <= 1.10 ms\n'
    printf -- '- Metrics: `variation-metrics.tsv` and `study-report.json`\n'
} >"${INDEX}"

printf 'terrain landscape variation review: wrote %s\n' "${OUT_DIR}"
