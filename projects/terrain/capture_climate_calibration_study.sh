#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/dev/projects/terrain/terrain}"
ASSET_ROOT="${ASSET_ROOT:-${ROOT_DIR}/cache/terrain/sources/v1/climate-calibration}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/terrain/climate-calibration-v1}"
WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"

REGIMES=(hot-dry hot-wet cool-wet cold-dry cold-wet)
MODELS=(mineral-control landform-transition climate-transition)
HEADINGS=(90 270)
DIAGNOSTICS=(vegetation moisture material-weights material-albedo)

EXPECTED_GEOMETRY_HASH="0x0e3762ad8af185aa"
EXPECTED_CLIMATE_CONTENT_HASH="0x84ba91da263d7164"

if [[ ! -x "${APP}" ]]; then
    printf 'terrain executable not found: %s\n' "${APP}" >&2
    printf 'Build it with: cmake --build --preset dev --target cubey_project_terrain\n' >&2
    exit 1
fi
if [[ ! -f "${ASSET_ROOT}/calibration-index.json" ]]; then
    printf 'terrain climate calibration assets not found: %s\n' "${ASSET_ROOT}" >&2
    printf '%s\n' \
        'Generate them with: cmake --build --preset dev --target cubey_terrain_generate_climate_calibration_assets' \
        >&2
    exit 1
fi

mkdir -p "${OUT_DIR}/captures" "${OUT_DIR}/profiles"
find "${OUT_DIR}" -mindepth 1 -maxdepth 1 ! -name captures ! -name profiles -delete
find "${OUT_DIR}/captures" -mindepth 1 -delete
find "${OUT_DIR}/profiles" -mindepth 1 -delete

CAPTURE_MANIFEST="${OUT_DIR}/capture-manifest.tsv"
METRICS="${OUT_DIR}/climate-model-metrics.tsv"
REPORT="${OUT_DIR}/study-report.json"
INDEX="${OUT_DIR}/index.md"

printf 'file\tregime\tmodel\tview\theading\tdebug_view\n' >"${CAPTURE_MANIFEST}"
printf '%s\n' \
    $'regime\tmodel\tgeometry_hash\tcontent_hash\tsource_focus_x_m\tsource_focus_z_m\tmean_rock\tmean_snow\tmean_vegetation\tmean_moisture\tclimate_sample_count\tmean_temperature_c\tmean_temperature_stddev_c\tmean_precipitation_annual_mm\tmean_precipitation_cv\tmean_growing_season_days\tmean_thermal_growth\tmean_thermal_water_demand_proxy_mm\tmean_climate_moisture_ratio\tmean_seasonality_factor\tmean_effective_moisture\tmean_moisture_weight\tmean_cover_weight\tmean_annual_cold_potential\tmean_wet_snow_potential' \
    >"${METRICS}"

metric_last() {
    local metrics_file="$1"
    local category="$2"
    local name="$3"
    local value
    value="$(awk -F, -v category="${category}" -v name="${name}" \
        '$2 == category && $3 == name { value = $4 }
         END { if (value != "") print value }' "${metrics_file}")"
    if [[ -z "${value}" ]]; then
        printf 'missing metric %s/%s in %s\n' "${category}" "${name}" "${metrics_file}" >&2
        exit 1
    fi
    printf '%s' "${value}"
}

metric_hash() {
    local metrics_file="$1"
    local prefix="$2"
    local low high
    low="$(metric_last "${metrics_file}" terrain.backdrop "${prefix}_low32")"
    high="$(metric_last "${metrics_file}" terrain.backdrop "${prefix}_high32")"
    printf '0x%08x%08x' "${high%%.*}" "${low%%.*}"
}

COMMON_ARGS=(
    --terrain-camera-preset backdrop
    --terrain-render-stride 3
    --terrain-surface-detail filtered-detail
    --terrain-shadows
    --terrain-placement raw-center
    --terrain-foreground-height 500
    --terrain-backdrop-orbit-radius 100
    --terrain-backdrop-elevation 8
    --time-of-day-mode solar
    --time-hours 9
    --day-of-year 172
    --latitude-degrees 35
    --pause-time
    --no-clouds
)

capture() {
    local regime="$1"
    local model="$2"
    local name="$3"
    local view="$4"
    local heading="$5"
    local debug_view="$6"
    shift 6
    local region_dir="${ASSET_ROOT}/${regime}"
    local lane="${OUT_DIR}/captures/${regime}/${model}"
    local output="${lane}/${name}.png"
    mkdir -p "${lane}"
    "${APP}" --headless --width "${WIDTH}" --height "${HEIGHT}" \
        --terrain-heightfield "${region_dir}" \
        --terrain-surface-fields "${region_dir}" \
        --terrain-surface-model "${model}" \
        "${COMMON_ARGS[@]}" --terrain-backdrop-azimuth "${heading}" \
        "$@" --output "${output}"
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        "${output#"${OUT_DIR}/"}" "${regime}" "${model}" "${view}" "${heading}" \
        "${debug_view}" >>"${CAPTURE_MANIFEST}"
}

append_metrics() {
    local regime="$1"
    local model="$2"
    local metrics_file="$3"
    printf '%s\t%s\t%s\t%s' \
        "${regime}" "${model}" \
        "$(metric_hash "${metrics_file}" geometry_hash)" \
        "$(metric_hash "${metrics_file}" content_hash)" >>"${METRICS}"
    for metric in source_focus_x_m source_focus_z_m; do
        printf '\t%s' "$(metric_last "${metrics_file}" terrain.placement "${metric}")" \
            >>"${METRICS}"
    done
    for metric in mean_rock mean_snow mean_vegetation mean_moisture; do
        printf '\t%s' "$(metric_last "${metrics_file}" terrain.surface "${metric}")" \
            >>"${METRICS}"
    done
    for metric in sample_count mean_temperature_c mean_temperature_stddev_c \
        mean_precipitation_annual_mm mean_precipitation_cv mean_growing_season_days \
        mean_thermal_growth mean_thermal_water_demand_proxy_mm \
        mean_climate_moisture_ratio mean_seasonality_factor mean_effective_moisture \
        mean_moisture_weight mean_cover_weight mean_annual_cold_potential \
        mean_wet_snow_potential; do
        printf '\t%s' "$(metric_last "${metrics_file}" terrain.climate "${metric}")" \
            >>"${METRICS}"
    done
    printf '\n' >>"${METRICS}"
}

for regime in "${REGIMES[@]}"; do
    region_dir="${ASSET_ROOT}/${regime}"
    for manifest in heightfield.json surface-fields.json region-summary.json; do
        if [[ ! -f "${region_dir}/${manifest}" ]]; then
            printf 'calibration region is incomplete: %s/%s\n' "${region_dir}" "${manifest}" >&2
            exit 1
        fi
    done
    for model in "${MODELS[@]}"; do
        profile_prefix="${OUT_DIR}/profiles/${regime}-${model}"
        for heading in "${HEADINGS[@]}"; do
            profile_args=()
            if [[ "${heading}" == "90" ]]; then
                profile_args=(--profile-output "${profile_prefix}")
            fi
            capture "${regime}" "${model}" "heading-${heading}" standard "${heading}" none \
                "${profile_args[@]}"
        done
        append_metrics "${regime}" "${model}" "${profile_prefix}.metrics.csv"
    done

    capture "${regime}" climate-transition raking raking 90 none \
        --time-of-day-mode manual --sun-elevation 12 --sun-azimuth 35
    for diagnostic in "${DIAGNOSTICS[@]}"; do
        capture "${regime}" climate-transition "diagnostic-${diagnostic}" diagnostic 90 \
            "${diagnostic}" --debug-view "${diagnostic}"
    done
done

CANONICAL_PROFILE="${OUT_DIR}/profiles/hot-dry-canonical-selected"
capture hot-dry climate-transition canonical-selected-control canonical 90 none \
    --terrain-placement selected --profile-output "${CANONICAL_PROFILE}"
CANONICAL_GEOMETRY_HASH="$(metric_hash "${CANONICAL_PROFILE}.metrics.csv" geometry_hash)"
CANONICAL_CONTENT_HASH="$(metric_hash "${CANONICAL_PROFILE}.metrics.csv" content_hash)"
if [[ "${CANONICAL_GEOMETRY_HASH}" != "${EXPECTED_GEOMETRY_HASH}" ]]; then
    printf 'canonical hot-dry geometry hash changed: %s\n' "${CANONICAL_GEOMETRY_HASH}" >&2
    exit 1
fi
if [[ "${CANONICAL_CONTENT_HASH}" != "${EXPECTED_CLIMATE_CONTENT_HASH}" ]]; then
    printf 'canonical hot-dry climate content hash changed: %s\n' \
        "${CANONICAL_CONTENT_HASH}" >&2
    exit 1
fi

awk -F '\t' '
    NR == 1 { next }
    {
        key = $1
        frozen = $3 "|" $5 "|" $6
        climate = $11
        for (column = 12; column <= 25; ++column) climate = climate "|" $column
        if (key in frozen_by_regime && frozen_by_regime[key] != frozen) {
            print "geometry or selected focus changed across models for " key > "/dev/stderr"
            exit 1
        }
        if (key in climate_by_regime && climate_by_regime[key] != climate) {
            print "climate diagnostics changed across models for " key > "/dev/stderr"
            exit 1
        }
        frozen_by_regime[key] = frozen
        climate_by_regime[key] = climate
        model_count[key] += 1
    }
    END {
        for (key in model_count) {
            if (model_count[key] != 3) {
                print "expected three model rows for " key > "/dev/stderr"
                exit 1
            }
        }
        if (length(model_count) != 5) {
            print "expected five climate regimes" > "/dev/stderr"
            exit 1
        }
    }
' "${METRICS}"

jq -Rn \
    --slurpfile calibration "${ASSET_ROOT}/calibration-index.json" \
    --arg git_revision "$(git -C "${ROOT_DIR}" rev-parse HEAD)" \
    --arg canonical_geometry_hash "${CANONICAL_GEOMETRY_HASH}" \
    --arg canonical_content_hash "${CANONICAL_CONTENT_HASH}" \
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
        schema: "cubey.terrain.climate-calibration-study.v1",
        git_revision: $git_revision,
        capture: {
            width: $width,
            height: $height,
            foreground_height_m: 500,
            headings_degrees: [90, 270],
            comparison_placement: "raw-center",
            render_stride: 3,
            surface_detail: "filtered-detail",
            performance_gate: null
        },
        canonical_selected_control: {
            regime: "hot-dry",
            model: "climate-transition",
            geometry_hash: $canonical_geometry_hash,
            content_hash: $canonical_content_hash
        },
        calibration: $calibration[0],
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
    MODEL_FILES=()
    MODEL_LABELS=()
    HEADING_FILES=()
    HEADING_LABELS=()
    RAKING_FILES=()
    RAKING_LABELS=()
    DIAGNOSTIC_FILES=()
    DIAGNOSTIC_LABELS=()
    SOURCE_FILES=()
    SOURCE_LABELS=()
    for regime in "${REGIMES[@]}"; do
        for model in "${MODELS[@]}"; do
            MODEL_FILES+=("${OUT_DIR}/captures/${regime}/${model}/heading-90.png")
            MODEL_LABELS+=("${regime}: ${model}")
            for heading in "${HEADINGS[@]}"; do
                HEADING_FILES+=("${OUT_DIR}/captures/${regime}/${model}/heading-${heading}.png")
                HEADING_LABELS+=("${regime}: ${model}, ${heading} deg")
            done
        done
        RAKING_FILES+=("${OUT_DIR}/captures/${regime}/climate-transition/raking.png")
        RAKING_LABELS+=("${regime}")
        for diagnostic in "${DIAGNOSTICS[@]}"; do
            DIAGNOSTIC_FILES+=(
                "${OUT_DIR}/captures/${regime}/climate-transition/diagnostic-${diagnostic}.png"
            )
            DIAGNOSTIC_LABELS+=("${regime}: ${diagnostic}")
        done
        for source in height slope climate-temperature-mean climate-temperature-stddev \
            climate-precipitation-annual climate-precipitation-cv; do
            SOURCE_FILES+=("${ASSET_ROOT}/${regime}/${source}.png")
            SOURCE_LABELS+=("${regime}: ${source}")
        done
    done
    montage_group "${OUT_DIR}/model-overview.png" 3x5 320x180+8+26 \
        MODEL_FILES MODEL_LABELS
    montage_group "${OUT_DIR}/heading-comparison.png" 6x5 256x144+6+24 \
        HEADING_FILES HEADING_LABELS
    montage_group "${OUT_DIR}/raking-comparison.png" 5x1 320x180+8+26 \
        RAKING_FILES RAKING_LABELS
    montage_group "${OUT_DIR}/surface-diagnostics.png" 4x5 320x180+8+26 \
        DIAGNOSTIC_FILES DIAGNOSTIC_LABELS
    montage_group "${OUT_DIR}/source-fields.png" 6x5 240x240+6+24 \
        SOURCE_FILES SOURCE_LABELS
fi

{
    printf '# Terrain Climate Calibration Study V1\n\n'
    printf 'Start with `model-overview.png` for model separation across the five regimes. '
    printf '`heading-comparison.png` checks that the reading survives a 180-degree view change, '
    printf 'and `raking-comparison.png` emphasizes relief and snow boundaries.\n\n'
    printf '`source-fields.png` uses fixed scales across every region. '
    printf '`surface-diagnostics.png` shows the final vegetation, moisture, material weights, '
    printf 'and material albedo for the climate model.\n\n'
    printf 'The report is evidence-only: it preserves all existing formulas and thresholds, '
    printf 'does not promote a new biome contract, and applies no new performance gate.\n\n'
    printf 'Comparison lanes use deterministic `raw-center` placement so source selection stays '
    printf 'independent from the material study. A separate selected hot/dry capture checks the '
    printf 'current selected-stage geometry and content hashes.\n\n'
    printf -- '- Assets: `%s`\n' "${ASSET_ROOT}"
    printf -- '- Resolution: %sx%s\n' "${WIDTH}" "${HEIGHT}"
    printf -- '- Foreground height: 500 m\n'
    printf -- '- Models: `%s`\n' "${MODELS[*]}"
    printf -- '- Regimes: `%s`\n' "${REGIMES[*]}"
    printf -- '- Metrics: `climate-model-metrics.tsv` and `study-report.json`\n'
} >"${INDEX}"

printf 'terrain climate calibration study: wrote %s\n' "${OUT_DIR}"
