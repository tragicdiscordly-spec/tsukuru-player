#!/usr/bin/env python3
"""Small helper for talking to a jailbroken PS5 that is running elfldr, ftpsrv and websrv.

  ps5.py send   HOST FILE.elf                 send a payload to elfldr (port 9021)
  ps5.py upload HOST LOCAL_DIR REMOTE_DIR     copy a directory over FTP (ftpsrv, port 2121)
  ps5.py launch HOST REMOTE_ELF [--args "A B"] [--cwd DIR] [--env "K=V K2=V2"] [--pipe]
                                              start a homebrew as a foreground app through websrv
                                              (port 8080); --pipe prints its console output until
                                              it exits

Everything here talks to unauthenticated services, so only use it on a network you trust.
On Git Bash set MSYS_NO_PATHCONV=1, otherwise it rewrites remote paths that start with a slash.
"""
import ftplib
import os
import socket
import sys
import urllib.parse
import urllib.request

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(errors='replace')  # file names may not fit the console's code page


def send(host, path, port=9021):
    with open(path, 'rb') as f, socket.create_connection((host, port), timeout=10) as s:
        s.sendall(f.read())
    print(f'sent {os.path.getsize(path)} bytes to {host}:{port}')


def upload(host, local_dir, remote_dir, port=2121):
    ftp = ftplib.FTP(encoding='utf-8')  # games often have non-ASCII file names
    ftp.connect(host, port, timeout=15)
    ftp.login()  # anonymous

    def enter(path):
        # ftpsrv answers 550 to MKD for folders that already exist and rejects STOR with absolute
        # paths, so walk the path with CWD and only create what is missing.
        cur = ''
        for part in [p for p in path.split('/') if p]:
            cur += '/' + part
            try:
                ftp.cwd(cur)
            except ftplib.error_perm:
                ftp.mkd(cur)
                ftp.cwd(cur)

    for root, _dirs, files in os.walk(local_dir):
        rel = os.path.relpath(root, local_dir).replace('\\', '/')
        target = remote_dir if rel == '.' else f'{remote_dir}/{rel}'
        enter(target)
        for name in files:
            with open(os.path.join(root, name), 'rb') as f:
                ftp.storbinary(f'STOR {name}', f)
            print(f'uploaded {target}/{name}')
    ftp.quit()


def launch(host, remote_elf, args='', cwd='', env='', pipe=False, port=8080):
    """Start a homebrew as a foreground app. With pipe=True, print its output until it exits."""
    query = {'path': remote_elf}
    if args:
        query['args'] = args
    if cwd:
        query['cwd'] = cwd
    if env:
        query['env'] = env
    if pipe:
        query['pipe'] = '1'
    url = f'http://{host}:{port}/hbldr?' + urllib.parse.urlencode(query)
    with urllib.request.urlopen(url, timeout=600) as r:
        print(f'{url} -> HTTP {r.status}')
        if pipe:
            while True:
                chunk = r.read1(4096) if hasattr(r, 'read1') else r.read(4096)
                if not chunk:
                    break
                sys.stdout.write(chunk.decode('utf-8', 'replace'))
                sys.stdout.flush()


def _launch_cli(host, remote_elf, *rest):
    opts = {'args': '', 'cwd': '', 'env': '', 'pipe': False}
    it = iter(rest)
    for a in it:
        if a == '--pipe':
            opts['pipe'] = True
        elif a in ('--args', '--cwd', '--env'):
            opts[a[2:]] = next(it)
        else:
            sys.exit(f'unknown option {a}')
    launch(host, remote_elf, **opts)


if __name__ == '__main__':
    cmds = {'send': (send, 2), 'upload': (upload, 3), 'launch': (_launch_cli, 2)}
    if len(sys.argv) < 2 or sys.argv[1] not in cmds or len(sys.argv) - 2 < cmds[sys.argv[1]][1]:
        sys.exit(__doc__)
    cmds[sys.argv[1]][0](*sys.argv[2:])
