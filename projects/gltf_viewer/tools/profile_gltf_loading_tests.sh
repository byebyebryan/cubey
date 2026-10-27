#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RUNNER="$SCRIPT_DIR/profile_gltf_loading.sh"
TEST_ROOT="$(mktemp -d "/tmp/cubey-gltf-loading-profile-test.XXXXXX")"

cleanup() {
    rm -rf -- "$TEST_ROOT"
}
trap cleanup EXIT

fail() {
    printf 'gltf loading profile test: %s\n' "$*" >&2
    exit 1
}

expect_failure() {
    local name="$1"
    shift
    if "$@" >"$TEST_ROOT/$name.log" 2>&1; then
        fail "expected $name to fail"
    fi
}

# shellcheck disable=SC1091
# shellcheck source=profile_gltf_loading.sh
source "$RUNNER"

# Read current fixture names from the native emitter, rather than mirroring the
# runner's list: extending the emitter must expose a stale reader in this test.
mapfile -t CURRENT_METRIC_NAMES < <(
    sed -n '/^void record_metrics(/,/^}/p' "$SCRIPT_DIR/../gltf_viewer_loading_metrics.cpp" |
        sed -n 's/.*"\([a-z0-9_]*\)".*/\1/p'
)
[[ "${#CURRENT_METRIC_NAMES[@]}" -eq 48 ]] || fail 'unexpected native metric inventory'

manifest_out="$TEST_ROOT/new-manifest"
mkdir -p "$manifest_out"
bash -c 'source "$1"; MANIFEST_CSV="$2/profile-manifest.csv"; LANE_NAMES=(fixture-lane); LANE_PATHS=(Models/Fixture/fixture.glb); REPEATS=2; write_manifest; load_manifest; [[ "$LOADED_MANIFEST_SCHEMA" == gltf_loading_profile_v2 && "${#METRIC_NAMES[@]}" -eq 48 && "${#MANIFEST_LANES[@]}" -eq 3 ]]' \
    bash "$RUNNER" "$manifest_out" || fail 'new live manifests did not select the current inventory'

write_metrics() {
    local profile_prefix="$1"
    local value_offset="$2"
    local schema="${3:-$LEGACY_MANIFEST_SCHEMA}"
    mkdir -p "$(dirname "$profile_prefix")"
    {
        printf 'frame_index,category,name,value\n'
        local index=1
        local metric
        local historical_names=(generation_id source_file_bytes metadata_probe_ms asset_load_ms \
            document_parse_ms buffer_load_ms asset_validate_ms image_payload_ms \
            image_decode_ms asset_assembly_ms scene_prepare_ms staged_worker_prepare_ms gltf_residency_ms \
            staged_gpu_install_ms activation_ms triangle_count node_count material_count \
            texture_count prepared_texture_count basisu_encoded_image_bytes decoded_rgba_bytes \
            prepared_texture_upload_bytes mesh_upload_bytes mesh_upload_transfer_submission_count \
            gpu_upload_bytes gpu_upload_copy_count gpu_upload_owner_advance_count \
            gpu_upload_step_count gpu_upload_submission_count gpu_upload_owner_submit_ms gpu_upload_owner_max_step_ms \
            gpu_upload_owner_target_ms gpu_upload_step_byte_cap gpu_upload_copy_byte_target \
            gpu_upload_owner_over_target_step_count \
            gpu_upload_completion_latency_ms gpu_upload_pool_initial_capacity_bytes \
            gpu_upload_pool_final_capacity_bytes gpu_upload_pool_peak_capacity_bytes \
            gpu_upload_pool_reserved_at_final_submission_bytes gpu_upload_pool_growth_count \
            gpu_upload_backpressure_count gpu_upload_first_step_to_final_completion_ms \
            gpu_upload_submission_frame gpu_upload_completion_frame)
        local names=("${historical_names[@]}")
        if [[ "$schema" == "$MANIFEST_SCHEMA" ]]; then
            names=("${CURRENT_METRIC_NAMES[@]}")
        fi
        for metric in "${names[@]}"; do
            printf '0,gltf_loading,%s,%s.000000\n' "$metric" "$((value_offset + index))"
            index=$((index + 1))
        done
    } >"$profile_prefix.metrics.csv"
}

write_retained_fixture() {
    local out_dir="$1"
    local schema="${2:-$LEGACY_MANIFEST_SCHEMA}"
    mkdir -p "$out_dir/profiles"
    printf 'metadata-must-remain-untouched\n' >"$out_dir/metadata.txt"
    {
        printf 'schema,warm_repeats,lane,observation,repeat,asset_relative_path,profile_prefix\n'
        printf '%s,2,fixture-lane,first,0,Models/Fixture/fixture.glb,profiles/fixture-lane-first\n' \
            "$schema"
        printf '%s,2,fixture-lane,warm,1,Models/Fixture/fixture.glb,profiles/fixture-lane-warm-1\n' \
            "$schema"
        printf '%s,2,fixture-lane,warm,2,Models/Fixture/fixture.glb,profiles/fixture-lane-warm-2\n' \
            "$schema"
    } >"$out_dir/profile-manifest.csv"
    write_metrics "$out_dir/profiles/fixture-lane-first" 0 "$schema"
    write_metrics "$out_dir/profiles/fixture-lane-warm-1" 100 "$schema"
    write_metrics "$out_dir/profiles/fixture-lane-warm-2" 200 "$schema"
}

valid_out="$TEST_ROOT/valid-retained"
write_retained_fixture "$valid_out"
cp "$valid_out/metadata.txt" "$TEST_ROOT/metadata.before"
cp "$valid_out/profile-manifest.csv" "$TEST_ROOT/manifest.before"
env -u REPEATS \
    APP="$TEST_ROOT/missing-viewer" \
    ASSET_ROOT="$TEST_ROOT/missing-sample-assets" \
    SUMMARIZE_ONLY=1 \
    "$RUNNER" "$valid_out" >"$TEST_ROOT/summary-replay.log" 2>&1
cmp "$TEST_ROOT/metadata.before" "$valid_out/metadata.txt" ||
    fail 'summary replay rewrote metadata'
cmp "$TEST_ROOT/manifest.before" "$valid_out/profile-manifest.csv" ||
    fail 'summary replay rewrote the manifest'
[[ "$(wc -l <"$valid_out/runs.csv")" -eq 4 ]] ||
    fail 'summary replay did not use the manifest-recorded first plus two warm observations'
grep -q '^fixture-lane,warm,2,' "$valid_out/runs.csv" ||
    fail 'summary replay ignored the manifest-recorded second warm observation'
[[ "$(awk -F, 'NR == 1 {print NF}' "$valid_out/runs.csv")" -eq 50 ]] ||
    fail 'historical replay changed the 46-metric report schema'

current_out="$TEST_ROOT/current-retained"
write_retained_fixture "$current_out" "$MANIFEST_SCHEMA"
cp "$current_out/metadata.txt" "$TEST_ROOT/current-metadata.before"
cp "$current_out/profile-manifest.csv" "$TEST_ROOT/current-manifest.before"
env REPEATS=99 APP="$TEST_ROOT/missing-viewer" ASSET_ROOT="$TEST_ROOT/missing-sample-assets" \
    SUMMARIZE_ONLY=1 "$RUNNER" "$current_out" >"$TEST_ROOT/current-replay.log" 2>&1
cmp "$TEST_ROOT/current-metadata.before" "$current_out/metadata.txt" || fail 'current replay rewrote metadata'
cmp "$TEST_ROOT/current-manifest.before" "$current_out/profile-manifest.csv" || fail 'current replay rewrote manifest'
[[ "$(wc -l <"$current_out/runs.csv")" -eq 4 ]] || fail 'current replay ignored recorded repetitions'
[[ "$(awk -F, 'NR == 1 {print NF}' "$current_out/runs.csv")" -eq 52 ]] || fail 'current replay lost metrics'
[[ "$(awk -F, 'NR == 1 {print NF}' "$current_out/summary.csv")" -eq 194 ]] || fail 'current summary lost metrics'
awk -F, 'NR == 1 {ok = ($51 == "default_texture_logical_binding_count" && $52 == "default_texture_physical_upload_count")} NR == 2 {ok = ok && ($51 == 47 && $52 == 48)} END {exit !ok}' \
    "$current_out/runs.csv" || fail 'current reports lost native default-texture metrics'
awk -F, 'NR == 2 {exit !($187 == 47 && $188 == 197 && $189 == 147 && $190 == 247 && $191 == 48 && $192 == 198 && $193 == 148 && $194 == 248)}' \
    "$current_out/summary.csv" || fail 'current summary miscomputed default-texture observations'

# Exercise the live observation path with an isolated stand-in process; no
# renderer, GPU or asset checkout is needed to prove its parser contract.
stub_app="$TEST_ROOT/viewer-stub"
cat >"$stub_app" <<'STUB'
#!/usr/bin/env bash
set -euo pipefail
while (($#)); do
    case "$1" in
        --profile-output) prefix="$2"; shift 2 ;;
        --output) png="$2"; shift 2 ;;
        *) shift ;;
    esac
done
cp "$FIXTURE_METRICS" "$prefix.metrics.csv"
printf '%s' 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jI5sAAAAASUVORK5CYII=' |
    base64 --decode >"$png"
STUB
chmod +x "$stub_app"
live_out="$TEST_ROOT/live-observation"
mkdir -p "$live_out/profiles" "$live_out/logs"
# The script expressions below expand inside the child Bash.
# shellcheck disable=SC2016
env APP="$stub_app" FIXTURE_METRICS="$current_out/profiles/fixture-lane-first.metrics.csv" \
    bash -c 'source "$1"; OUT_DIR="$2"; LOGS_DIR="$OUT_DIR/logs"; ACTIVE_RUNS_CSV="$OUT_DIR/runs.csv"; run_observation fixture-lane fixture.glb first 0 profiles/fixture-lane-first' \
    bash "$RUNNER" "$live_out"
[[ "$(awk -F, 'NR == 1 {print NF}' "$live_out/runs.csv")" -eq 52 ]] || fail 'live reader lost current metrics'
# shellcheck disable=SC2016
expect_failure historical-metrics-in-live-run \
    env APP="$stub_app" FIXTURE_METRICS="$valid_out/profiles/fixture-lane-first.metrics.csv" \
    bash -c 'source "$1"; OUT_DIR="$2"; LOGS_DIR="$OUT_DIR/logs"; ACTIVE_RUNS_CSV="$OUT_DIR/rejected.csv"; run_observation fixture-lane fixture.glb first 0 profiles/fixture-lane-first' \
    bash "$RUNNER" "$live_out"
grep -q 'expected exactly 48 .*found 46' "$TEST_ROOT/historical-metrics-in-live-run.log" || fail 'live schema failure was unrelated'

legacy_out="$TEST_ROOT/legacy-metadata"
cp -a "$valid_out" "$legacy_out"
rm "$legacy_out/profile-manifest.csv"
{
    printf 'sample_assets_declared_pin=%s\nsample_assets_head=%s\nsample_assets_worktree=clean\nwarm_repeats=2\n' "$SAMPLE_ASSETS_PIN" "$SAMPLE_ASSETS_PIN"
    printf '\n[lane_assets]\nlane,relative_path,size_bytes,sha256\nfixture-lane,Models/Fixture/fixture.glb,1,%064d\n' 0
} >"$legacy_out/metadata.txt"
cp "$legacy_out/metadata.txt" "$TEST_ROOT/legacy-metadata.before"
env APP="$TEST_ROOT/missing-viewer" ASSET_ROOT="$TEST_ROOT/missing-sample-assets" \
    SUMMARIZE_ONLY=1 "$RUNNER" "$legacy_out" >"$TEST_ROOT/legacy-replay.log" 2>&1
cmp "$valid_out/runs.csv" "$legacy_out/runs.csv" || fail 'legacy metadata replay changed reports'
cmp "$valid_out/summary.csv" "$legacy_out/summary.csv" || fail 'legacy metadata replay changed summaries'
cmp "$TEST_ROOT/legacy-metadata.before" "$legacy_out/metadata.txt" || fail 'legacy metadata was rewritten'
[[ ! -e "$legacy_out/profile-manifest.csv" ]] || fail 'legacy replay fabricated a manifest'

nonempty_out="$TEST_ROOT/nonempty-live-output"
mkdir -p "$nonempty_out"
printf 'retain me\n' >"$nonempty_out/sentinel"
expect_failure nonempty-live-output \
    env APP="$TEST_ROOT/missing-viewer" ASSET_ROOT="$TEST_ROOT/missing-sample-assets" \
    "$RUNNER" "$nonempty_out"
grep -qx 'retain me' "$nonempty_out/sentinel" ||
    fail 'non-empty output sentinel changed'
[[ ! -e "$nonempty_out/metadata.txt" && ! -e "$nonempty_out/profile-manifest.csv" ]] ||
    fail 'non-empty live output rejection mutated evidence'

non_git_root="$TEST_ROOT/non-git-corpus"
mkdir -p "$non_git_root"
expect_failure missing-git-identity \
    env APP="$TEST_ROOT/missing-viewer" ASSET_ROOT="$non_git_root" \
    "$RUNNER" "$TEST_ROOT/missing-git-output"
[[ ! -e "$TEST_ROOT/missing-git-output" ]] ||
    fail 'missing Git corpus identity created output evidence'

wrong_root="$TEST_ROOT/wrong-pin-corpus"
git init --quiet "$wrong_root"
git -C "$wrong_root" config user.email gltf-profile-test@example.invalid
git -C "$wrong_root" config user.name gltf-profile-test
printf 'fixture\n' >"$wrong_root/tracked"
git -C "$wrong_root" add tracked
git -C "$wrong_root" commit --quiet -m fixture
expect_failure wrong-git-pin \
    env APP="$TEST_ROOT/missing-viewer" ASSET_ROOT="$wrong_root" \
    "$RUNNER" "$TEST_ROOT/wrong-git-output"

dirty_root="$TEST_ROOT/dirty-corpus"
git init --quiet "$dirty_root"
git -C "$dirty_root" config user.email gltf-profile-test@example.invalid
git -C "$dirty_root" config user.name gltf-profile-test
printf 'tracked\n' >"$dirty_root/tracked"
git -C "$dirty_root" add tracked
git -C "$dirty_root" commit --quiet -m fixture
dirty_head="$(git -C "$dirty_root" rev-parse HEAD)"
printf 'dirty\n' >>"$dirty_root/tracked"
# shellcheck disable=SC2016
expect_failure dirty-git-corpus \
    bash -c 'source "$1"; SAMPLE_ASSETS_PIN="$2"; validate_pinned_sample_assets_checkout "$3"' \
    bash "$RUNNER" "$dirty_head" "$dirty_root"

duplicate_manifest_out="$TEST_ROOT/duplicate-manifest"
cp -a "$valid_out" "$duplicate_manifest_out"
duplicate_manifest_row="$(tail -n 1 "$duplicate_manifest_out/profile-manifest.csv")"
printf '%s\n' "$duplicate_manifest_row" >>"$duplicate_manifest_out/profile-manifest.csv"
expect_failure duplicate-manifest \
    env -u REPEATS APP="$TEST_ROOT/missing-viewer" ASSET_ROOT="$TEST_ROOT/missing-sample-assets" \
    SUMMARIZE_ONLY=1 "$RUNNER" "$duplicate_manifest_out"

duplicate_profile_out="$TEST_ROOT/duplicate-profile"
cp -a "$valid_out" "$duplicate_profile_out"
duplicate_metric_row="$(sed -n '2p' "$duplicate_profile_out/profiles/fixture-lane-first.metrics.csv")"
printf '%s\n' "$duplicate_metric_row" \
    >>"$duplicate_profile_out/profiles/fixture-lane-first.metrics.csv"
expect_failure duplicate-profile-metric \
    env -u REPEATS APP="$TEST_ROOT/missing-viewer" ASSET_ROOT="$TEST_ROOT/missing-sample-assets" \
    SUMMARIZE_ONLY=1 "$RUNNER" "$duplicate_profile_out"

missing_phase_out="$TEST_ROOT/missing-phase-profile"
cp -a "$valid_out" "$missing_phase_out"
sed -i '/,asset_assembly_ms,/d' \
    "$missing_phase_out/profiles/fixture-lane-first.metrics.csv"
expect_failure missing-profile-phase \
    env -u REPEATS APP="$TEST_ROOT/missing-viewer" ASSET_ROOT="$TEST_ROOT/missing-sample-assets" \
    SUMMARIZE_ONLY=1 "$RUNNER" "$missing_phase_out"

expect_current_replay_failure() {
    local name="$1" expected="$2" out_dir="$3"
    expect_failure "$name" \
        env APP="$TEST_ROOT/missing-viewer" ASSET_ROOT="$TEST_ROOT/missing-sample-assets" \
        SUMMARIZE_ONLY=1 "$RUNNER" "$out_dir"
    grep -q "$expected" "$TEST_ROOT/$name.log" || fail "$name failed for an unrelated reason"
    cmp "$current_out/runs.csv" "$out_dir/runs.csv" || fail "$name replaced a valid runs report"
    cmp "$current_out/summary.csv" "$out_dir/summary.csv" || fail "$name replaced a valid summary"
}

missing_current_out="$TEST_ROOT/current-missing-pair"
cp -a "$current_out" "$missing_current_out"
sed -i '/,default_texture_.*_count,/d' "$missing_current_out/profiles/fixture-lane-first.metrics.csv"
expect_current_replay_failure current-missing-pair 'expected exactly 48 .*found 46' "$missing_current_out"

partial_current_out="$TEST_ROOT/current-missing-one"
cp -a "$current_out" "$partial_current_out"
sed -i '/,default_texture_physical_upload_count,/d' "$partial_current_out/profiles/fixture-lane-first.metrics.csv"
expect_current_replay_failure current-missing-one 'expected exactly 48 .*found 47' "$partial_current_out"

unknown_current_out="$TEST_ROOT/current-unknown-metric"
cp -a "$current_out" "$unknown_current_out"
sed -i 's/default_texture_physical_upload_count/unknown_metric/' "$unknown_current_out/profiles/fixture-lane-first.metrics.csv"
expect_current_replay_failure current-unknown-metric 'missing gltf_loading metric' "$unknown_current_out"

duplicate_current_out="$TEST_ROOT/current-duplicate"
cp -a "$current_out" "$duplicate_current_out"
sed -n '2p' "$current_out/profiles/fixture-lane-first.metrics.csv" >>"$duplicate_current_out/profiles/fixture-lane-first.metrics.csv"
expect_current_replay_failure current-duplicate 'duplicate gltf_loading metric' "$duplicate_current_out"

multiframe_out="$TEST_ROOT/current-multiple-frames"
cp -a "$current_out" "$multiframe_out"
sed -i '3s/^0,/1,/' "$multiframe_out/profiles/fixture-lane-first.metrics.csv"
expect_current_replay_failure current-multiple-frames 'span multiple frames' "$multiframe_out"

mixed_profiles_out="$TEST_ROOT/current-mixed-profiles"
cp -a "$current_out" "$mixed_profiles_out"
write_metrics "$mixed_profiles_out/profiles/fixture-lane-warm-1" 100 "$LEGACY_MANIFEST_SCHEMA"
expect_current_replay_failure current-mixed-profiles 'expected exactly 48 .*found 46' "$mixed_profiles_out"

mixed_schema_out="$TEST_ROOT/mixed-manifest-schemas"
cp -a "$current_out" "$mixed_schema_out"
sed -i "3s/$MANIFEST_SCHEMA/$LEGACY_MANIFEST_SCHEMA/" "$mixed_schema_out/profile-manifest.csv"
expect_current_replay_failure mixed-manifest-schemas 'inconsistent metric schemas' "$mixed_schema_out"

unknown_schema_out="$TEST_ROOT/unknown-manifest-schema"
cp -a "$current_out" "$unknown_schema_out"
sed -i "s/$MANIFEST_SCHEMA/gltf_loading_profile_v999/" "$unknown_schema_out/profile-manifest.csv"
expect_current_replay_failure unknown-manifest-schema 'unsupported profile manifest schema' "$unknown_schema_out"

legacy_extra_out="$TEST_ROOT/legacy-extra-metrics"
cp -a "$current_out" "$legacy_extra_out"
sed -i "s/$MANIFEST_SCHEMA/$LEGACY_MANIFEST_SCHEMA/" "$legacy_extra_out/profile-manifest.csv"
expect_current_replay_failure legacy-extra-metrics 'expected exactly 46 .*found 48' "$legacy_extra_out"

printf 'gltf loading profile script tests passed\n'
