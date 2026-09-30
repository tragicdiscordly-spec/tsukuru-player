#!/usr/bin/env python3
"""Copy RPG Maker RTPs (runtime packages: the shared default graphics and sounds) to the PS5.

  add_rtp.py                       find the RTPs installed on this PC and copy them
  add_rtp.py NAME FOLDER           copy FOLDER to the console as NAME

NAME is what the games ask for in their Game.ini (RTP=...) or the EasyRPG name:

  2000       RPG Maker 2000 RTP         (games made with RPG Maker 2000)
  2003       RPG Maker 2003 RTP
  Standard   RPG Maker XP RTP
  RPGVX      RPG Maker VX RTP
  RPGVXAce   RPG Maker VX Ace RTP

They end up in /data/rtp/NAME on the console (ftpsrv must be running, see start_ps5.py). The RTPs
are Kadokawa's; download and install them from RPG Maker's website (the RTP downloads page) on a
Windows PC first. They are free to use with RPG Maker games but are not part of this project and
must not be redistributed with it.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import add_game  # noqa: E402  (reuses the parallel FTP upload)

# Where the RTP installers put things on Windows: (name, [candidate folders])
PF = [os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'), os.environ.get('ProgramFiles', r'C:\Program Files')]
CANDIDATES = {
    'RPGVXAce': [os.path.join(p, r'Common Files\Enterbrain\RGSS3\RPGVXAce') for p in PF],
    'RPGVX': [os.path.join(p, r'Common Files\Enterbrain\RGSS2\RPGVX') for p in PF],
    'Standard': [os.path.join(p, r'Common Files\Enterbrain\RGSS\Standard') for p in PF],
    '2003': [os.path.join(p, r'Enterbrain\RPG2003\RTP') for p in PF] + [os.path.join(p, r'Common Files\Enterbrain\RPG2003\RTP') for p in PF],
    '2000': [os.path.join(p, r'ASCII\RPG2000\RTP') for p in PF] + [os.path.join(p, r'Common Files\Enterbrain\RPG2000\RTP') for p in PF],
}


def registry_paths():
    """RTP folders recorded in the Windows registry by the RTP installers (best effort)."""
    found = {}
    try:
        import winreg
    except ImportError:
        return found
    keys = [
        ('RPGVXAce', r'SOFTWARE\Enterbrain\RGSS3\RTP', 'RPGVXAce'),
        ('RPGVX', r'SOFTWARE\Enterbrain\RGSS2\RTP', 'RPGVX'),
        ('Standard', r'SOFTWARE\Enterbrain\RGSS\RTP', 'Standard'),
        ('2003', r'SOFTWARE\Enterbrain\RPG2003', 'RuntimePackagePath'),
        ('2000', r'SOFTWARE\ASCII\RPG2000', 'RuntimePackagePath'),
    ]
    for name, sub, value in keys:
        for view in (winreg.KEY_WOW64_32KEY, winreg.KEY_WOW64_64KEY):
            try:
                with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, sub, 0, winreg.KEY_READ | view) as k:
                    path, _ = winreg.QueryValueEx(k, value)
                    if path and os.path.isdir(path):
                        found[name] = path
                        break
            except OSError:
                continue
    return found


def main():
    args = sys.argv[1:]
    try:
        add_game.connect().quit()
    except OSError as e:
        sys.exit(f'Cannot reach the PS5 FTP server at {add_game.HOST}:{add_game.PORT} ({e}).\n'
                 'Run start-ps5.bat first (after jailbreaking the console).')

    if len(args) == 2:
        todo = [(args[0], args[1])]
    elif not args:
        todo = []
        reg = registry_paths()
        for name in CANDIDATES:
            path = reg.get(name) or next((p for p in CANDIDATES[name] if os.path.isdir(p)), None)
            if path:
                todo.append((name, path))
        if not todo:
            sys.exit('No installed RTP found on this PC. Install the RTP from RPG Maker\'s website, or run:\n'
                     '  add_rtp.py NAME FOLDER')
    else:
        sys.exit(__doc__)

    for name, path in todo:
        print(f'{name}: {path}')
        # add_game copies into REMOTE_ROOT/<folder name>; point it at /data/rtp and use NAME as the folder name
        add_game.REMOTE_ROOT = '/data/rtp'
        add_game.add_game(path, 4, remote_name=name)


if __name__ == '__main__':
    main()
