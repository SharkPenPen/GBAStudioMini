#!/usr/bin/env bash
# tools/setup-psp-wsl.sh
#
# 在 WSL2 Ubuntu 里安装 pspdev。
# 用法：
#   sudo apt-get update
#   bash tools/setup-psp-wsl.sh
#
# 注意：Windows 原生没有官方 pspdev；必须在 WSL2/Ubuntu 或 Linux 机器上编。
set -euo pipefail

if ! command -v sudo >/dev/null 2>&1; then
  echo "sudo not found; run this inside WSL2 Ubuntu." >&2
  exit 2
fi

sudo apt-get update

# 官方依赖；Ubuntu 24.04 的 readline/gpgme 包名可能带 t64，失败时回退。
sudo apt-get install -y \
  build-essential cmake pkgconf libreadline8 libusb-0.1 libgpgme11 \
  libarchive-tools fakeroot wget || \
sudo apt-get install -y \
  build-essential cmake pkgconf libreadline8t64 libusb-0.1-4 libgpgme11t64 \
  libarchive-tools fakeroot wget

if ! command -v psp-gcc >/dev/null 2>&1; then
  TMP="$(mktemp -d)"
  echo "Downloading pspdev..."
  wget -O "$TMP/pspdev.tar.gz" \
    https://github.com/pspdev/pspdev/releases/latest/download/pspdev-ubuntu-latest-x86_64.tar.gz
  tar -xvf "$TMP/pspdev.tar.gz" -C "$HOME"
  rm -rf "$TMP"
fi

if ! grep -q 'PSPDEV=' "$HOME/.bashrc"; then
  {
    echo ''
    echo '# pspdev'
    echo 'export PSPDEV="$HOME/pspdev"'
    echo 'export PATH="$PATH:$PSPDEV/bin"'
  } >> "$HOME/.bashrc"
fi

export PSPDEV="$HOME/pspdev"
export PATH="$PATH:$PSPDEV/bin"

if ! command -v psp-config >/dev/null 2>&1; then
  echo "psp-config still not found. Check $PSPDEV/bin." >&2
  exit 3
fi

echo "PSPDEV=$PSPDEV"
psp-config --pspdev-path
