#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

export PLAYDATE_SDK_PATH="${PLAYDATE_SDK_PATH:-/home/antigravity/PlaydateSDK}"
export PATH="/home/antigravity/arm-gcc/bin:${PLAYDATE_SDK_PATH}/bin:${PATH}"

GIT_SHA="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
echo "==> Building CrossPoint for Playdate (version: 2.0.0-playdate-${GIT_SHA})"

echo "==> [1/4] Building Playdate Simulator target (pdex.so)..."
cmake -B build_sim -DCMAKE_BUILD_TYPE=Release -DCROSSPOINT_GIT_SHA="${GIT_SHA}"
cmake --build build_sim --parallel "$(nproc)"

echo "==> [2/4] Building Playdate ARM Cortex-M7 Device target (pdex.bin)..."
cmake -B build_device -DCMAKE_BUILD_TYPE=Release \
  -DCROSSPOINT_GIT_SHA="${GIT_SHA}" \
  -DCMAKE_TOOLCHAIN_FILE="${PLAYDATE_SDK_PATH}/C_API/buildsupport/arm.cmake"
cmake --build build_device --parallel "$(nproc)"

echo "==> [3/4] Generating the bundled manual (with licence notices)..."
python3 tools/build_manual.py --deps-dir build_sim/_deps \
  --output Source/manual/CrossPoint_Manual.epub --stamp Source/manual/manual.stamp

echo "==> [4/4] Packaging universal CrossPoint.pdx (Simulator + Device)..."
"${PLAYDATE_SDK_PATH}/bin/pdc" -sdkpath "${PLAYDATE_SDK_PATH}" Source CrossPoint.pdx
rm -rf CrossPoint_DEVICE.pdx
ls -lh CrossPoint.pdx
