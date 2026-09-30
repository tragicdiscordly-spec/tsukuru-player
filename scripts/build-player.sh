#!/usr/bin/env bash
# Configures and builds EasyRPG Player for the PS5. Run inside Ubuntu (WSL) as the build user
# (not root; git refuses to read repositories owned by another user).
#
# Usage: build-player.sh [path-to-Player-checkout] [path-to-build-dir]
set -eu

export PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}"
SRC="${1:-$HOME/easyrpg/Player}"
BUILD="${2:-$HOME/easyrpg/build-player-ps5}"

# The SDK's clang wrapper does not search the homebrew prefix that the libraries install into.
export CPATH="$PS5_PAYLOAD_SDK/target/user/homebrew/include"
export LIBRARY_PATH="$PS5_PAYLOAD_SDK/target/user/homebrew/lib"

source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"

# Apply our patches to the Player checkout (skipping the ones that are already applied).
PATCHES="$(dirname "$(realpath "$0")")/../patches"
for p in "$PATCHES"/*.patch; do
    [ -e "$p" ] || continue
    if git -C "$SRC" apply --check -R "$p" 2>/dev/null; then
        echo "patch already applied: $(basename "$p")"
    else
        git -C "$SRC" apply "$p"
        echo "applied patch: $(basename "$p")"
    fi
done

"$CMAKE" -S "$SRC" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DPLAYER_TARGET_PLATFORM=SDL2 \
    -DPLAYER_AUDIO_BACKEND=SDL2 \
    -DPLAYER_AUDIO_RESAMPLER=samplerate \
    -DCMAKE_CXX_STANDARD_LIBRARIES=-lbz2 \
    -DCMAKE_CXX_FLAGS=-DU_STATIC_IMPLEMENTATION \
    -DPLAYER_ENABLE_TESTS=OFF \
    -DPLAYER_WITH_LHASA=OFF \
    -DPLAYER_WITH_NLOHMANN_JSON=OFF \
    -DPLAYER_WITH_NATIVE_MIDI=OFF \
    -DPLAYER_WITH_WILDMIDI=OFF \
    -DPLAYER_WITH_FLUIDSYNTH=OFF \
    -DPLAYER_WITH_FLUIDLITE=OFF \
    -DPLAYER_WITH_XMP=OFF

"$CMAKE" --build "$BUILD" -j"$(nproc)"
ls -l "$BUILD"/easyrpg-player* 2>/dev/null || true
