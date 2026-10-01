#!/usr/bin/env python3
"""Copy RPG Maker games to the PS5's games folder over FTP.

  add_game.py [-j N] [--name FOLDER_NAME_ON_THE_CONSOLE] GAME_FOLDER [GAME_FOLDER ...]

Each folder is copied to /data/games/<folder name>/ on the console (ftpsrv must be running, see
start_ps5.py). Files that already exist with the same size are skipped, so an interrupted copy can
simply be started again. Host: $PS5_HOST or 192.168.1.16. -j sets the number of parallel FTP
connections (default 4); games are thousands of small files and every file costs several network
round trips, so several connections make a big difference.

You can also drag game folders onto add-game.bat.

The console plays .webm movies itself but cannot decode .mp4. When a game has .mp4 movies and ffmpeg is on your PATH
they are converted to .mpg first (next to the originals, see convert_video.py); use --no-convert to skip that.
"""
import ftplib
import os
import queue
import shutil
import socket
import sys
import threading
import time

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
    try:
        ftp.cwd(path)  # the usual case: the folder is already there
        return
    except ftplib.error_perm:
        pass
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
    markers = ('rpg_rt.ldb', 'rpg_rt.lmt', 'game.ini', 'game.rgssad', 'game.rgss2a', 'game.rgss3a', 'rpg_core.js', 'rmmz_core.js')
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
        self.failed = []
        self.start = self.last = time.time()

    def add(self, size, copied):
        with self.lock:
            self.files += 1
            if copied:
                self.copied += 1
                self.bytes += size
            else:
                self.skipped += 1
            self.show()

    def fail(self, path):
        with self.lock:
            self.files += 1
            self.failed.append(path)
            self.show(force=True)

    def show(self, force=False):
        now = time.time()
        if force or now - self.last >= 3 or self.files == self.total_files:
            self.last = now
            elapsed = max(now - self.start, 1e-6)
            rate = self.bytes / elapsed / 1e6
            # the time left is estimated from the files that had to be copied (skipped ones cost nothing)
            todo = max(self.total_files - self.files, 0)
            eta = ''
            if self.copied >= 20:
                secs = todo * elapsed / self.copied
                eta = f', about {int(secs // 60)} min {int(secs % 60)} s left'
            print(f'  {self.files}/{self.total_files} files ({self.copied} copied, {self.skipped} already there'
                  f'{f", {len(self.failed)} FAILED" if self.failed else ""}), {rate:.1f} MB/s{eta}', flush=True)


RETRIES = 6


def upload_worker(jobs, progress):
    """One FTP connection that stays open for many folders (a new connection per folder is slow and wears the
    console's FTP server out on games with thousands of folders). A dropped connection is re-opened and the
    folder carried on where it stopped."""
    ftp = None
    while True:
        try:
            remote_dir, entries = jobs.get_nowait()
        except queue.Empty:
            break
        pending = list(entries)
        attempt = 0
        while pending:
            try:
                if ftp is None:
                    ftp = connect()
                enter(ftp, remote_dir)
                existing = remote_sizes(ftp)
                while pending:
                    path, name = pending[0]
                    size = os.path.getsize(path)
                    if existing.get(name) == size:
                        progress.add(size, False)
                    else:
                        with open(path, 'rb') as f:
                            ftp.storbinary(f'STOR {name}', f)
                        progress.add(size, True)
                    pending.pop(0)
            except (OSError, ftplib.all_errors) as e:
                try:
                    if ftp is not None:
                        ftp.close()
                except OSError:
                    pass
                ftp = None
                attempt += 1
                if attempt > RETRIES:
                    for path, _name in pending:
                        progress.fail(path)
                    print(f'  giving up on {remote_dir}: {e}', flush=True)
                    pending = []
                else:
                    time.sleep(min(2 ** attempt, 30))
    if ftp is not None:
        try:
            ftp.quit()
        except (OSError, ftplib.all_errors):
            pass


def prepare_movies(folder):
    """Make the console-playable .mpg copies of a game's movies (needs ffmpeg), or say what to do."""
    has_movies = any(f.lower().endswith(('.mp4', '.m4v', '.mov')) and
                     any(part.lower() in ('movies', 'movie') for part in os.path.relpath(root, folder).split(os.sep))
                     for root, _d, files in os.walk(folder) for f in files)
    if not has_movies:
        return
    if not shutil.which('ffmpeg'):
        print('NOTE: this game has .mp4 movies. The console cannot decode those; with ffmpeg installed (winget install '
              'Gyan.FFmpeg) they are converted automatically. Without it the movies are skipped in the game.')
        return
    print('Converting the movies for the console (the originals stay) ...', flush=True)
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import convert_video
    convert_video.convert_game(folder)


def add_game(folder, jobs, remote_name=None, convert_movies=True, name=None):
    """Copy `folder` to REMOTE_ROOT/<remote_name or the folder's own name> on the console."""
    folder = os.path.abspath(folder)
    name = remote_name or name or os.path.basename(folder.rstrip('\\/'))
    if not os.path.isdir(folder):
        print(f'skipping {folder}: not a folder')
        return
    if remote_name is None and not looks_like_game(folder):
        print(f'warning: {name} does not look like an RPG Maker game (no RPG_RT.ldb, Game.ini, Game.rgss*a or js/rpg_core.js '
              'in it), the launcher may not list it')

    if convert_movies and remote_name is None:
        prepare_movies(folder)

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
    jobs_queue = queue.Queue()
    for rdir, entries in by_dir.items():
        if entries:
            jobs_queue.put((rdir, entries))
    workers = [threading.Thread(target=upload_worker, args=(jobs_queue, progress)) for _ in range(jobs)]
    for w in workers:
        w.start()
    for w in workers:
        w.join()
    if progress.failed:
        print(f'{name}: {len(progress.failed)} files could not be sent. Run the same command again: files that are '
              'already there are skipped, so only the missing ones are sent.')
        for path in progress.failed[:10]:
            print('   ', path)
        sys.exit(1)
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
    convert = '--no-convert' not in args
    args = [a for a in args if a != '--no-convert']
    game_name = None
    if '--name' in args:
        i = args.index('--name')
        game_name = args[i + 1]
        args = args[:i] + args[i + 2:]
    if not args:
        sys.exit(__doc__)
    try:
        connect().quit()
    except OSError as e:
        sys.exit(f'Cannot reach the PS5 FTP server at {HOST}:{PORT} ({e}).\n'
                 'Run start-ps5.bat first (after jailbreaking the console).')
    for folder in args:
        add_game(folder, jobs, convert_movies=convert, name=game_name)


if __name__ == '__main__':
    main()
