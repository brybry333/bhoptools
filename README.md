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
2. CS:S → Properties → Launch Options:
   ```
   LD_PRELOAD="$HOME/.local/share/bhoptools/bhoptools.so:$HOME/.local/share/bhoptools/cstv/client/libcstv.so:$LD_PRELOAD" %command% -insecure
   ```

Keys: CS:S Options → Keyboard, the bhoptools section at the top (only F8 = prediction has a default).
On/off switches: Options → bhoptools.

## What's different from daf's 1.14.1
- no auto-updater: at game start (at most every 6 hours) it reads the latest release's version number from GitHub and
  Options > bhoptools says "Update available" with a link to the Releases page; nothing is downloaded or installed
- `bhoptools_hello` once per map instead of every 30 s (servers without the bhoptools plugin don't fill your console)
- built in, each with a switch in Options → bhoptools:
  - [curtimefix](https://github.com/brybry333/curtimefix): smooth movement on old maps
  - [CS:TV](https://github.com/brybry333/cstv): video screens (source in `cstv/`)
  - the Progress Log (`cstv_book`, `!prog`, `!progsave`): a practice diary per map, drawn as a Minecraft book. Works on
    every server, no plugin: the log is saved on your PC and runs are followed in the game, from the map's timer zones in
    the public [zone list](https://github.com/srcwr/zones-cstrike) shavit servers use. The book's Minecraft pictures,
    font and sounds come from Minecraft itself on your PC (a Minecraft install, else Mojang's download server, once);
    none of Mojang's files ship with bhoptools.
  - [triggerlook](https://github.com/brybry333/triggerlook): the triggers a server shows, in your own look (`triggerlook_menu`)
  - [p2paint](https://github.com/brybry333/p2paint): paint (`p2paint_menu`, hold `+paint`) on every server, no plugin:
    your paint is saved on your PC, and other players on the same server who run bhoptools see it (and can copy it)
    through cstv.xyz
- keys in the game's own Options → Keyboard instead of fixed F-keys

## Build
- Linux: `make` in `client/linux` → `bhoptools.so`; CS:TV: `cstv/make-dist-linux.sh`.
- Windows: `client/windows/build.sh` (clang-cl + xwin) → `bhoptools.exe`; CS:TV: `cstv/win/build.sh` + `cstv/win/make-dist-windows.sh`.
- The other modules come from their own repos (links above). Put everything into `dist/linux` / `dist/windows`, then
  `tools/make-release.sh` makes the three release files. Details: client/README.md.

## Found a bug?
Turn on `bhoptools_log`, reproduce it, turn it off, and open an issue here with `cstrike/bhoptools_log.txt`.
