#!/usr/bin/env bash
set -euo pipefail

VMAF_TAG="${VMAF_TAG:-v3.0.0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/third_party/vmaf-src"
BUILD="$SRC/libvmaf/build"
PREFIX="$ROOT/third_party/vmaf-install"

if [ -f "$PREFIX/lib/pkgconfig/libvmaf.pc" ]; then
    echo "libvmaf already installed at $PREFIX"
    exit 0
fi

missing=()
for tool in meson ninja nasm git; do
    command -v "$tool" >/dev/null || missing+=("$tool")
done
if [ ${#missing[@]} -ne 0 ]; then
    echo "Missing build tools: ${missing[*]}" >&2
    echo "On Ubuntu: sudo apt install meson ninja-build nasm git" >&2
    exit 1
fi

mkdir -p "$ROOT/third_party"

if [ ! -d "$SRC/.git" ]; then
    git clone --depth 1 --branch "$VMAF_TAG" https://github.com/Netflix/vmaf.git "$SRC"
fi

rm -rf "$BUILD"
meson setup "$BUILD" "$SRC/libvmaf" \
    --buildtype=release \
    --prefix="$PREFIX" \
    --libdir=lib \
    --default-library=static
ninja -C "$BUILD"
ninja -C "$BUILD" install

echo "libvmaf installed to $PREFIX"
