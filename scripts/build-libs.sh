#!/usr/bin/env bash
# Builds the PS5 libraries EasyRPG Player needs, using the recipes from
# https://github.com/ps5-payload-dev/pacbrew-repo and installs them into the PS5 payload SDK.
#
# Run inside Ubuntu (WSL) as root. makepkg refuses to run as root, so each package is built as
# $BUILD_USER and installed with pacman as root. Dependencies are already satisfied by build order,
# so pacman's dependency checks are skipped (-dd / -d).
#
# Usage: build-libs.sh [package ...]   (defaults to the list below)
set -u

BUILD_USER="${BUILD_USER:-${SUDO_USER:-$USER}}"
REPO="${REPO:-/home/$BUILD_USER/pacbrew-repo}"
export PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}"
export MAKEFLAGS="${MAKEFLAGS:--j$(nproc)}"
# The SDK's clang wrapper doesn't search the homebrew prefix where these packages install their
# headers and libraries, so older autotools recipes (libpng, ...) can't find zlib.h without this.
export CPATH="$PS5_PAYLOAD_SDK/target/user/homebrew/include"
export LIBRARY_PATH="$PS5_PAYLOAD_SDK/target/user/homebrew/lib"

# Recipes that are not in pacbrew-repo live in ../packages next to this script (pixman, liblcf, ...).
# Copy them into the pacbrew tree so the loop below treats them like any other package.
EXTRA="${EXTRA:-$(dirname "$(realpath "$0")")/../packages}"
if [ -d "$EXTRA" ]; then
    for d in "$EXTRA"/*/; do
        name="$(basename "$d")"
        mkdir -p "$REPO/$name"
        cp -r "$d". "$REPO/$name/"
        chown -R "$BUILD_USER" "$REPO/$name"
    done
fi

DEFAULT_PKGS=(zlib fmt libpng expat libiconv freetype harfbuzz
              libsamplerate libogg libvorbis opus mpg123 flac libsndfile
              SDL2 SDL2_mixer)
PKGS=("${@:-${DEFAULT_PKGS[@]}}")

for PKG in "${PKGS[@]}"; do
    echo "=== building $PKG ($(date +%T)) ==="
    su "$BUILD_USER" -c "cd '$REPO/$PKG' && rm -f *.pkg.tar.* && rm -rf src pkg && \
        PS5_PAYLOAD_SDK='$PS5_PAYLOAD_SDK' MAKEFLAGS='$MAKEFLAGS' CPATH='$CPATH' \
        LIBRARY_PATH='$LIBRARY_PATH' makepkg -d -c -f -C" \
        || { echo "!!! build of $PKG failed"; exit 1; }
    pacman --config "$REPO/pacman.conf" --noconfirm -Udd "$REPO/$PKG"/ps5-payload-*.pkg.tar.* \
        || { echo "!!! install of $PKG failed"; exit 1; }
    case "$PKG" in
        # expat only builds a static library but installs a CMake config that references a missing
        # libexpat.so, which breaks every find_package(EXPAT) that finds it. Drop that config.
        expat) rm -rf "$PS5_PAYLOAD_SDK"/target/user/homebrew/lib/cmake/expat-* ;;
    esac
done
echo "=== all done ($(date +%T)) ==="
