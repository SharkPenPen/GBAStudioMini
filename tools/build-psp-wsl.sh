#!/usr/bin/env bash
# tools/build-psp-wsl.sh
#
# 在 WSL2 Ubuntu 里构建 PSP 版。
# 前置：bash tools/setup-psp-wsl.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PSPDEV="${PSPDEV:-$HOME/pspdev}"
export PATH="$PATH:$PSPDEV/bin"

if ! command -v psp-gcc >/dev/null 2>&1; then
  echo "psp-gcc not found; run tools/setup-psp-wsl.sh first." >&2
  exit 2
fi

BUILD="$ROOT/build/psp-wsl"
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchain-psp.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD"

echo
echo "ELF  : $BUILD/bin/game_psp.elf"
if [ -f "$BUILD/EBOOT.PBP" ]; then
  echo "EBOOT: $BUILD/EBOOT.PBP"
fi
