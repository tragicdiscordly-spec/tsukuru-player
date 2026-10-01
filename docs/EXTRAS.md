# Extras: RTP, music and other things some games need

You only need this page if a game complains or has no music.

## RTP (RPG Maker's shared default graphics and sounds)

Many games do not include everything and need the **RTP** of the RPG Maker they were made with. The launcher marks such a game
with a red **`!`** and says which RTP it needs. MV and MZ games do not need it.

| Made with | RTP name | Folder |
| --- | --- | --- |
| RPG Maker 2000 | `2000` | `rtp/2000` |
| RPG Maker 2003 | `2003` | `rtp/2003` |
| RPG Maker XP | `Standard` | `rtp/Standard` |
| RPG Maker VX | `RPGVX` | `rtp/RPGVX` |
| RPG Maker VX Ace | `RPGVXAce` | `rtp/RPGVXAce` |

The simplest way: make a folder called **`rtp`** on your USB stick and put the RTP folder inside it (for example
`rtp/RPGVXAce`). On the console itself the same folders go to `/data/rtp`.

Download the RTP from [RPG Maker's website](https://www.rpgmakerweb.com/run-time-package) on a Windows PC (it is free, from
Kadokawa; you do **not** need the RPG Maker editor), run its installer once, and copy the installed folder. From a PC you can
also run `python scripts/add_rtp.py`: it finds the installed RTPs and copies them to the console
(`add_rtp.py NAME FOLDER` copies one folder by hand). The RTP is Kadokawa's property and is not part of this project.

## Music

- **MIDI** in XP/VX/VX Ace games needs a **SoundFont** (`.sf2`). Any General MIDI SoundFont works; a good free one is
  [GeneralUser GS](https://github.com/mrbumpy409/GeneralUser-GS) (about 30 MB). Put it in a folder called `soundfonts` on the
  USB stick, or copy it to the console with `python scripts/add_soundfont.py GeneralUser-GS.sf2` (it goes to `/data/soundfonts`).
  RPG Maker 2000/2003 games do not need one: EasyRPG has its own MIDI synthesizer.
- mkxp-z cannot play **.wma** files (some VX Ace games use them). Convert them with
  `python scripts/convert_audio.py "C:\path\Game"` (needs ffmpeg): it writes an `.ogg` next to each `.wma`.
- OGG, MP3 and WAV work everywhere.

## Movies (MV and MZ games)

Games made with RPG Maker MV or MZ keep their cutscenes as **.webm** or **.mp4** files, which the console cannot decode.
The player plays **.mpg** (MPEG-1) instead: it looks for a `.mpg` with the same name next to the movie the game asks for
(`movies/Opening.webm` becomes `movies/Opening.mpg`). Make them on your PC (needs [ffmpeg](https://ffmpeg.org/); on Windows:
`winget install Gyan.FFmpeg`):

- `add-game.bat` converts the movies by itself before it copies a game, when ffmpeg is installed (add `--no-convert` to
  `python scripts/add_game.py` to skip that), or
- drag the game folder onto `convert-videos.bat` (`python scripts/convert_video.py "C:\path\Game"`), then copy the game.

The originals stay. Movies are scaled to at most 960 pixels wide so they play smoothly. Without the `.mpg` files a game still
runs: its movies are skipped.

## Button mapping

Open **Options** in the launcher: you can map each pad button to a game button or a keyboard key, change the picture scaling
and the touchpad pointer speed. The mapping is used by MV and MZ games; the older engines use their own default keys.
