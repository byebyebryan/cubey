#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BUILD_DIR=${BUILD_DIR:-"${ROOT_DIR}/build/dev"}
APP=${APP:-"${BUILD_DIR}/projects/planet/planet"}
OUT_DIR=${1:-"${ROOT_DIR}/outputs/planet/orbital-v1"}

cmake --build "${BUILD_DIR}" --target cubey_project_planet -j 4
rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}"

capture() {
    name=$1
    shift
    "${APP}" --headless --width 1600 --height 900 --planet-camera-mode orbit \
        --planet-terrain-seed 1337 --planet-disk-coverage 0.48 \
        --planet-surface-quality standard --output "${OUT_DIR}/${name}.png" "$@"
}

capture lit --planet-view lit
capture terminator --planet-view terminator
capture crescent --planet-view crescent
capture night --planet-view night
capture land --debug-view land --planet-view lit
capture elevation --debug-view elevation --planet-view lit
capture ice --debug-view ice --planet-view lit
capture albedo --debug-view albedo --planet-view lit

printf 'file\tview\n' >"${OUT_DIR}/manifest.tsv"
printf 'lit.png\tlit\n' >>"${OUT_DIR}/manifest.tsv"
printf 'terminator.png\tterminator\n' >>"${OUT_DIR}/manifest.tsv"
printf 'crescent.png\tcrescent\n' >>"${OUT_DIR}/manifest.tsv"
printf 'night.png\tnight\n' >>"${OUT_DIR}/manifest.tsv"
printf 'land.png\tland mask debug\n' >>"${OUT_DIR}/manifest.tsv"
printf 'elevation.png\televation debug\n' >>"${OUT_DIR}/manifest.tsv"
printf 'ice.png\tice mask debug\n' >>"${OUT_DIR}/manifest.tsv"
printf 'albedo.png\tbase-color debug\n' >>"${OUT_DIR}/manifest.tsv"
printf 'planet orbital review: %s\n' "${OUT_DIR}"
