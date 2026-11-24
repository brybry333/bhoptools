#!/usr/bin/env bash
# make-dist-windows.sh: put CS:TV into the fork's dist/windows next to bhoptools.exe: cstv_client.dll and the cstv
# folder (helper, Python, yt-dlp, streamlink, materials, fonts). Run ./fetch-deps.sh and ./build.sh first.
set -euo pipefail
W=$(cd "$(dirname "$0")" && pwd)
C=$(dirname "$W")
OUT=$C/win/build/out
D=$W/deps
R=$C/../dist/windows
rm -rf "$R/cstv" "$R/LICENSES/cstv"
mkdir -p "$R/cstv/fonts" "$R/LICENSES/cstv"

cp "$OUT/cstv_client.dll" "$R/"
cp "$C/mcassets.py" "$R/"   # packed into bhoptools.exe (pack.py), run by it with the Python below
cp "$OUT/cstvd.exe" "$OUT/cstv_ytdl.exe" "$C"/helper/{cstv_overlay.py,cstv_ytdl.py,cstv_link.py} "$R/cstv/"
cp "$C/win/build/mpv-dev/libmpv-2.dll" "$D/yt-dlp.exe" "$R/cstv/"
unzip -q -o "$D/deno.zip" -d "$R/cstv/"

# embeddable Python + Pillow (a wheel is a zip); the ._pth file lists where imports come from
unzip -q "$D/python-embed.zip" -d "$R/cstv/python"
mkdir -p "$R/cstv/python/Lib/site-packages"
unzip -q "$D"/pillow-*-win_amd64.whl -d "$R/cstv/python/Lib/site-packages"
pth=$(ls "$R/cstv/python"/python3*._pth)
printf 'Lib\\site-packages\r\n' >> "$pth"

# portable streamlink without its 145 MB ffmpeg.exe (only needed to mux separate audio/video streams;
# Twitch and YouTube live are single HLS streams)
unzip -q "$D"/streamlink-*-x86_64.zip -d "$R/cstv/"
mv "$R/cstv"/streamlink-*-x86_64 "$R/cstv/streamlink"
rm -rf "$R/cstv/streamlink/ffmpeg"

# bhoptools.exe copies these into cstrike\custom\cstv before the game starts
cp -r "$C/assets/materials" "$C/assets/resource" "$R/cstv/"
cp "$C"/helper/fonts/RobotoMono-*.ttf "$R/cstv/fonts/"
cp /usr/share/fonts/noto/NotoSans-Regular.ttf /usr/share/fonts/noto/NotoSans-Bold.ttf \
   /usr/share/fonts/noto/NotoColorEmoji.ttf "$R/cstv/fonts/"

cp /usr/share/licenses/noto-fonts/LICENSE "$R/LICENSES/cstv/Noto-fonts-OFL.txt"
cp "$C/helper/fonts/RobotoMono-LICENSE.txt" "$R/LICENSES/cstv/"
cat > "$R/LICENSES/cstv/SOURCES.txt" <<SRC
Where the parts come from (and their source code):
  cstv_client.dll, cstv\cstvd.exe, cstv\*.py, cstv_ytdl.exe
                            GPL-3.0    github.com/brybry333/cstv (here: bhoptools' cstv folder)
  cstv\libmpv-2.dll         mpv (GPL build) github.com/mpv-player/mpv, built by
                            github.com/shinchiro/mpv-winbuild-cmake (release $(basename "$D"/mpv-dev-*.7z .7z))
  cstv\yt-dlp.exe           Unlicense  github.com/yt-dlp/yt-dlp
  cstv\deno.exe             MIT        github.com/denoland/deno
  cstv\streamlink\          BSD-2      github.com/streamlink/windows-builds (its own licenses inside)
  cstv\python\              PSF        python.org (LICENSE.txt inside); Pillow (MIT-CMU, in site-packages)
  cstv\fonts\               SIL OFL / Apache-2.0   Noto and Roboto Mono
SRC
du -sh "$R/cstv"
