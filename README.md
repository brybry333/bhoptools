# bhoptools

rawinput2, real client-authoritative triggers, 0 ping checkpoints, 0 ping hud elements like bhop-get-stats, viewpunch
removal, 8k polling rate support, fps_max over 1k, and more. Plus CS:TV (video screens in game), paint and the
trigger menu. Based on [ddyfad/bhoptools](https://github.com/ddyfad/bhoptools) 1.14.1.

## Install

**Windows**
1. Download `bhoptools.exe` from [Releases](../../releases) and run it.
2. CS:TV videos (optional): put `bhoptools-cstv-video.zip` in the same folder as `bhoptools.exe`. Don't unzip it.

**Linux**
1. Download `bhoptools-linux.tar.gz` from [Releases](../../releases), unpack it and run `./install.sh`.
2. Paste the line it prints into CS:S → Properties → Launch Options.

Keys: CS:S Options → Keyboard, the bhoptools section at the top (only F8 = prediction has a default).
On/off switches: Options → bhoptools.

## What's different from daf's 1.14.1
- no auto-updater: it never contacts GitHub; new versions only from this repo's Releases
- `bhoptools_hello` once per map instead of every 30 s (servers without the bhoptools plugin don't fill your console)
- built in, each with a switch in Options → bhoptools:
  - [curtimefix](https://github.com/brybry333/curtimefix): smooth movement on old maps
  - [CS:TV](https://github.com/brybry333/cstv): video screens (source in `cstv/`)
  - [triggerlook](https://github.com/brybry333/triggerlook): the triggers a server shows, in your own look (`triggerlook_menu`)
  - [p2paint](https://github.com/brybry333/p2paint): paint on servers with its plugin (`p2paint_menu`)
- keys in the game's own Options → Keyboard instead of fixed F-keys

## Build
- Linux: `make` in `client/linux` → `bhoptools.so`; CS:TV: `cstv/make-dist-linux.sh`.
- Windows: `client/windows/build.sh` (clang-cl + xwin) → `bhoptools.exe`; CS:TV: `cstv/win/build.sh` + `cstv/win/make-dist-windows.sh`.
- The other modules come from their own repos (links above). Put everything into `dist/linux` / `dist/windows`, then
  `tools/make-release.sh` makes the three release files. Details: client/README.md.

## Found a bug?
Turn on `bhoptools_log`, reproduce it, turn it off, and open an issue here with `cstrike/bhoptools_log.txt`.
