#!/usr/bin/env bash
# Builds the folder "rpgmaker-ps5" that installs RPG Maker on a PS5 from a USB stick, without a PC.
#
# Usage: make-usb-bundle.sh [output-dir]      (default: dist)
#
# Needs the built programs. Where they are can be set with environment variables (defaults in brackets):
#   HB           folder with launcher/engine programs as built for the console, one folder per program:
#                rpgmaker/, outsider/, easyrpg/, mkxp-z/ with eboot.elf in each    [../build/homebrew]
#   LAUNCHER     the launcher program                                              [launcher/launcher.elf]
#   OUTSIDER_SRC the Outsider checkout (its src/shims folder is copied)            [~/easyrpg/mv/outsider]
#   OSMESA       libOSMesa.so.8 as it goes to the console (stripped)               [$HB/lib/libOSMesa.so.8]
#   FONT         the launcher's font                                               [$HB/rpgmaker/font.ttf]
#
# The result: <output-dir>/rpgmaker-ps5/ with install.elf, install.html, README.txt and files/. Copy that folder to
# the root of a USB stick (exFAT), plug it into the console, start websrv and open install.html (see README.txt).
set -eu

HERE="$(dirname "$(realpath "$0")")"
REPO="$HERE/.."
OUT="${1:-$REPO/dist}/rpgmaker-ps5"
HB="${HB:-$REPO/../build/homebrew}"
LAUNCHER="${LAUNCHER:-$REPO/launcher/launcher.elf}"
OUTSIDER_SRC="${OUTSIDER_SRC:-$HOME/easyrpg/mv/outsider}"
OSMESA="${OSMESA:-$HB/lib/libOSMesa.so.8}"
FONT="${FONT:-$HB/rpgmaker/font.ttf}"

need() { [ -e "$1" ] || { echo "missing: $1 ($2)" >&2; exit 1; }; }
need "$REPO/installer/install.elf" "build it with: make -C installer"
need "$LAUNCHER" "build it with: make -C launcher"
need "$HB/outsider/eboot.elf" "scripts/build-outsider.sh"
need "$HB/easyrpg/eboot.elf" "scripts/build-player.sh"
need "$HB/mkxp-z/eboot.elf" "scripts/build-mkxp.sh"
need "$OUTSIDER_SRC/src/shims" "the Outsider checkout"
need "$OSMESA" "libOSMesa.so.8 from the mesa package"
need "$FONT" "a font with Japanese and Chinese glyphs, e.g. wqymicrohei.ttf"

rm -rf "$OUT"
mkdir -p "$OUT/files/homebrew/rpgmaker/sce_sys" "$OUT/files/homebrew/outsider/shims" \
         "$OUT/files/homebrew/easyrpg/sce_sys" "$OUT/files/homebrew/mkxp-z/sce_sys" "$OUT/files/lib"

cp "$REPO/installer/install.elf" "$OUT/install.elf"
cp "$REPO/installer/install.html" "$OUT/install.html"
cp "$REPO/installer/README-usb.txt" "$OUT/README.txt"
mkdir -p "$OUT/licenses"
cp "$REPO/LICENSE" "$REPO/LEGAL.md" "$REPO/THIRD-PARTY-NOTICES.md" "$OUT/licenses/"

cp "$LAUNCHER" "$OUT/files/homebrew/rpgmaker/eboot.elf"
cp "$FONT" "$OUT/files/homebrew/rpgmaker/font.ttf"
cp "$REPO/tile/assets/icon0.png" "$OUT/files/homebrew/rpgmaker/sce_sys/icon0.png"

cp "$HB/outsider/eboot.elf" "$OUT/files/homebrew/outsider/eboot.elf"
# every shim except the test driver
for f in "$OUTSIDER_SRC"/src/shims/*.js; do
    [ "$(basename "$f")" = "control_shim.js" ] && continue
    cp "$f" "$OUT/files/homebrew/outsider/shims/"
done

cp "$HB/easyrpg/eboot.elf" "$OUT/files/homebrew/easyrpg/eboot.elf"
cp "$HB/mkxp-z/eboot.elf" "$OUT/files/homebrew/mkxp-z/eboot.elf"
for e in easyrpg mkxp-z; do
    if [ -d "$HB/$e/sce_sys" ]; then cp "$HB/$e/sce_sys"/* "$OUT/files/homebrew/$e/sce_sys/"; fi
done

cp "$OSMESA" "$OUT/files/lib/libOSMesa.so.8"

echo "bundle ready: $OUT"
du -sh "$OUT"
find "$OUT" -type f | sed "s#^$OUT/##" | sort | head -40
