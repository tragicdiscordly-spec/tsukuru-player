# Tsukuru Player

RPG Maker games on a jailbroken PlayStation 5.

Play **RPG Maker 2000, 2003, XP, VX, VX Ace, MV and MZ** games on a **jailbroken PlayStation 5**:

* **RPG Maker 2000 / 2003** games run on [EasyRPG Player](https://easyrpg.org).
* **RPG Maker XP / VX / VX Ace** games run on [mkxp-z](https://gitlab.com/mkxp-z/mkxp-z) (real Ruby,
  software OpenGL).
* **RPG Maker MV / MZ** games run on the Outsider runtime (QuickJS, SoLoud and a software OpenGL renderer; the game's own
  scripts and plugins run unchanged).
* A small **launcher** app lists your games (from the console or a USB stick), picks the right engine
  and starts it. One home screen tile: **Tsukuru Player**.

Both engines are built with the open source [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk)
and run as homebrew. Everything here is built from source.

> **Status:** working, tested on firmware **13.60** with a test suite, a large translated 2000/2003
> game, a VX Ace game with an encrypted archive and heavy scripts (runs at full speed), and an MV and an MZ game.
> MV/MZ games depend on their plugins and run slower than the older engines: heavy scenes (many light sources, lots of
> plugin scripting) can drop below 30 frames per second. Touchpad = mouse, and the launcher's **Settings** screen (Options
> button) maps pad buttons to game buttons and to keyboard keys.

This repository contains **only** build recipes, patches, a launcher and helper scripts. It contains
no exploit code, no keys, no Sony files, no games, and none of RPG Maker's RTP.

## Using it

You need a console you have already jailbroken (an ELF loader must listen on port 9021), the
programs built from this repository, and the games you own.

### Installing without a PC

`scripts/make-usb-bundle.sh` builds a folder, `tsukuru-player`, that installs everything from a USB stick: copy it to the
stick, open its `install.html` from a phone on the same network (`http://<console address>:8080/fs/mnt/usb0/tsukuru-player/install.html`)
and tap Install. `installer/README-usb.txt` (copied into the folder) has the steps.

### Every time you want to play

1. Jailbreak the console (your exploit of choice).
2. Start the two helper servers on the console: the web launcher (websrv) and an FTP server (ftpsrv).
   * From a PC: run **`start-ps5.bat`** (or `python scripts/start_ps5.py`).
   * Or without a PC: put `websrv-ps5.elf` and `ftpsrv-ps5.elf` in `/data/etaHEN/payloads/` and turn on
     auto start for them in etaHEN's payload menu.
3. Select the **Tsukuru Player** tile on the home screen (a page opens for a moment, then the launcher). Move
   with the D-pad or left stick, press **Cross** to play, **Triangle** to rescan, **Square** to delete a game
   stored on the console (press it twice; games on a USB stick are never deleted), **L1/R1** to page.

> The FTP server and web launcher have no password and give full access to the console's files while
> they run. Only use this on a network you trust. They stop when the console restarts.

### Adding games

* **From a PC:** drag a game folder onto **`add-game.bat`** (or `python scripts/add_game.py "C:\path\Game"`).
  It copies the game to `/data/games` over FTP, fast, and tells you if the game needs an RTP.
* **From a USB stick** (exFAT): put your game folders in a folder called `games` on the stick and plug it
  in before starting the launcher. Games wrapped in an extra folder are found too.

The launcher works out the RPG Maker version from the game's files. Saves are written into the game's
own folder.

### Games that do not work

Not every game runs. If a game closes right after it starts, the launcher says why the next time you open it
(the last error the game printed). Typical reasons:

* **XP/VX/Ace games that call Windows DLLs** (`Win32API`, for Steam, window tricks, key state, ...). A stand-in
  class answers every call with 0, which is enough for many games (for example Steam achievements), but a game that
  really needs the DLL (Pokemon Uranium, for example) will not work.
* **MV/MZ games that use their own file protection** (hashed file names, a modified player) cannot be read; the
  project does not try to get around that.
* **MV/MZ plugins that need Node.js features beyond the common ones.** The runtime provides what plugins usually use
  (`Buffer`, files, paths, `os`, `events`, `util`, `zlib` with the game's pako, the game's own CommonJS files, `nw`);
  running other programs, networking and Steam are not available, and plugins that need them lose that feature.

### RTP (RPG Maker's shared default graphics and sounds)

Many games do not include everything and need the **RTP** of the RPG Maker they were made with. The
launcher marks such a game with a red **`!`** and says which RTP it needs.

| Made with | RTP name | Folder on the console |
| --- | --- | --- |
| RPG Maker 2000 | `2000` | `/data/rtp/2000` |
| RPG Maker 2003 | `2003` | `/data/rtp/2003` |
| RPG Maker XP | `Standard` | `/data/rtp/Standard` |
| RPG Maker VX | `RPGVX` | `/data/rtp/RPGVX` |
| RPG Maker VX Ace | `RPGVXAce` | `/data/rtp/RPGVXAce` |

Download and install the RTP you need from
[RPG Maker's website](https://www.rpgmakerweb.com/run-time-package) on a Windows PC (free, from
Kadokawa; you do **not** need the RPG Maker editor), then run **`python scripts/add_rtp.py`**: it finds
the installed RTPs and copies them to the console. (`add_rtp.py NAME FOLDER` copies a folder by hand;
a `rtp` folder on a USB stick works too.) The RTP is Kadokawa's property and is not part of this project.

### Music

* **MIDI** in XP/VX/VX Ace games needs a **SoundFont** (`.sf2`). Any General MIDI SoundFont works; a good
  free one is [GeneralUser GS](https://github.com/mrbumpy409/GeneralUser-GS) (about 30 MB). Copy it with
  `python scripts/add_soundfont.py GeneralUser-GS.sf2` (it goes to `/data/soundfonts`; a `soundfonts` folder
  on a USB stick works too). RPG Maker 2000/2003 games do not need one, EasyRPG has its own MIDI synthesizer.
* mkxp-z cannot play **.wma** files (some VX Ace games use them). Convert them with
  `python scripts/convert_audio.py "C:\path\Game"` (needs ffmpeg): it writes an `.ogg` next to each `.wma`.
* OGG, MP3 and WAV work everywhere.

## What is in here

| Path | What |
| --- | --- |
| `launcher/` | The launcher app (SDL2, C) |
| `patches/` | Patches for EasyRPG Player and mkxp-z (PS5 platform support, RTP, fullscreen, ...) |
| `packages/` | Build recipes (`PKGBUILD`, [pacbrew](https://github.com/ps5-payload-dev/pacbrew-repo) style) for libraries that pacbrew does not have, a patched SDL2, and a small FluidSynth-compatible MIDI synthesizer built on [TinySoundFont](https://github.com/schellingb/TinySoundFont) |
| `shim/` | `ps5path.c`: makes relative file paths work (firmware 13.60 rejects them for launched apps) |
| `tile/` | Installer for the home screen tile |
| `installer/` | The USB installer (copies the programs to the console, installs the tile) and its install page |
| `LEGAL.md`, `THIRD-PARTY-NOTICES.md` | What the project is and is not, and the licenses of everything in it |
| `scripts/` | Build scripts (Linux/WSL) and the PC helpers above |
| `tests/` | Small programs used to bring the port up (SDL, OpenGL, Ruby, paths, ...) |

## Building

You need Linux (Ubuntu 24.04+ or WSL2), the [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk)
release in `/opt/ps5-payload-sdk`, and [pacbrew-repo](https://github.com/ps5-payload-dev/pacbrew-repo)
cloned to `~/pacbrew-repo`. In order:

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

# 3. launcher and tile
make -C launcher && make -C tile
```

On the console the programs go to `/data/homebrew/easyrpg`, `/data/homebrew/mkxp-z` and
`/data/homebrew/rpgmaker` (as `eboot.elf`), and `libOSMesa.so.8` (software OpenGL, built by the `mesa`
package) goes to `/user/homebrew/lib`. The launcher also needs a font at
`/data/homebrew/rpgmaker/font.ttf` (mkxp-z's `wqymicrohei.ttf` works, it covers Japanese and Chinese).
`scripts/ps5.py` uploads and starts things over FTP and the web launcher.

Some downloads are blocked on certain networks (`ftp.gnu.org`, for example); the recipes check
checksums, so you can fetch the same file from a mirror and put it next to the recipe.

## Credits and licenses

* [Outsider](https://github.com/GeneralArcade/outsider) (MV/MZ runtime; used under its GPLv3+ option), QuickJS-NG (MIT),
  SoLoud (zlib): see `THIRD-PARTY-NOTICES.md` for the complete list of components and their licenses
* [EasyRPG Player](https://github.com/EasyRPG/Player) and liblcf: GPLv3 / MIT
* [mkxp-z](https://gitlab.com/mkxp-z/mkxp-z) and mkxp: GPLv2 or later
* [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk), the PS5 SDL port, websrv, ftpsrv, pacbrew:
  John Törnblom and contributors (GPLv3+ / zlib)
* TinySoundFont: MIT; the MIDI SoundFont you choose has its own license (GeneralUser GS: free to use in software)
* Ruby: Ruby / BSD-2-Clause; Mesa and LLVM: MIT / Apache 2.0 with LLVM exception; ICU: Unicode, Inc.;
  SDL2, physfs, SDL_sound: zlib; inih: BSD-3-Clause; pixman: MIT
* The launcher's font (mkxp-z's `wqymicrohei.ttf`): WenQuanYi Micro Hei

This project is licensed under the **GNU General Public License, version 3 or (at your option) any
later version**; see `LICENSE`. RPG Maker and RTP are trademarks/property of Kadokawa Corporation; PlayStation and PS5 are
trademarks of Sony Interactive Entertainment. This project is not affiliated with either company.

## Legal

The project contains no games, no RTP, no exploit code and no Sony files, and it is not meant for playing games you have no
right to play. See `LEGAL.md` (intended use, piracy, the console, names) and `THIRD-PARTY-NOTICES.md`. If you hand the
programs to someone, hand over or link to the source with them.
