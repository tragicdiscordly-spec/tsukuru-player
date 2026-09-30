#!/usr/bin/env bash
# Builds Outsider (RPG Maker MZ runtime, https://github.com/GeneralArcade/outsider or wherever your
# checkout came from) for the PS5. Run inside Ubuntu (WSL) as the build user. Needs the libraries from
# build-libs.sh (SDL2 and quickjs-ng at least; llvm and mesa only for running).
#
# Usage: build-outsider.sh [outsider-checkout] [soloud-checkout] [build-dir]
#
# Defaults: ~/easyrpg/mv/outsider, ~/easyrpg/mv/soloud, <outsider-checkout>/build-ps5.
# The result is <build-dir>/outsider (an ELF for /data/homebrew/outsider/eboot.elf).
set -eu

export PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}"
SRC="${1:-$HOME/easyrpg/mv/outsider}"
SOLOUD="${2:-$HOME/easyrpg/mv/soloud}"
BUILD="${3:-$SRC/build-ps5}"
HB="$PS5_PAYLOAD_SDK/target/user/homebrew"
HERE="$(dirname "$(realpath "$0")")"

# The SDK's clang wrapper does not search the homebrew prefix that the libraries install into.
export CPATH="$HB/include"
export LIBRARY_PATH="$HB/lib"

source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"

# Apply our patches to the Outsider checkout (skipping the ones that are already applied). Set SKIP_PATCHES=1
# when you are working directly in the checkout (it then already contains the changes).
for p in "$HERE/../patches/outsider"/*.patch; do
    [ "${SKIP_PATCHES:-0}" = "1" ] && break
    [ -e "$p" ] || continue
    if git -c safe.directory='*' -C "$SRC" apply --check -R "$p" 2>/dev/null; then
        echo "patch already applied: $(basename "$p")"
    else
        git -c safe.directory='*' -C "$SRC" apply "$p"
        echo "applied patch: $(basename "$p")"
    fi
done

# pkg-config output as CMake lists
SDL_CFLAGS="$($PKG_CONFIG --cflags sdl2 | xargs | tr ' ' ';')"
SDL_LIBS="$($PKG_CONFIG --libs --static sdl2 | xargs | tr ' ' ';')"

rm -rf "$BUILD"
${CMAKE} -DCMAKE_BUILD_TYPE=Release \
         -DRMMZ_PS5=ON \
         -DRMMZ_PS5_PREFIX="$HB" \
         -DRMMZ_PS5_SDL_CFLAGS="$SDL_CFLAGS" \
         -DRMMZ_PS5_SDL_LIBS="$SDL_LIBS" \
         -DRMMZ_PS5_SOLOUD_DIR="$SOLOUD" \
         -DRMMZ_PS5_SHIM_DIR="$(realpath "$HERE/../shim")" \
         -B "$BUILD" \
         -S "$SRC"
${MAKE} -C "$BUILD" -j"$(nproc)"

ls -la "$BUILD/outsider"
