#!/usr/bin/env bash
# Install the eyeq binary system-wide. Builds first if needed.
#
# Usage:
#   scripts/install.sh                  # installs to /usr/local/bin (asks for sudo)
#   scripts/install.sh --prefix DIR     # installs to DIR/bin (no sudo if writable)
#   scripts/install.sh --build-dir DIR  # use an existing build directory

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="/usr/local"
BUILD="$ROOT/build"

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix)    PREFIX="$2"; shift 2 ;;
        --build-dir) BUILD="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if [ ! -x "$BUILD/eyeq" ]; then
    echo ">>> No build at $BUILD; configuring and building..."
    cmake -S "$ROOT" -B "$BUILD"
    cmake --build "$BUILD" -j
fi

# Use sudo only if the install prefix isn't user-writable.
SUDO=""
if [ ! -w "$PREFIX" ] && [ ! -w "$(dirname "$PREFIX")" ]; then
    SUDO="sudo"
fi

echo ">>> Installing to $PREFIX/bin/eyeq"
$SUDO cmake --install "$BUILD" --prefix "$PREFIX"
