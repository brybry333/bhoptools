#!/usr/bin/env bash
# make-release.sh: release/ = the three files to upload, made from dist/ (build everything into dist first):
#   bhoptools.exe               Windows: the launcher with the modules, maretf.exe, CS:TV's game files and the licenses
#                               packed in (client/windows/pack.py)
#   bhoptools-cstv-video.zip    Windows, optional: CS:TV's video helper; goes next to bhoptools.exe, which unpacks it
#                               (stored, not compressed: bhoptools.exe only copies bytes)
#   bhoptools-linux.tar.gz      Linux: bhoptools/ with the libraries, CS:TV and install.sh
set -euo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
D=$R/dist
OUT=$R/release
rm -rf "$OUT"
mkdir -p "$OUT/linux/bhoptools"

cp "$D/windows/bhoptools.exe" "$OUT/bhoptools.exe"
python3 "$R/client/windows/pack.py" "$OUT/bhoptools.exe" "$D/windows"
(cd "$D/windows" && zip -q -r -0 "$OUT/bhoptools-cstv-video.zip" cstv -x 'cstv/materials/*' 'cstv/resource/*')

L=$OUT/linux/bhoptools
cp "$D/linux/"{bhoptools.so,libcurtimefix.so,triggerlook.so,p2paint.so} "$L/"
cp -r "$D/linux/cstv" "$L/"
cp "$R/client/linux/install.sh" "$L/"
mkdir -p "$L/LICENSES" && cp "$R/LICENSE" "$L/LICENSES/bhoptools-GPL-3.0.txt" && cp -r "$D/windows/LICENSES/cstv" "$L/LICENSES/"
tar -C "$OUT/linux" -czf "$OUT/bhoptools-linux.tar.gz" bhoptools
rm -rf "$OUT/linux"
ls -la "$OUT"
