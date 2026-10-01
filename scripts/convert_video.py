#!/usr/bin/env python3
"""Convert the movies of an RPG Maker MV/MZ game to MPEG-1 (.mpg), next to the originals.

  convert_video.py GAME_FOLDER [GAME_FOLDER ...]

RPG Maker MV and MZ games ship their movies as .webm (or .mp4). The PS5 player has no WebM decoder; it
plays MPEG-1 (.mpg) movies, and looks for a .mpg with the same name next to the movie the game asks for
(movies/Opening.webm -> movies/Opening.mpg). This tool makes those files with ffmpeg, which must be on
your PATH (winget install Gyan.FFmpeg). The originals stay. Run it on the copy of the game on your PC,
then copy the game to the console. Movies are scaled down to at most 960 pixels wide to keep playback smooth.
"""
import os
import shutil
import subprocess
import sys

MOVIE_EXTENSIONS = ('.webm', '.mp4', '.ogv', '.m4v', '.mov')
MAX_WIDTH = 960


def convert_game(folder):
    ffmpeg = shutil.which('ffmpeg')
    if not ffmpeg:
        sys.exit('ffmpeg was not found on your PATH. Install it (winget install Gyan.FFmpeg) and try again.')
    done = skipped = failed = 0
    for root, _dirs, files in os.walk(folder):
        # only movie folders: "movies" or "movie" below the game (the engines look nowhere else)
        if not any(part.lower() in ('movies', 'movie') for part in os.path.relpath(root, folder).split(os.sep)):
            continue
        for name in files:
            base, ext = os.path.splitext(name)
            if ext.lower() not in MOVIE_EXTENSIONS:
                continue
            src = os.path.join(root, name)
            dst = os.path.join(root, base + '.mpg')
            if os.path.exists(dst):
                skipped += 1
                continue
            tmp = dst + '.part'
            cmd = [ffmpeg, '-y', '-loglevel', 'error', '-i', src,
                   '-vf', f"scale='min({MAX_WIDTH},iw)':-2", '-c:v', 'mpeg1video', '-q:v', '4', '-g', '25',
                   '-c:a', 'mp2', '-b:a', '192k', '-ar', '44100', '-ac', '2', '-f', 'mpeg', tmp]
            if subprocess.call(cmd) == 0 and os.path.exists(tmp):
                os.replace(tmp, dst)
                done += 1
                print(f'  converted {os.path.relpath(src, folder)}', flush=True)
            else:
                if os.path.exists(tmp):
                    os.remove(tmp)
                failed += 1
                print(f'  FAILED {os.path.relpath(src, folder)}', flush=True)
    print(f'{os.path.basename(os.path.abspath(folder))}: {done} converted, {skipped} already done, {failed} failed')


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for game in sys.argv[1:]:
        if os.path.isdir(game):
            convert_game(game)
        else:
            print(f'skipping {game}: not a folder')
