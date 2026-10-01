# Legal notes

*This file describes what the project is and is not. It is not legal advice; laws differ from country to country. If you
plan to publish or distribute this project, ask a lawyer who knows copyright and software law in your country.*

## What this project is

A launcher and engines that let people **play RPG Maker games they own, are licensed to play, or that are free**, on a
PlayStation 5 that they have already jailbroken. The engines are open source programs (see `THIRD-PARTY-NOTICES.md`). The
project exists for compatibility (RPG Maker games on hardware that has no official player), preservation, learning and
homebrew development.

## What it does not contain

* No games, no RTP, no SoundFonts.
* No exploit code and nothing that jailbreaks a console; no Sony files, keys or firmware.
* No tool for finding, downloading or unlocking games.
* No key recovery. RPG Maker MV and MZ can encrypt a game's images and sounds; the game stores the key it needs in its own
  `System.json`, and the runtime uses that key to read the files, as the official player does. RPG Maker XP/VX/VX Ace archives
  are read the way mkxp-z and the official player read them. Nothing is cracked, patched or bypassed, and no licence check exists
  that could be skipped.

## Piracy

The project is not made for, does not help with, and does not approve of copying or playing games without the right to do
so. A program that plays games cannot know whether someone has the right to play a particular one, and calling a tool
"educational" does not change what the person using it does with it. People who use it are responsible for what they run,
under the laws that apply to them. The maintainers do not host, link to or ask for games; requests for that will be ignored.
If you are a rights holder and think something in this repository infringes your rights, open an issue in this repository
(or use GitHub's own contact options on the owner's profile); it will be looked at promptly.

## The console

* Running your own code on a PlayStation 5 needs the console to be jailbroken. Whether jailbreaking your own console is legal
  depends on where you live (for example the rules on circumventing technical protection measures, such as section 1201
  of the US DMCA or article 6 of the EU copyright directive 2001/29/EC as each country has implemented it). This project does
  not provide the means.
* Sony's terms of service forbid modifying the console. Going online with a modified console can get the console or the
  account banned. The project does not change that risk. Keep a jailbroken console offline, or reset it before going online.
* The helper servers the project relies on (`websrv`, `ftpsrv`) have no password. Use them only on a network you trust.

## Names

"RPG Maker" is a trademark of Kadokawa Corporation; "PlayStation" and "PS5" are trademarks of Sony Interactive
Entertainment. They are used here only to say which games and which console the software works with. The project is not
made, approved or supported by either company. The project is called "Tsukuru Player" (and the home screen tile says so) so that nobody takes it for an official product.

## Licensing of the project

* The project's own code: GPL-3.0-or-later (`LICENSE`). Third-party components keep their licenses
  (`THIRD-PARTY-NOTICES.md`).
* If you hand the programs to someone (for example the USB bundle), you must also give them, or offer them, the source code,
  and keep the license texts with the programs. The notices file lists what to include.
* Outsider, the MV/MZ runtime, is dual-licensed by its authors; this project uses its GPL option and adds its own changes under
  the GPL only (see the notices file).

## No warranty

The software is provided as is, without warranty of any kind (GPL sections 15 and 16). Use it at your own risk.
