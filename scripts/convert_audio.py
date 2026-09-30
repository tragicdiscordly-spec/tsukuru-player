#!/usr/bin/env python3
"""Convert audio that mkxp-z (RPG Maker XP/VX/VX Ace) cannot play to OGG, next to the originals.

  convert_audio.py GAME_FOLDER [GAME_FOLDER ...]

RPG Maker games refer to music and sounds without a file extension (Audio/BGM/Battle1), and mkxp-z
picks whichever of .ogg .mp3 .wav .mid ... exists. Windows RPG Maker also plays .wma, which mkxp-z
does not, so a game with .wma music is silent. This tool converts every .wma below the game's
Audio folder to a .ogg with the same name (the originals stay) using ffmpeg, which must be on your
PATH. Run it on the copy of the game on your PC, then copy the game to the console.
"""
import os
import shutil
import subprocess
import sys

UNSUPPORTED = ('.wma',)


def convert_game(folder):
    ffmpeg = shutil.which('ffmpeg')
    if not ffmpeg:
        sys.exit('ffmpeg was not found on your PATH. Install it (winget install Gyan.FFmpeg) and try again.')
    done = skipped = failed = 0
    for root, _dirs, files in os.walk(folder):
        for name in files:
            base, ext = os.path.splitext(name)
            if ext.lower() not in UNSUPPORTED:
                continue
            src = os.path.join(root, name)
            dst = os.path.join(root, base + '.ogg')
            if os.path.exists(dst):
                skipped += 1
                continue
            cmd = [ffmpeg, '-y', '-loglevel', 'error', '-i', src, '-vn', '-c:a', 'libvorbis', '-q:a', '5', dst]
            if subprocess.call(cmd) == 0 and os.path.exists(dst):
                done += 1
                print(f'  converted {os.path.relpath(src, folder)}')
            else:
                failed += 1
                print(f'  FAILED    {os.path.relpath(src, folder)}')
    print(f'{folder}: {done} converted, {skipped} already had an .ogg, {failed} failed')


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for folder in sys.argv[1:]:
        if not os.path.isdir(folder):
            print(f'skipping {folder}: not a folder')
            continue
        convert_game(folder)


if __name__ == '__main__':
    main()
