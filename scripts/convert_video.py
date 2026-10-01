#!/usr/bin/env python3
"""Convert the .mp4 movies of an RPG Maker MV/MZ game to MPEG-1 (.mpg), next to the originals.

  convert_video.py [--all] GAME_FOLDER [GAME_FOLDER ...]

The PS5 player plays WebM movies (VP8/VP9 with Opus or Vorbis sound, what RPG Maker's own export produces) by itself.
It cannot decode MP4 (H.264): for those it looks for a .mpg (MPEG-1) with the same name next to the movie the game asks
for (movies/Opening.mp4 -> movies/Opening.mpg). This tool makes those files with ffmpeg, which must be on your PATH
(winget install Gyan.FFmpeg). The originals stay. Run it on the copy of the game on your PC, then copy the game to the
console. With --all it converts the WebM movies as well (smaller picture, lighter on the console). Movies are scaled
down to at most 960 pixels wide.
"""
import os
import shutil
import subprocess
import sys

MOVIE_EXTENSIONS = ('.mp4', '.m4v', '.mov')           # what the console cannot decode itself
ALL_EXTENSIONS = MOVIE_EXTENSIONS + ('.webm', '.ogv')
MAX_WIDTH = 960


def convert_game(folder, extensions=MOVIE_EXTENSIONS):
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
            if ext.lower() not in extensions:
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
    args = sys.argv[1:]
    exts = MOVIE_EXTENSIONS
    if '--all' in args:
        exts = ALL_EXTENSIONS
        args = [a for a in args if a != '--all']
    for game in args:
        if os.path.isdir(game):
            convert_game(game, exts)
        else:
            print(f'skipping {game}: not a folder')
