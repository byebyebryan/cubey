#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-${ROOT_DIR}/build/release/projects/gltf_viewer/gltf_viewer}"
ASSET_ROOT="${ASSET_ROOT:-${ROOT_DIR}/build/dev-gltf-conformance/_deps/gltf_sample_assets-src}"
OUT_DIR="${1:-${ROOT_DIR}/outputs/gltf/loading-profile-$(date +%Y%m%d-%H%M%S)}"
REPEATS="${REPEATS:-5}"
WIDTH="${WIDTH:-320}"
HEIGHT="${HEIGHT:-180}"
SUMMARIZE_ONLY="${SUMMARIZE_ONLY:-0}"
UPLOAD_OWNER_TARGET_MS="${UPLOAD_OWNER_TARGET_MS:-2}"
UPLOAD_STEP_BYTE_CAP="${UPLOAD_STEP_BYTE_CAP:-33554432}"

SAMPLE_ASSETS_PIN="2bac6f8c57bf471df0d2a1e8a8ec023c7801dddf"
MANIFEST_SCHEMA="gltf_loading_profile_v1"
MODEL_INDEX="${ASSET_ROOT}/Models/model-index.json"
PROFILES_DIR="${OUT_DIR}/profiles"
LOGS_DIR="${OUT_DIR}/logs"
RUNS_CSV="${OUT_DIR}/runs.csv"
SUMMARY_CSV="${OUT_DIR}/summary.csv"
METADATA_TXT="${OUT_DIR}/metadata.txt"
MANIFEST_CSV="${OUT_DIR}/profile-manifest.csv"

LANE_NAMES=(
    damaged-helmet-glb
    basisu-anisotropy
    skinned-animation
    animated-morph
    large-scene
)
LANE_PATHS=(
    Models/DamagedHelmet/glTF-Binary/DamagedHelmet.glb
    Models/AnisotropyBarnLamp/glTF-KTX-BasisU/AnisotropyBarnLamp.gltf
    Models/CesiumMan/glTF-Binary/CesiumMan.glb
    Models/AnimatedMorphCube/glTF-Binary/AnimatedMorphCube.glb
    Models/Sponza/glTF/Sponza.gltf
)

METRIC_NAMES=(
    generation_id
    source_file_bytes
    metadata_probe_ms
    asset_load_ms
    document_parse_ms
    buffer_load_ms
    asset_validate_ms
    image_payload_ms
    image_decode_ms
    asset_assembly_ms
    scene_prepare_ms
    staged_worker_prepare_ms
    gltf_residency_ms
    staged_gpu_install_ms
    activation_ms
    triangle_count
    node_count
    material_count
    texture_count
    prepared_texture_count
    basisu_encoded_image_bytes
    decoded_rgba_bytes
    prepared_texture_upload_bytes
    mesh_upload_bytes
    mesh_upload_transfer_submission_count
    gpu_upload_bytes
    gpu_upload_copy_count
    gpu_upload_owner_advance_count
    gpu_upload_step_count
    gpu_upload_submission_count
    gpu_upload_owner_submit_ms
    gpu_upload_owner_max_step_ms
    gpu_upload_owner_target_ms
    gpu_upload_step_byte_cap
    gpu_upload_copy_byte_target
    gpu_upload_owner_over_target_step_count
    gpu_upload_completion_latency_ms
    gpu_upload_pool_initial_capacity_bytes
    gpu_upload_pool_final_capacity_bytes
    gpu_upload_pool_peak_capacity_bytes
    gpu_upload_pool_reserved_at_final_submission_bytes
    gpu_upload_pool_growth_count
    gpu_upload_backpressure_count
    gpu_upload_first_step_to_final_completion_ms
    gpu_upload_submission_frame
    gpu_upload_completion_frame
)

declare -a MANIFEST_LANES=()
declare -a MANIFEST_OBSERVATIONS=()
declare -a MANIFEST_REPEATS=()
declare -a MANIFEST_ASSETS=()
declare -a MANIFEST_PREFIXES=()
declare -a MANIFEST_LANE_ORDER=()
declare -A MANIFEST_OBSERVATION_SEEN=()
declare -A MANIFEST_PREFIX_SEEN=()
declare -A MANIFEST_LANE_SEEN=()
declare -A MANIFEST_LANE_ASSET=()
MANIFEST_WARM_REPEATS=""
ACTIVE_RUNS_CSV=""
ACTIVE_SUMMARY_CSV=""

fail() {
    printf 'gltf loading profile: %b\n' "$*" >&2
    exit 1
}

require_positive_integer() {
    local name="$1"
    local value="$2"
    [[ "${value}" =~ ^[1-9][0-9]*$ ]] || fail "${name} must be a positive integer (got ${value})"
}

validate_lane_name() {
    local lane="$1"
    [[ "${lane}" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] ||
        fail "manifest lane name is malformed: ${lane}"
}

validate_relative_asset() {
    local relative_asset="$1"
    [[ -n "${relative_asset}" && "${relative_asset}" != /* && "${relative_asset}" != *".."* &&
        "${relative_asset}" != *','* ]] ||
        fail "manifest asset path is malformed: ${relative_asset}"
}

sha256_file() {
    local path="$1"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "${path}" | awk '{print $1}'
        return
    fi
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "${path}" | awk '{print $1}'
        return
    fi
    printf 'unavailable'
}

git_head() {
    local path="$1"
    git -C "${path}" rev-parse HEAD 2>/dev/null || printf 'unavailable'
}

git_state() {
    local path="$1"
    if git -C "${path}" rev-parse --is-inside-work-tree >/dev/null 2>&1 &&
        [[ -z "$(git -C "${path}" status --porcelain 2>/dev/null)" ]]; then
        printf 'clean'
    else
        printf 'dirty-or-unavailable'
    fi
}

gpu_driver() {
    if command -v nvidia-smi >/dev/null 2>&1; then
        nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null |
            head -n 1 || true
        return
    fi
    if command -v vulkaninfo >/dev/null 2>&1; then
        vulkaninfo --summary 2>/dev/null |
            awk '/GPU[0-9]+:/ || /driverName/ || /driverInfo/ { print; count++; if (count >= 3) exit }' ||
            true
        return
    fi
    printf 'unavailable'
}

validate_pinned_sample_assets_checkout() {
    local asset_root="${1:-${ASSET_ROOT}}"
    [[ -d "${asset_root}" ]] ||
        fail "Sample Assets checkout is missing: ${asset_root}; configure with 'cmake --preset dev-gltf-conformance'"
    git -C "${asset_root}" rev-parse --is-inside-work-tree >/dev/null 2>&1 ||
        fail "Sample Assets root is not a Git checkout: ${asset_root}; live profiling requires the exact clean pinned checkout ${SAMPLE_ASSETS_PIN}"

    local head
    head="$(git -C "${asset_root}" rev-parse HEAD 2>/dev/null)" ||
        fail "Sample Assets checkout has no readable Git HEAD: ${asset_root}"
    [[ "${head}" == "${SAMPLE_ASSETS_PIN}" ]] ||
        fail "Sample Assets checkout is at ${head}, expected pinned ${SAMPLE_ASSETS_PIN}; live profiling will not use an unpinned corpus"

    local status
    status="$(git -C "${asset_root}" status --porcelain --untracked-files=normal 2>/dev/null)" ||
        fail "could not inspect Sample Assets checkout state: ${asset_root}"
    [[ -z "${status}" ]] ||
        fail "Sample Assets checkout is dirty: ${asset_root}; live profiling requires the exact clean pinned checkout ${SAMPLE_ASSETS_PIN}"
}

write_metadata() {
    local cubey_state
    local sample_state
    local sample_size
    cubey_state="$(git_state "${ROOT_DIR}")"
    sample_state="$(git_state "${ASSET_ROOT}")"
    sample_size="$(du -sh "${ASSET_ROOT}" 2>/dev/null | awk '{print $1}' || printf 'unavailable')"

    {
        printf '# Cubey glTF staged-loading profile metadata\n'
        printf 'generated_at=%s\n' "$(date --iso-8601=seconds 2>/dev/null || date)"
        printf 'runner=%s\n' "${BASH_SOURCE[0]}"
        printf 'summarize_only=0\n'
        printf 'app=%s\n' "${APP}"
        printf 'app_sha256=%s\n' "$(sha256_file "${APP}")"
        printf 'cubey_root=%s\n' "${ROOT_DIR}"
        printf 'cubey_head=%s\n' "$(git_head "${ROOT_DIR}")"
        printf 'cubey_worktree=%s\n' "${cubey_state}"
        printf 'sample_assets_root=%s\n' "${ASSET_ROOT}"
        printf 'sample_assets_required_pin=%s\n' "${SAMPLE_ASSETS_PIN}"
        printf 'sample_assets_head=%s\n' "$(git_head "${ASSET_ROOT}")"
        printf 'sample_assets_worktree=%s\n' "${sample_state}"
        printf 'sample_assets_checkout_size=%s\n' "${sample_size}"
        printf 'profile_manifest=%s\n' "$(basename "${MANIFEST_CSV}")"
        printf 'profile_manifest_schema=%s\n' "${MANIFEST_SCHEMA}"
        printf 'host=%s\n' "$(hostname 2>/dev/null || printf 'unavailable')"
        printf 'os=%s\n' "$(uname -srvm 2>/dev/null || printf 'unavailable')"
        printf 'gpu_driver=%s\n' "$(gpu_driver | tr '\n' ';')"
        printf 'width=%s\n' "${WIDTH}"
        printf 'height=%s\n' "${HEIGHT}"
        printf 'warm_repeats=%s\n' "${REPEATS}"
        printf 'profile_warmup_frames=0\n'
        printf 'profile_upload_owner_target_ms=%s\n' "${UPLOAD_OWNER_TARGET_MS}"
        printf 'profile_upload_step_byte_cap=%s\n' "${UPLOAD_STEP_BYTE_CAP}"
        printf 'profile_upload_copy_byte_target=2097152\n'
        printf 'environment=static_pbr_generated_ibl\n'
        printf 'clouds=disabled\n'
        printf 'terrain=disabled\n'
        printf 'ocean=disabled\n'
        printf 'capture=one_frame_png_then_removed\n'
        printf '\n[lane_assets]\n'
        printf 'lane,relative_path,size_bytes,sha256\n'
        local index
        for index in "${!LANE_NAMES[@]}"; do
            local asset_path="${ASSET_ROOT}/${LANE_PATHS[index]}"
            printf '%s,%s,%s,%s\n' \
                "${LANE_NAMES[index]}" \
                "${LANE_PATHS[index]}" \
                "$(stat -c '%s' "${asset_path}")" \
                "$(sha256_file "${asset_path}")"
        done
    } >"${METADATA_TXT}"
}

validate_asset_inventory() {
    validate_pinned_sample_assets_checkout "${ASSET_ROOT}"
    [[ -f "${MODEL_INDEX}" ]] ||
        fail "Sample Assets inventory is missing: ${MODEL_INDEX}; configure with 'cmake --preset dev-gltf-conformance'"

    local index
    for index in "${!LANE_NAMES[@]}"; do
        local asset_path="${ASSET_ROOT}/${LANE_PATHS[index]}"
        [[ -f "${asset_path}" ]] ||
            fail "profile lane '${LANE_NAMES[index]}' is missing ${LANE_PATHS[index]}; no substitute will be selected"
        [[ -s "${asset_path}" ]] || fail "profile asset is empty: ${asset_path}"
    done
}

validate_app() {
    if [[ ! -x "${APP}" ]]; then
        fail "release gltf_viewer is missing or not executable: ${APP}\nConfigure/build it with: cmake --preset release && cmake --build --preset release --target cubey_project_gltf_viewer"
    fi
}

validate_fresh_live_output_dir() {
    if [[ -e "${OUT_DIR}" && ! -d "${OUT_DIR}" ]]; then
        fail "live output path exists and is not a directory: ${OUT_DIR}"
    fi
    if [[ -d "${OUT_DIR}" ]]; then
        local existing_entry
        existing_entry="$(find "${OUT_DIR}" -mindepth 1 -maxdepth 1 -print -quit)"
        [[ -z "${existing_entry}" ]] ||
            fail "live output directory is not empty: ${OUT_DIR}; choose a fresh directory to avoid reusing evidence"
    fi
}

validate_png() {
    local path="$1"
    [[ -s "${path}" ]] || fail "headless capture was empty: ${path}"
    if command -v file >/dev/null 2>&1; then
        [[ "$(file -b --mime-type "${path}")" == "image/png" ]] ||
            fail "headless capture is not a PNG: ${path}"
    else
        local signature
        signature="$(od -An -tx1 -N8 "${path}" | tr -d '[:space:]')"
        [[ "${signature}" == "89504e470d0a1a0a" ]] || fail "headless capture has no PNG signature: ${path}"
    fi
}

reset_manifest() {
    MANIFEST_LANES=()
    MANIFEST_OBSERVATIONS=()
    MANIFEST_REPEATS=()
    MANIFEST_ASSETS=()
    MANIFEST_PREFIXES=()
    MANIFEST_LANE_ORDER=()
    MANIFEST_OBSERVATION_SEEN=()
    MANIFEST_PREFIX_SEEN=()
    MANIFEST_LANE_SEEN=()
    MANIFEST_LANE_ASSET=()
    MANIFEST_WARM_REPEATS=""
}

manifest_expected_prefix() {
    local lane="$1"
    local observation="$2"
    local repeat="$3"
    if [[ "${observation}" == 'first' ]]; then
        printf 'profiles/%s-first' "${lane}"
    else
        printf 'profiles/%s-warm-%s' "${lane}" "${repeat}"
    fi
}

add_manifest_entry() {
    local schema="$1"
    local warm_repeats="$2"
    local lane="$3"
    local observation="$4"
    local repeat="$5"
    local relative_asset="$6"
    local profile_prefix="$7"

    [[ "${schema}" == "${MANIFEST_SCHEMA}" ]] ||
        fail "unsupported profile manifest schema '${schema}'"
    require_positive_integer 'manifest warm_repeats' "${warm_repeats}"
    validate_lane_name "${lane}"
    validate_relative_asset "${relative_asset}"
    [[ "${observation}" == 'first' || "${observation}" == 'warm' ]] ||
        fail "manifest observation must be first or warm (got ${observation})"
    if [[ "${observation}" == 'first' ]]; then
        [[ "${repeat}" == '0' ]] || fail "manifest first observation for ${lane} must have repeat 0"
    else
        require_positive_integer "manifest warm repeat for ${lane}" "${repeat}"
        (( repeat <= warm_repeats )) ||
            fail "manifest warm repeat ${repeat} exceeds warm_repeats ${warm_repeats} for ${lane}"
    fi
    local expected_prefix
    expected_prefix="$(manifest_expected_prefix "${lane}" "${observation}" "${repeat}")"
    [[ "${profile_prefix}" == "${expected_prefix}" ]] ||
        fail "manifest profile prefix for ${lane}/${observation}/${repeat} must be ${expected_prefix} (got ${profile_prefix})"

    if [[ -z "${MANIFEST_WARM_REPEATS}" ]]; then
        MANIFEST_WARM_REPEATS="${warm_repeats}"
    elif [[ "${MANIFEST_WARM_REPEATS}" != "${warm_repeats}" ]]; then
        fail 'manifest has inconsistent warm_repeats values'
    fi

    local observation_key="${lane}|${observation}|${repeat}"
    [[ -z "${MANIFEST_OBSERVATION_SEEN[${observation_key}]+present}" ]] ||
        fail "duplicate manifest observation: ${lane}/${observation}/${repeat}"
    [[ -z "${MANIFEST_PREFIX_SEEN[${profile_prefix}]+present}" ]] ||
        fail "duplicate manifest profile prefix: ${profile_prefix}"
    MANIFEST_OBSERVATION_SEEN["${observation_key}"]=1
    MANIFEST_PREFIX_SEEN["${profile_prefix}"]=1

    if [[ -z "${MANIFEST_LANE_SEEN[${lane}]+present}" ]]; then
        MANIFEST_LANE_SEEN["${lane}"]=1
        MANIFEST_LANE_ASSET["${lane}"]="${relative_asset}"
        MANIFEST_LANE_ORDER+=("${lane}")
    elif [[ "${MANIFEST_LANE_ASSET[${lane}]}" != "${relative_asset}" ]]; then
        fail "manifest lane ${lane} uses multiple asset paths"
    fi

    MANIFEST_LANES+=("${lane}")
    MANIFEST_OBSERVATIONS+=("${observation}")
    MANIFEST_REPEATS+=("${repeat}")
    MANIFEST_ASSETS+=("${relative_asset}")
    MANIFEST_PREFIXES+=("${profile_prefix}")
}

validate_loaded_manifest() {
    [[ -n "${MANIFEST_WARM_REPEATS}" && "${#MANIFEST_LANE_ORDER[@]}" -gt 0 ]] ||
        fail 'profile manifest contains no observations'

    local lane
    for lane in "${MANIFEST_LANE_ORDER[@]}"; do
        local first_key="${lane}|first|0"
        [[ -n "${MANIFEST_OBSERVATION_SEEN[${first_key}]+present}" ]] ||
            fail "manifest is missing first observation for ${lane}"
        local repeat
        for ((repeat = 1; repeat <= MANIFEST_WARM_REPEATS; ++repeat)); do
            local warm_key="${lane}|warm|${repeat}"
            [[ -n "${MANIFEST_OBSERVATION_SEEN[${warm_key}]+present}" ]] ||
                fail "manifest is missing warm observation ${repeat} for ${lane}"
        done
    done

    local expected_count=$(( ${#MANIFEST_LANE_ORDER[@]} * (MANIFEST_WARM_REPEATS + 1) ))
    [[ "${#MANIFEST_LANES[@]}" -eq "${expected_count}" ]] ||
        fail 'manifest contains observations outside its declared first/warm inventory'
}

write_manifest() {
    {
        printf 'schema,warm_repeats,lane,observation,repeat,asset_relative_path,profile_prefix\n'
        local index
        for index in "${!LANE_NAMES[@]}"; do
            local lane="${LANE_NAMES[index]}"
            local relative_asset="${LANE_PATHS[index]}"
            printf '%s,%s,%s,first,0,%s,%s\n' \
                "${MANIFEST_SCHEMA}" "${REPEATS}" "${lane}" "${relative_asset}" \
                "$(manifest_expected_prefix "${lane}" first 0)"
            local repeat
            for ((repeat = 1; repeat <= REPEATS; ++repeat)); do
                printf '%s,%s,%s,warm,%s,%s,%s\n' \
                    "${MANIFEST_SCHEMA}" "${REPEATS}" "${lane}" "${repeat}" "${relative_asset}" \
                    "$(manifest_expected_prefix "${lane}" warm "${repeat}")"
            done
        done
    } >"${MANIFEST_CSV}"
}

load_manifest() {
    reset_manifest
    [[ -f "${MANIFEST_CSV}" ]] || return 1

    local header
    IFS= read -r header <"${MANIFEST_CSV}"
    [[ "${header}" == 'schema,warm_repeats,lane,observation,repeat,asset_relative_path,profile_prefix' ]] ||
        fail "unexpected profile manifest header in ${MANIFEST_CSV}: ${header}"

    local schema warm_repeats lane observation repeat relative_asset profile_prefix extra
    local rows=0
    while IFS=, read -r schema warm_repeats lane observation repeat relative_asset profile_prefix extra ||
        [[ -n "${schema}${warm_repeats}${lane}${observation}${repeat}${relative_asset}${profile_prefix}${extra}" ]]; do
        rows=$((rows + 1))
        [[ -z "${extra:-}" && -n "${schema}" && -n "${warm_repeats}" && -n "${lane}" &&
            -n "${observation}" && -n "${repeat}" && -n "${relative_asset}" && -n "${profile_prefix}" ]] ||
            fail "malformed profile manifest row ${rows} in ${MANIFEST_CSV}"
        add_manifest_entry "${schema}" "${warm_repeats}" "${lane}" "${observation}" "${repeat}" \
            "${relative_asset}" "${profile_prefix}"
    done < <(tail -n +2 "${MANIFEST_CSV}")
    [[ "${rows}" -gt 0 ]] || fail "profile manifest has no data rows: ${MANIFEST_CSV}"
    validate_loaded_manifest
    return 0
}

metadata_value() {
    local key="$1"
    [[ -f "${METADATA_TXT}" ]] || return 1
    awk -F= -v key="${key}" '$1 == key { print substr($0, length(key) + 2); exit }' "${METADATA_TXT}"
}

load_legacy_manifest_from_metadata() {
    reset_manifest
    [[ -f "${METADATA_TXT}" ]] ||
        fail "profile manifest is missing and no legacy metadata is available: ${OUT_DIR}"

    local declared_pin sample_head sample_state legacy_repeats
    declared_pin="$(metadata_value sample_assets_declared_pin || true)"
    sample_head="$(metadata_value sample_assets_head || true)"
    sample_state="$(metadata_value sample_assets_worktree || true)"
    legacy_repeats="$(metadata_value warm_repeats || true)"
    [[ "${declared_pin}" == "${SAMPLE_ASSETS_PIN}" && "${sample_head}" == "${SAMPLE_ASSETS_PIN}" &&
        "${sample_state}" == 'clean' ]] ||
        fail "legacy metadata cannot prove the exact clean pinned Sample Assets corpus; replay requires ${MANIFEST_CSV}"
    require_positive_integer 'legacy metadata warm_repeats' "${legacy_repeats}"

    local in_assets=0
    local saw_header=0
    local rows=0
    local line lane relative_asset size_bytes sha256 extra
    while IFS= read -r line || [[ -n "${line}" ]]; do
        if [[ "${line}" == '[lane_assets]' ]]; then
            in_assets=1
            continue
        fi
        [[ "${in_assets}" -eq 1 ]] || continue
        if [[ "${saw_header}" -eq 0 ]]; then
            [[ "${line}" == 'lane,relative_path,size_bytes,sha256' ]] ||
                fail 'legacy metadata has an invalid lane inventory header'
            saw_header=1
            continue
        fi
        [[ -n "${line}" ]] || break
        IFS=, read -r lane relative_asset size_bytes sha256 extra <<<"${line}"
        [[ -z "${extra:-}" && -n "${lane}" && -n "${relative_asset}" &&
            "${size_bytes}" =~ ^[1-9][0-9]*$ && "${sha256}" =~ ^[0-9a-fA-F]{64}$ ]] ||
            fail "legacy metadata has a malformed lane inventory row: ${line}"
        add_manifest_entry "${MANIFEST_SCHEMA}" "${legacy_repeats}" "${lane}" first 0 \
            "${relative_asset}" "$(manifest_expected_prefix "${lane}" first 0)"
        local repeat
        for ((repeat = 1; repeat <= legacy_repeats; ++repeat)); do
            add_manifest_entry "${MANIFEST_SCHEMA}" "${legacy_repeats}" "${lane}" warm "${repeat}" \
                "${relative_asset}" "$(manifest_expected_prefix "${lane}" warm "${repeat}")"
        done
        rows=$((rows + 1))
    done <"${METADATA_TXT}"
    [[ "${saw_header}" -eq 1 && "${rows}" -gt 0 ]] ||
        fail "legacy metadata has no complete lane inventory; replay requires ${MANIFEST_CSV}"
    validate_loaded_manifest
    printf 'gltf loading profile: replaying legacy retained profiles from pinned metadata; a new live run will write %s\n' \
        "${MANIFEST_CSV}" >&2
}

load_retained_inventory() {
    if ! load_manifest; then
        load_legacy_manifest_from_metadata
    fi
}

parse_metrics() {
    local metrics_csv="$1"
    local lane="$2"
    local observation="$3"
    local repeat="$4"
    local relative_asset="$5"
    [[ -f "${metrics_csv}" ]] || fail "profile metrics are missing: ${metrics_csv}"

    local header
    IFS= read -r header <"${metrics_csv}"
    [[ "${header}" == 'frame_index,category,name,value' ]] ||
        fail "unexpected metrics CSV header in ${metrics_csv}: ${header}"

    declare -A values=()
    local rows=0
    local frame_index=""
    local metric_frame category name value extra
    while IFS=, read -r metric_frame category name value extra ||
        [[ -n "${metric_frame}${category}${name}${value}${extra}" ]]; do
        [[ -n "${metric_frame}" ]] || continue
        [[ -z "${extra:-}" ]] || fail "unexpected comma in metrics row: ${metrics_csv}"
        [[ "${category}" == 'gltf_loading' ]] || continue
        [[ "${metric_frame}" =~ ^[0-9]+$ &&
            "${value}" =~ ^-?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?$ ]] ||
            fail "malformed gltf_loading metric row in ${metrics_csv}"
        rows=$((rows + 1))
        if [[ -z "${frame_index}" ]]; then
            frame_index="${metric_frame}"
        elif [[ "${metric_frame}" != "${frame_index}" ]]; then
            fail "gltf_loading metrics span multiple frames in ${metrics_csv}"
        fi
        [[ -z "${values[${name}]+present}" ]] ||
            fail "duplicate gltf_loading metric '${name}' in ${metrics_csv}"
        values["${name}"]="${value}"
    done < <(tail -n +2 "${metrics_csv}")

    [[ "${rows}" -eq "${#METRIC_NAMES[@]}" ]] ||
        fail "expected exactly ${#METRIC_NAMES[@]} gltf_loading metrics in ${metrics_csv}, found ${rows}"
    local expected
    for expected in "${METRIC_NAMES[@]}"; do
        [[ -n "${values[${expected}]+present}" ]] ||
            fail "missing gltf_loading metric '${expected}' in ${metrics_csv}"
    done

    {
        printf '%s,%s,%s,%s' "${lane}" "${observation}" "${repeat}" "${relative_asset}"
        for expected in "${METRIC_NAMES[@]}"; do
            printf ',%s' "${values[${expected}]}"
        done
        printf '\n'
    } >>"${ACTIVE_RUNS_CSV}"
}

run_observation() {
    local lane="$1"
    local relative_asset="$2"
    local observation="$3"
    local repeat="$4"
    local profile_prefix="$5"
    local asset_path="${ASSET_ROOT}/${relative_asset}"
    local observation_label="${observation}"
    if [[ "${observation}" == 'warm' ]]; then
        observation_label="${observation}-${repeat}"
    fi
    local prefix="${OUT_DIR}/${profile_prefix}"
    local png_path="${OUT_DIR}/${lane}-${observation_label}.png"
    local log_path="${LOGS_DIR}/${lane}-${observation_label}.log"

    if [[ "${SUMMARIZE_ONLY}" == '1' ]]; then
        parse_metrics "${prefix}.metrics.csv" "${lane}" "${observation}" "${repeat}" "${relative_asset}"
        return
    fi

    local -a command=(
        "${APP}"
        --headless
        --capture png
        --frames 1
        --width "${WIDTH}"
        --height "${HEIGHT}"
        --output "${png_path}"
        --input "${asset_path}"
        --pbr-environment-source static
        --no-clouds
        --no-ocean-backdrop
        --profile-upload-owner-target-ms "${UPLOAD_OWNER_TARGET_MS}"
        --profile-upload-step-byte-cap "${UPLOAD_STEP_BYTE_CAP}"
        --profile-output "${prefix}"
        --profile-warmup-frames 0
    )

    if ! "${command[@]}" >"${log_path}" 2>&1; then
        printf 'gltf loading profile: lane failed: %s (%s)\n' "${lane}" "${relative_asset}" >&2
        printf 'No substitute will be selected. Inspect: %s\n' "${log_path}" >&2
        if [[ "${lane}" == 'large-scene' ]]; then
            printf 'Sponza failure requires an explicit architecture decision before continuing.\n' >&2
        else
            printf 'The asset may require a glTF feature that Cubey does not currently support.\n' >&2
        fi
        tail -n 40 "${log_path}" >&2 || true
        exit 1
    fi

    validate_png "${png_path}"
    rm -f "${png_path}"
    parse_metrics "${prefix}.metrics.csv" "${lane}" "${observation}" "${repeat}" "${relative_asset}"
}

write_runs_header() {
    {
        printf 'lane,observation,repeat,asset_relative_path'
        local metric
        for metric in "${METRIC_NAMES[@]}"; do
            printf ',%s' "${metric}"
        done
        printf '\n'
    } >"${ACTIVE_RUNS_CSV}"
}

write_summary_header() {
    {
        printf 'lane,asset_relative_path'
        local metric
        for metric in "${METRIC_NAMES[@]}"; do
            printf ',%s_first,%s_warm_median,%s_warm_min,%s_warm_max' \
                "${metric}" "${metric}" "${metric}" "${metric}"
        done
        printf '\n'
    } >"${ACTIVE_SUMMARY_CSV}"
}

write_summary_row() {
    local lane="$1"
    local relative_asset="$2"
    local expected_warm_runs="$3"
    awk -F, -v lane="${lane}" -v relative_asset="${relative_asset}" \
        -v expected_warm_runs="${expected_warm_runs}" \
        -v metric_count="${#METRIC_NAMES[@]}" '
        function median(values, count,    i, j, temp, middle) {
            for (i = 1; i <= count; ++i) {
                for (j = i + 1; j <= count; ++j) {
                    if (values[j] < values[i]) {
                        temp = values[i]
                        values[i] = values[j]
                        values[j] = temp
                    }
                }
            }
            middle = int((count + 1) / 2)
            if ((count % 2) == 1) {
                return values[middle]
            }
            return (values[middle] + values[middle + 1]) / 2.0
        }
        $1 == lane && $4 == relative_asset {
            ++runs
            if ($2 == "first") {
                ++first_runs
                for (metric = 1; metric <= metric_count; ++metric) {
                    first[metric] = $(metric + 4)
                }
            } else if ($2 == "warm") {
                ++warm_runs
                for (metric = 1; metric <= metric_count; ++metric) {
                    warm[metric, warm_runs] = $(metric + 4) + 0.0
                }
            }
        }
        END {
            if (runs != expected_warm_runs + 1 || first_runs != 1 || warm_runs != expected_warm_runs) {
                printf "summary input for %s is incomplete: runs=%d first=%d warm=%d expected_warm=%d\n", lane, runs, first_runs, warm_runs, expected_warm_runs > "/dev/stderr"
                exit 1
            }
            printf "%s,%s", lane, relative_asset
            for (metric = 1; metric <= metric_count; ++metric) {
                for (sorted_key in sorted) delete sorted[sorted_key]
                for (repeat = 1; repeat <= warm_runs; ++repeat) {
                    sorted[repeat] = warm[metric, repeat]
                }
                min = sorted[1]
                max = sorted[1]
                for (repeat = 2; repeat <= warm_runs; ++repeat) {
                    if (sorted[repeat] < min) min = sorted[repeat]
                    if (sorted[repeat] > max) max = sorted[repeat]
                }
                printf ",%s,%s,%s,%s", first[metric], median(sorted, warm_runs), min, max
            }
            printf "\n"
        }
    ' "${ACTIVE_RUNS_CSV}" >>"${ACTIVE_SUMMARY_CSV}"
}

validate_summary() {
    local expected_rows="${#MANIFEST_LANE_ORDER[@]}"
    local summary_rows
    summary_rows="$(( $(wc -l <"${ACTIVE_SUMMARY_CSV}") - 1 ))"
    [[ "${summary_rows}" -eq "${expected_rows}" ]] ||
        fail "summary.csv has ${summary_rows} rows; expected ${expected_rows}"
    local expected_columns=$((2 + (${#METRIC_NAMES[@]} * 4)))
    awk -F, -v expected_columns="${expected_columns}" \
        'NR == 1 && NF != expected_columns { exit 1 } NR > 1 && NF != expected_columns { exit 1 }' \
        "${ACTIVE_SUMMARY_CSV}" || fail 'summary.csv schema has an unexpected column count'
}

rebuild_reports() {
    ACTIVE_RUNS_CSV="${RUNS_CSV}.tmp.$$"
    ACTIVE_SUMMARY_CSV="${SUMMARY_CSV}.tmp.$$"
    write_runs_header

    local index
    for index in "${!MANIFEST_LANES[@]}"; do
        run_observation \
            "${MANIFEST_LANES[index]}" \
            "${MANIFEST_ASSETS[index]}" \
            "${MANIFEST_OBSERVATIONS[index]}" \
            "${MANIFEST_REPEATS[index]}" \
            "${MANIFEST_PREFIXES[index]}"
    done

    write_summary_header
    local lane
    for lane in "${MANIFEST_LANE_ORDER[@]}"; do
        write_summary_row "${lane}" "${MANIFEST_LANE_ASSET[${lane}]}" "${MANIFEST_WARM_REPEATS}"
    done
    validate_summary
    mv "${ACTIVE_RUNS_CSV}" "${RUNS_CSV}"
    mv "${ACTIVE_SUMMARY_CSV}" "${SUMMARY_CSV}"
}

main() {
    [[ "${SUMMARIZE_ONLY}" == '0' || "${SUMMARIZE_ONLY}" == '1' ]] ||
        fail 'SUMMARIZE_ONLY must be 0 or 1'

    if [[ "${SUMMARIZE_ONLY}" == '1' ]]; then
        [[ -d "${OUT_DIR}" ]] || fail "summary-only output directory is missing: ${OUT_DIR}"
        [[ -d "${PROFILES_DIR}" ]] || fail "summary-only profile directory is missing: ${PROFILES_DIR}"
        load_retained_inventory
        rebuild_reports
    else
        require_positive_integer REPEATS "${REPEATS}"
        require_positive_integer WIDTH "${WIDTH}"
        require_positive_integer HEIGHT "${HEIGHT}"
        validate_fresh_live_output_dir
        validate_asset_inventory
        validate_app
        mkdir -p "${PROFILES_DIR}" "${LOGS_DIR}"
        write_metadata
        write_manifest
        load_manifest || fail "newly written profile manifest is missing: ${MANIFEST_CSV}"
        rebuild_reports
    fi

    printf 'wrote %s\n' "${RUNS_CSV}"
    printf 'wrote %s\n' "${SUMMARY_CSV}"
    printf 'raw profiles: %s\n' "${PROFILES_DIR}"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    main "$@"
fi
