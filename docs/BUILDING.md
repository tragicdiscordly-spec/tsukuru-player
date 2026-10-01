# Building Tsukuru Player and how it fits together

This page is for people who want to build the programs themselves or work on the code. If you only want to play games, read
the [README](../README.md).

## What is in this repository

| Path | What |
| --- | --- |
| `launcher/` | The launcher app (SDL2, C): lists games, picks the engine, settings screen |
| `installer/` | The USB installer (copies the programs to the console, installs the tile) and its install page |
| `tile/` | Installer for the home screen tile |
| `patches/` | Patches for EasyRPG Player, mkxp-z and the Outsider runtime (PS5 platform support, RTP, input, rendering, Node.js support, ...) |
| `extras/` | Files that go next to the engines on the console (`mkxp-z/rgss_compat.rb`: Windows/Ruby 1.8 compatibility for XP/VX/Ace games) |
| `packages/` | Build recipes (`PKGBUILD`, [pacbrew](https://github.com/ps5-payload-dev/pacbrew-repo) style) for libraries pacbrew does not have, a patched SDL2, and a small FluidSynth-compatible MIDI synthesizer built on [TinySoundFont](https://github.com/schellingb/TinySoundFont) |
| `shim/` | `ps5path.c`: makes relative file paths work (firmware 13.60 rejects them for launched apps) and fixes the console's time functions |
| `scripts/` | Build scripts (Linux/WSL) and the PC helpers (`add_game.py`, `ps5.py`, ...) |
| `tests/` | Small programs used to bring the port up (SDL, OpenGL, Ruby, paths, ...) |
| `LEGAL.md`, `THIRD-PARTY-NOTICES.md` | What the project is and is not, and the licenses of everything in it |

## How the pieces fit together

- The **launcher** (`/data/homebrew/rpgmaker`) scans `/data/games` and every USB stick's `games` folder, works out the RPG Maker
  generation from each game's files, and starts the matching engine through websrv's launcher API
  (`http://127.0.0.1:8080/hbldr`).
- **EasyRPG Player** runs 2000/2003 games. **mkxp-z** runs XP/VX/VX Ace games (real Ruby 3.1; `extras/mkxp-z/rgss_compat.rb`
  and the mkxp-z patch make games written for the Windows player behave). **Outsider** runs MV/MZ games: QuickJS-NG runs the
  game's own JavaScript, with JS shims for the browser (DOM, canvas, PIXI, WebAudio) and for Node.js (`node_shim.js`: `Buffer`,
  `fs`, `path`, `zlib`, hashing, `process`, `nw`, ...).
- Everything is drawn with software OpenGL (Mesa llvmpipe), because homebrew has no GPU access.
- Games write their output to `/data/homebrew/last-run.log`, and the launcher shows the last error line when it starts again.

## Building

You need Linux (Ubuntu 24.04+ or WSL2), the [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) release in
`/opt/ps5-payload-sdk`, and [pacbrew-repo](https://github.com/ps5-payload-dev/pacbrew-repo) cloned to `~/pacbrew-repo`.
In order:

```sh
# 1. libraries (pacbrew recipes and ours, see packages/)
scripts/build-libs.sh zlib fmt libpng expat libiconv bzip2 freetype harfbuzz libsamplerate \
    libogg libvorbis opus mpg123 flac libsndfile SDL2 SDL2_mixer SDL2_image SDL2_ttf libtheora \
    openal libjpeg-turbo libwebp openlibm llvm mesa
scripts/build-libs.sh pixman inih icu liblcf physfs sdl_sound uchardet fluidsynth-tsf

# 2. engines
scripts/build-player.sh ~/easyrpg/Player       # EasyRPG Player (clone EasyRPG/Player and EasyRPG/liblcf first)
scripts/build-ruby.sh                          # MRI Ruby 3.1 (needs a native Ruby 3.1 of the same version)
scripts/build-mkxp.sh ~/easyrpg/mkxp-z         # mkxp-z (clone mkxp-z from GitLab first)
scripts/build-outsider.sh                      # MV/MZ runtime (clone Outsider and SoLoud first)

# 3. launcher, installer and tile
make -C launcher && make -C installer && make -C tile

# 4. the USB bundle
scripts/make-usb-bundle.sh dist
```

On the console the programs go to `/data/homebrew/easyrpg`, `/data/homebrew/mkxp-z`, `/data/homebrew/outsider` and
`/data/homebrew/rpgmaker` (as `eboot.elf`), and `libOSMesa.so.8` (software OpenGL, built by the `mesa` package) goes to
`/user/homebrew/lib`. The launcher also needs a font at `/data/homebrew/rpgmaker/font.ttf` (mkxp-z's `wqymicrohei.ttf` works; it
covers Japanese and Chinese). `scripts/ps5.py` uploads and starts things over FTP and the web launcher.

`scripts/make-usb-bundle.sh` collects the built programs into the `tsukuru-player` folder that users copy to a USB stick
(the bundle is what a release should contain, together with the license texts).

Some downloads are blocked on certain networks (`ftp.gnu.org`, for example); the recipes check checksums, so you can fetch the
same file from a mirror and put it next to the recipe.

## Notes for working on the code

- On Git Bash for Windows, set `MSYS_NO_PATHCONV=1`, or arguments that start with a slash are rewritten into Windows paths.
- Launched apps on firmware 13.60 cannot use relative paths; `shim/ps5path.c` wraps the libc calls (see `shim/wrap-flags.txt`).
- Do not overwrite a program that is running: copy to a new name and rename (the installer does this).
- Handy debugging switches: `MKXP_DEBUG_SHOT` and `MKXP_DEBUG_KEYS` (see `extras/mkxp-z/rgss_compat.rb`), `RMMZ_AUTOSTART`,
  `RMMZ_AUTOSCRIPT` and `RMMZ_DUMP_PRESENT` for Outsider.
