#!/usr/bin/env python3
"""Copy a SoundFont (.sf2) to the PS5, so MIDI music plays in RPG Maker XP/VX/VX Ace games.

  add_soundfont.py FILE.sf2

It is copied to /data/soundfonts on the console (ftpsrv must be running, see start_ps5.py). The
launcher passes the first .sf2 it finds there (or in a "soundfonts" folder on a USB stick) to mkxp-z.

Any General MIDI SoundFont works. A good free one is GeneralUser GS (about 30 MB), from its author's
repository https://github.com/mrbumpy409/GeneralUser-GS (file GeneralUser-GS.sf2).
RPG Maker 2000/2003 games do not need a SoundFont: EasyRPG has its own MIDI synthesizer.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import add_game  # noqa: E402  (reuses the FTP helpers)


def main():
    if len(sys.argv) != 2 or not sys.argv[1].lower().endswith('.sf2') or not os.path.isfile(sys.argv[1]):
        sys.exit(__doc__)
    path = sys.argv[1]
    try:
        ftp = add_game.connect()
    except OSError as e:
        sys.exit(f'Cannot reach the PS5 FTP server at {add_game.HOST}:{add_game.PORT} ({e}).\n'
                 'Run start-ps5.bat first (after jailbreaking the console).')
    add_game.enter(ftp, '/data/soundfonts')
    name = os.path.basename(path)
    with open(path, 'rb') as f:
        ftp.storbinary(f'STOR {name}', f)
    ftp.quit()
    print(f'copied {name} to /data/soundfonts ({os.path.getsize(path) / 1e6:.1f} MB)')


if __name__ == '__main__':
    main()
