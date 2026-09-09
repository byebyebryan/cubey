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

write_metrics() {
    local profile_prefix="$1"
    local value_offset="$2"
    mkdir -p "$(dirname "$profile_prefix")"
    {
        printf 'frame_index,category,name,value\n'
        local index=1
        local metric
        for metric in generation_id source_file_bytes metadata_probe_ms asset_load_ms \
            scene_prepare_ms staged_worker_prepare_ms gltf_residency_ms \
            staged_gpu_install_ms activation_ms triangle_count node_count material_count \
            texture_count prepared_texture_count basisu_encoded_image_bytes decoded_rgba_bytes \
            prepared_texture_upload_bytes mesh_upload_bytes mesh_upload_transfer_submission_count; do
            printf '0,gltf_loading,%s,%s.000000\n' "$metric" "$((value_offset + index))"
            index=$((index + 1))
        done
    } >"$profile_prefix.metrics.csv"
}

write_retained_fixture() {
    local out_dir="$1"
    mkdir -p "$out_dir/profiles"
    printf 'metadata-must-remain-untouched\n' >"$out_dir/metadata.txt"
    {
        printf 'schema,warm_repeats,lane,observation,repeat,asset_relative_path,profile_prefix\n'
        printf '%s,2,fixture-lane,first,0,Models/Fixture/fixture.glb,profiles/fixture-lane-first\n' \
            "$MANIFEST_SCHEMA"
        printf '%s,2,fixture-lane,warm,1,Models/Fixture/fixture.glb,profiles/fixture-lane-warm-1\n' \
            "$MANIFEST_SCHEMA"
        printf '%s,2,fixture-lane,warm,2,Models/Fixture/fixture.glb,profiles/fixture-lane-warm-2\n' \
            "$MANIFEST_SCHEMA"
    } >"$out_dir/profile-manifest.csv"
    write_metrics "$out_dir/profiles/fixture-lane-first" 0
    write_metrics "$out_dir/profiles/fixture-lane-warm-1" 100
    write_metrics "$out_dir/profiles/fixture-lane-warm-2" 200
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

printf 'gltf loading profile script tests passed\n'
