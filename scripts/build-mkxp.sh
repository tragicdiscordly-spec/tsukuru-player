#!/usr/bin/env bash
# Configures and builds mkxp-z (RPG Maker XP/VX/VX Ace) for the PS5. Run inside Ubuntu (WSL) as the
# build user. Needs the libraries from build-libs.sh (SDL2, SDL2_image, SDL2_ttf, SDL_sound, OpenAL,
# physfs, pixman, uchardet, theora, vorbis, ...) and Ruby from build-ruby.sh installed in the SDK.
#
# Usage: build-mkxp.sh [mkxp-z-checkout] [build-dir]
set -eu

export PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}"
SRC="${1:-$HOME/easyrpg/mkxp-z}"
# The build directory must be inside the source directory: mkxp-z embeds its assets with `xxd -i`,
# which names the arrays after the relative path of the input file, and the code expects "../assets/...".
BUILD="${2:-$SRC/build}"
HB="$PS5_PAYLOAD_SDK/target/user/homebrew"

# The SDK's clang wrapper does not search the homebrew prefix that the libraries install into.
export CPATH="$HB/include"
export LIBRARY_PATH="$HB/lib"

source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"

# mkxp-z asks pkg-config for "gl". On the PS5 OpenGL is Mesa's OSMesa, which SDL loads at run time
# with dlopen (from /user/homebrew/lib on the console), so nothing must be linked; a headers-only
# gl.pc satisfies the dependency. (Needs write access to the SDK; a no-op if it exists.)
if [ ! -f "$HB/lib/pkgconfig/gl.pc" ]; then
    cat > "$HB/lib/pkgconfig/gl.pc" <<'EOT'
prefix=/user/homebrew
includedir=${prefix}/include

Name: gl
Description: OpenGL headers only; the implementation (OSMesa) is loaded at run time by SDL
Version: 4.5
Cflags: -I${includedir}
Libs:
EOT
fi

# Some .pc files (OpenAL, ...) ask for -lstdc++, but the PS5 toolchain uses libc++, which the C++
# driver already links. An empty stand-in library satisfies the request.
if [ ! -f "$HB/lib/libstdc++.a" ]; then
    "$PS5_PAYLOAD_SDK/bin/prospero-ar" rc "$HB/lib/libstdc++.a"
fi

# Apply our patches to the mkxp-z checkout (skipping the ones that are already applied).
PATCHES="$(dirname "$(realpath "$0")")/../patches/mkxp-z"
for p in "$PATCHES"/*.patch; do
    [ -e "$p" ] || continue
    if git -c safe.directory='*' -C "$SRC" apply --check -R "$p" 2>/dev/null; then
        echo "patch already applied: $(basename "$p")"
    else
        git -c safe.directory='*' -C "$SRC" apply "$p"
        echo "applied patch: $(basename "$p")"
    fi
done

# PS5 programs started through the launcher cannot use relative paths (see shim/ps5path.c), which
# mkxp-z, physfs and Ruby use everywhere: link the shim in and wrap the libc functions it covers.
SHIM_DIR="$(dirname "$(realpath "$0")")/../shim"
SHIM_O="${TMPDIR:-/tmp}/ps5path.o"
"$CC" -c -O2 "$SHIM_DIR/ps5path.c" -o "$SHIM_O"
WRAP="$(cat "$SHIM_DIR/wrap-flags.txt")"

rm -rf "$BUILD"

# -Wl,--export-dynamic: libOSMesa.so is loaded with dlopen and imports libc functions from this
# executable; the PS5 runtime linker only resolves symbols that the executable exports.
# (The SDK's meson wrapper does not preserve quoting, so the link arguments go in through an extra
# cross file instead of -D options.)
EXTRA_INI="${TMPDIR:-/tmp}/ps5-mkxp-extra.ini"
cat > "$EXTRA_INI" <<EOT
[built-in options]
c_link_args = ['-Wl,--export-dynamic', '$WRAP', '$SHIM_O']
cpp_link_args = ['-Wl,--export-dynamic', '$WRAP', '$SHIM_O']
EOT

"$MESON" setup "$BUILD" "$SRC" \
    --cross-file="$EXTRA_INI" \
    --buildtype=release \
    -Dgfx_backend=gl \
    -Duse_miniffi=false \
    -Denable-https=false \
    -Dshared_fluid=true \
    -Dmri_version=3.1

"$MESON" compile -C "$BUILD" -v
