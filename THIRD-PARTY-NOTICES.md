# Third-party software

This project ships programs that contain, or are built from, the software below. Each keeps its own license.
This file is an inventory, kept by hand: **check it against the versions you actually build before a release**. The full
license text of every component is in [`licenses/third-party/`](licenses/third-party/INDEX.txt), and the USB bundle carries the
same folder (`licenses/third-party`).

The project's own code (launcher, installer, tile installer, path shim, build scripts, patches) is licensed under the
**GNU General Public License, version 3 or (at your option) any later version** (`LICENSE`).

## What goes to the console

| Program | Built from | License |
| --- | --- | --- |
| Launcher (`rpgmaker`) | this repository, SDL2, SDL2_ttf | GPL-3.0-or-later (SDL2, SDL2_ttf: zlib) |
| EasyRPG Player (RPG Maker 2000/2003) | [EasyRPG Player](https://github.com/EasyRPG/Player) 0.8.1 + `patches/` | GPL-3.0-or-later |
| mkxp-z (RPG Maker XP/VX/VX Ace) | [mkxp-z](https://gitlab.com/mkxp-z/mkxp-z) 2.4 + `patches/mkxp-z/` | GPL-2.0-or-later |
| Outsider runtime (RPG Maker MV/MZ) | [Outsider](https://github.com/GeneralArcade/outsider) commit f789f46 + `patches/outsider/` | GPL-3.0-or-later (upstream is dual-licensed, see below) |
| Home screen tile installer | based on `install-ps5.c` of [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv) | GPL-3.0-or-later |
| `libOSMesa.so.8` | [Mesa](https://www.mesa3d.org) 22.1.7 (llvmpipe) with LLVM 14 | MIT (Mesa), Apache-2.0 with LLVM exceptions (LLVM) |
| Launcher font (`font.ttf`) | WenQuanYi Micro Hei (taken from mkxp-z) | Apache-2.0 or GPL-3.0 with font exception |

## Libraries linked into those programs

| Library | License | Used by |
| --- | --- | --- |
| SDL2 (the [PS5 port](https://github.com/ps5-payload-dev/SDL), patched here) | zlib | all |
| QuickJS-NG v0.17 | MIT | Outsider |
| SoLoud (core and WAV/OGG/MP3/FLAC parts only; includes stb_vorbis and dr_libs) | zlib (stb_vorbis and dr_libs: public domain/MIT-0) | Outsider |
| stb (image loading, TrueType), pl_mpeg | MIT / public domain | Outsider |
| libwebp 1.4.0 (decoding WebP pictures) | BSD-3-Clause | Outsider (licence text: licenses/third-party/libwebp) |
| FFmpeg 7.0.1 (libavformat, libavcodec, libavutil, libswscale, libswresample; LGPL-only build with just the VP8, VP9, Opus and Vorbis decoders and the Matroska/Ogg readers, to play WebM movies; recipe: packages/ffmpeg-lite) | LGPL-2.1-or-later | Outsider (licence texts: licenses/third-party/FFmpeg). It is linked statically; the complete source of this project is available, so it can be rebuilt with another FFmpeg |
| liblcf | MIT | EasyRPG Player |
| ICU | Unicode License | EasyRPG Player |
| Ruby 3.1 | Ruby License / BSD-2-Clause | mkxp-z |
| TinySoundFont (as a FluidSynth-compatible MIDI synthesizer) | MIT | mkxp-z |
| inih | BSD-3-Clause | EasyRPG Player |
| PhysicsFS, SDL_sound | zlib | mkxp-z |
| pixman | MIT | mkxp-z |
| uchardet | MPL-1.1 | EasyRPG Player |
| fmt, expat | MIT | EasyRPG Player |
| FreeType | FreeType License or GPL-2.0 | launcher, engines |
| HarfBuzz | MIT | engines |
| libpng, zlib, bzip2 | libpng, zlib, bzip2 licenses | all |
| libsamplerate | BSD-2-Clause | all |
| libogg, libvorbis, Opus, FLAC | BSD-3-Clause | engines |
| mpg123, libsndfile, libiconv | LGPL-2.1-or-later | engines |

The LGPL libraries are combined with GPL programs, which the LGPL allows; their source is available from the same
places as the rest (see "Source code" below).

## Toolchain (not shipped)

The programs are built with the [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) (GPL-3.0-or-later; it
contains no Sony code) and the recipes of [pacbrew-repo](https://github.com/ps5-payload-dev/pacbrew-repo).
The helper servers `websrv` and `ftpsrv` (GPL-3.0-or-later, by the same authors) are started on the console by the user
and are **not** part of the bundle.

## Outsider's dual license

Outsider is offered by General Arcade (Pte. Ltd.) under GPL-3.0-or-later **or** a commercial license. This project uses
it under the GPL, and **our changes are licensed under the GPL only**: we do not grant a commercial license for them.
Upstream asks contributors to assign copyright to General Arcade (`CONTRIBUTING.md` in its repository); nothing in this
repository does that. Anyone who wants to send changes upstream has to agree to their terms themselves.

## What is not included, on purpose

* **Games**, of any kind.
* **The RTP** (RPG Maker's shared graphics and sounds): Kadokawa's property. Users install it themselves
  (`README.md`, "RTP").
* **RPG Maker's scripts** (`rmmz_*.js`, `rpg_*.js`): every MV/MZ game carries its own copy; the runtime loads the copy of the
  game. A few short function texts are compared by fingerprint (`perf_post.js`) to detect unmodified core code.
* **Sony files, keys or exploits.** The console must already be jailbroken; nothing here jailbreaks it.
* **SoundFonts.** Users choose their own (for example GeneralUser GS).

## Source code

Anyone who receives the programs is entitled to their source (GPL). For this project that is: this repository (build
recipes, patches, launcher, installer) plus the upstream sources at the versions named above, which are fetched by the
build scripts. If you distribute the binaries (for instance the USB bundle), distribute or link to that source with them,
and keep the license texts and this file with them.
