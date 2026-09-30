#!/usr/bin/env python3
"""One-stop start after the console has been jailbroken (re-run after every reboot of the PS5).

  1. waits until the exploit's ELF loader answers on port 9021,
  2. sends websrv (web launcher, port 8080) and ftpsrv (FTP, port 2121) unless they already run,
  3. opens the launcher page in your browser.

Usage: start_ps5.py [HOST]        (default host: $PS5_HOST or 192.168.1.16)
Payloads (websrv-ps5.elf, ftpsrv-ps5.elf) are looked up in <repo>/build.
"""
import os
import socket
import sys
import time
import webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
# Payloads live in <repo>/build (or in ../build next to the repo when building outside of it).
BUILD_DIRS = [os.path.normpath(os.path.join(HERE, '..', 'build')),
              os.path.normpath(os.path.join(HERE, '..', '..', 'build'))]
HOST = sys.argv[1] if len(sys.argv) > 1 else os.environ.get('PS5_HOST', '192.168.1.16')


def is_open(port, timeout=1.5):
    try:
        with socket.create_connection((HOST, port), timeout=timeout):
            return True
    except OSError:
        return False


def wait_for(port, seconds, label):
    end = time.time() + seconds
    while time.time() < end:
        if is_open(port):
            return True
        time.sleep(2)
    print(f'timed out waiting for {label} (port {port})')
    return False


def send(name, port):
    if is_open(port):
        print(f'{name}: already running on port {port}')
        return
    path = next((p for p in (os.path.join(d, f'{name}-ps5.elf') for d in BUILD_DIRS)
                 if os.path.exists(p)), None)
    if path is None:
        sys.exit(f'{name}-ps5.elf not found in {BUILD_DIRS}; build it first')
    with open(path, 'rb') as f, socket.create_connection((HOST, 9021), timeout=10) as s:
        s.sendall(f.read())
    print(f'{name}: sent, waiting for port {port} ...', end=' ', flush=True)
    print('ok' if wait_for(port, 30, name) else 'FAILED')


def main():
    print(f'PS5 at {HOST}')
    if not is_open(9021):
        print('The ELF loader (port 9021) is not running. Run the jailbreak exploit on the PS5 now;')
        print('I will wait up to 5 minutes ...')
        if not wait_for(9021, 300, 'the ELF loader'):
            sys.exit(1)
    print('ELF loader is up.')
    send('websrv', 8080)
    send('ftpsrv', 2121)
    url = f'http://{HOST}:8080/'
    print()
    print(f'Launcher : {url}   (click "easyrpg")')
    print(f'FTP      : ftp://{HOST}:2121  (anonymous; put games in /data/games/<name>/)')
    webbrowser.open(url)


if __name__ == '__main__':
    main()
