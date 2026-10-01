Tsukuru Player for the PlayStation 5 - install from this USB stick
=============================================================

You need a PS5 that is already jailbroken (the ELF loader on port 9021 has to be running), and a phone or
computer on the same Wi-Fi/network as the console. No PC is needed.

1. Copy this whole folder ("tsukuru-player") to the top level of a USB stick (exFAT or FAT32) and plug the
   stick into the console. (Any computer, or a phone with a card reader/OTG adapter, can do this copy.)

2. Jailbreak the console and start "websrv" (web launcher, port 8080) from your payload menu.

3. Find the console's address: on the PS5 open Settings > Network > View Connection Status and read the
   "IP Address" (for example 192.168.1.222).

4. On your phone (or any computer on the same network) open this address in a browser, with YOUR console's
   address instead of 192.168.1.1 (copy and paste it, then change the numbers):

       http://192.168.1.1:8080/fs/mnt/usb0/tsukuru-player/install.html

   If the page is empty or "not found", the stick is not usb0: try usb1, usb2, ... in place of usb0.
   (On the console's own browser, if it lets you type an address, use 127.0.0.1 instead of the IP address.)

5. Tap "Install". Notifications in the corner of the TV show the progress; it takes a minute or two. When it says
   "Tsukuru Player installed", open the "Tsukuru Player" tile on the home screen (websrv has to be running; a web page shows
   for a moment and then the launcher starts). The same page updates an existing install. The launcher shows the
   console's address in the bottom right corner, so you can find it again.

   After the first install, "tsukuru-installer" is also in the payload menu of etaHEN (if you use it). To update later,
   plug in the stick with the new "tsukuru-player" folder and start it from there: no phone needed.

Games
-----
Put your games in a folder called "games" on the USB stick (one folder per game) and plug the stick in before
starting the launcher. RPG Maker 2000, 2003, XP, VX, VX Ace, MV and MZ games are supported.
You can also start "ftpsrv" on the console and connect an FTP app on your phone (host = the console's address,
port 2121, no user name or password) and copy game folders to /data/games.

Some games need RPG Maker's shared graphics and sounds (the "RTP"): the launcher marks those games with a red "!"
and says which one. Put the RTP folders into a folder called "rtp" on the stick (for example rtp/RPGVXAce).
The RTP is free from https://www.rpgmakerweb.com/run-time-package (run its Windows installer once and copy the installed
folder); it belongs to Kadokawa and is not included here.
MV and MZ games do not need it. For MIDI music in XP/VX/VX Ace games put a SoundFont (.sf2) into a folder called
"soundfonts" on the stick.

What is installed
-----------------
/data/homebrew/rpgmaker   the launcher
/data/homebrew/outsider   the runtime for RPG Maker MV and MZ games
/data/homebrew/easyrpg    EasyRPG Player (RPG Maker 2000 and 2003)
/data/homebrew/mkxp-z     mkxp-z (RPG Maker XP, VX and VX Ace)
/user/homebrew/lib        the software OpenGL library
and the "Tsukuru Player" tile on the home screen.

The installer only writes to those places. It needs about 140 MB on the console.

Licenses and source code
------------------------
These programs are free software built from source: EasyRPG Player (GPLv3), mkxp-z (GPLv2+), the MV/MZ runtime
(GPLv3+), the launcher and this installer (GPLv3+), Mesa, LLVM, SDL, Ruby and others under their own licenses.
The source code, build recipes and license texts are in the project repository that this stick came from.
The folder "licenses" next to this file has the license of the project (LICENSE), the list of everything it contains
(THIRD-PARTY-NOTICES.md) and notes on what the project is and is not (LEGAL.md). The programs are meant for playing games
you own or are licensed to play; none are included here.

The web launcher and the FTP server have no password while they run. Only use them on a network you trust.
