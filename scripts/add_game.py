#!/usr/bin/env python3
"""Copy RPG Maker games to the PS5's games folder over FTP.

  add_game.py [-j N] GAME_FOLDER [GAME_FOLDER ...]

Each folder is copied to /data/games/<folder name>/ on the console (ftpsrv must be running, see
start_ps5.py). Files that already exist with the same size are skipped, so an interrupted copy can
simply be started again. Host: $PS5_HOST or 192.168.1.16. -j sets the number of parallel FTP
connections (default 4); games are thousands of small files and every file costs several network
round trips, so several connections make a big difference.

You can also drag game folders onto add-game.bat.
"""
import ftplib
import os
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor

HOST = os.environ.get('PS5_HOST', '192.168.1.16')
PORT = 2121
REMOTE_ROOT = '/data/games'
IGNORED = {'thumbs.db', '.ds_store', 'desktop.ini'}


def connect():
    ftp = ftplib.FTP(encoding='utf-8')  # games often have Japanese file names
    ftp.connect(HOST, PORT, timeout=60)
    ftp.login()
    return ftp


def enter(ftp, path):
    """ftpsrv answers 550 to MKD for existing folders and rejects absolute paths in STOR, so walk
    the path with CWD and only create what is missing."""
    cur = ''
    for part in [p for p in path.split('/') if p]:
        cur += '/' + part
        try:
            ftp.cwd(cur)
        except ftplib.error_perm:
            ftp.mkd(cur)
            ftp.cwd(cur)


def remote_sizes(ftp):
    """Sizes of the files in the current remote folder, from one LIST instead of a SIZE per file."""
    sizes = {}
    lines = []
    ftp.retrlines('LIST', lines.append)
    for line in lines:
        parts = line.split(None, 8)
        if len(parts) == 9 and parts[0].startswith('-'):
            sizes[parts[8]] = int(parts[4])
    return sizes


def looks_like_game(folder):
    """A game folder has RPG_RT.ldb (2000/2003) or Game.ini / Game.rgss*a (XP, VX, VX Ace) in it, or in
    a folder below it (downloaded games are often wrapped in an extra folder)."""
    markers = ('rpg_rt.ldb', 'rpg_rt.lmt', 'game.ini', 'game.rgssad', 'game.rgss2a', 'game.rgss3a')
    for root, dirs, files in os.walk(folder):
        if any(f.lower() in markers for f in files):
            return True
        if root[len(folder):].count(os.sep) >= 2:
            dirs[:] = []  # only look two levels deep, like the launcher
    return False


class Progress:
    def __init__(self, total_files, total_bytes):
        self.lock = threading.Lock()
        self.total_files, self.total_bytes = total_files, total_bytes
        self.files = self.copied = self.skipped = self.bytes = 0
        self.start = self.last = time.time()

    def add(self, size, copied):
        with self.lock:
            self.files += 1
            if copied:
                self.copied += 1
                self.bytes += size
            else:
                self.skipped += 1
            now = time.time()
            if now - self.last >= 3 or self.files == self.total_files:
                self.last = now
                rate = self.bytes / max(now - self.start, 1e-6) / 1e6
                print(f'  {self.files}/{self.total_files} files ({self.copied} copied, '
                      f'{self.skipped} already there), {rate:.1f} MB/s', flush=True)


def upload_directory(remote_dir, entries, progress):
    """entries: list of (local_path, name). Runs on its own FTP connection."""
    ftp = connect()
    try:
        enter(ftp, remote_dir)
        existing = remote_sizes(ftp)
        for path, name in entries:
            size = os.path.getsize(path)
            if existing.get(name) == size:
                progress.add(size, False)
                continue
            with open(path, 'rb') as f:
                ftp.storbinary(f'STOR {name}', f)
            progress.add(size, True)
    finally:
        try:
            ftp.quit()
        except ftplib.all_errors:
            pass


def add_game(folder, jobs, remote_name=None):
    """Copy `folder` to REMOTE_ROOT/<remote_name or the folder's own name> on the console."""
    folder = os.path.abspath(folder)
    name = remote_name or os.path.basename(folder.rstrip('\\/'))
    if not os.path.isdir(folder):
        print(f'skipping {folder}: not a folder')
        return
    if remote_name is None and not looks_like_game(folder):
        print(f'warning: {name} does not look like an RPG Maker game (no RPG_RT.ldb, Game.ini or Game.rgss*a '
              'next to each other), the launcher may not list it')

    by_dir = {}
    total_bytes = 0
    for root, _dirs, names in os.walk(folder):
        rel = os.path.relpath(root, folder).replace('\\', '/')
        rdir = f'{REMOTE_ROOT}/{name}' + ('' if rel == '.' else '/' + rel)
        by_dir.setdefault(rdir, [])
        for n in names:
            if n.lower() not in IGNORED:
                p = os.path.join(root, n)
                by_dir[rdir].append((p, n))
                total_bytes += os.path.getsize(p)
    total_files = sum(len(v) for v in by_dir.values())
    print(f'{name}: {total_files} files in {len(by_dir)} folders, {total_bytes / 1e6:.1f} MB, {jobs} connections')

    # Create every folder first, on one connection, so the workers never race on MKD.
    ftp = connect()
    for rdir in by_dir:
        enter(ftp, rdir)
    ftp.quit()

    progress = Progress(total_files, total_bytes)
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(upload_directory, rdir, entries, progress)
                   for rdir, entries in by_dir.items() if entries]
        for fut in futures:
            fut.result()
    print(f'{name}: done in {time.time() - progress.start:.0f} s')
    if remote_name is None:
        check_rtp(folder)


def ini_values(folder, keys):
    """Values of the given keys in the game's Game.ini (case-insensitive, spaces around '=' allowed)."""
    found = {}
    for root, dirs, files in os.walk(folder):
        for f in files:
            if f.lower() == 'game.ini':
                with open(os.path.join(root, f), encoding='utf-8', errors='replace') as fh:
                    for line in fh:
                        key, sep, value = line.partition('=')
                        if sep and key.strip().lower() in keys and value.strip():
                            found[key.strip().lower()] = value.strip()
                return found
        if root[len(folder):].count(os.sep) >= 2:
            dirs[:] = []
    return found


def check_rtp(folder):
    """Tell the user which RTPs (RPG Maker's shared assets) the game asks for and whether the console has them."""
    names = sorted(set(ini_values(folder, ('rtp', 'rtp1', 'rtp2', 'rtp3')).values()))
    if not names:
        return
    try:
        ftp = connect()
        ftp.cwd('/data/rtp')
        installed = []
        ftp.retrlines('LIST', lambda line: installed.append(line.split(None, 8)[-1].lower()))
        ftp.quit()
    except (OSError, ftplib.all_errors):
        installed = []
    for rtp in names:
        if rtp.lower() in installed:
            print(f'RTP "{rtp}": already on the console.')
        else:
            print(f'NOTE: this game needs the RTP "{rtp}", which is not on the console yet.')
            print('      Install it on your PC (RPG Maker website, RTP downloads) and run:  python scripts/add_rtp.py')


def main():
    args = sys.argv[1:]
    jobs = 4
    if args[:1] == ['-j'] and len(args) >= 2:
        jobs = max(1, int(args[1]))
        args = args[2:]
    if not args:
        sys.exit(__doc__)
    try:
        connect().quit()
    except OSError as e:
        sys.exit(f'Cannot reach the PS5 FTP server at {HOST}:{PORT} ({e}).\n'
                 'Run start-ps5.bat first (after jailbreaking the console).')
    for folder in args:
        add_game(folder, jobs)


if __name__ == '__main__':
    main()
