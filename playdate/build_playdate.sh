#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

export PLAYDATE_SDK_PATH="${PLAYDATE_SDK_PATH:-/home/antigravity/PlaydateSDK}"
export PATH="/home/antigravity/arm-gcc/bin:${PLAYDATE_SDK_PATH}/bin:${PATH}"

echo "==> [1/3] Building Playdate Simulator target (pdex.so)..."
cmake -B build_sim -DCMAKE_BUILD_TYPE=Release
cmake --build build_sim --parallel "$(nproc)"

echo "==> [2/3] Building Playdate ARM Cortex-M7 Device target (pdex.bin)..."
cmake -B build_device -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="${PLAYDATE_SDK_PATH}/C_API/buildsupport/arm.cmake"
cmake --build build_device --parallel "$(nproc)"

echo "==> [3/3] Packaging universal CrossPoint.pdx (Simulator + Device)..."
"${PLAYDATE_SDK_PATH}/bin/pdc" -sdkpath "${PLAYDATE_SDK_PATH}" Source CrossPoint.pdx
rm -rf CrossPoint_DEVICE.pdx
ls -lh CrossPoint.pdx
