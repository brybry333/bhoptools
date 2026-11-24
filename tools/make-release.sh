#!/usr/bin/env bash
# make-release.sh: release/ = the three files to upload, made from dist/ (build everything into dist first):
#   bhoptools.exe               Windows: the launcher with the modules, maretf.exe, CS:TV's game files and the licenses
#                               packed in (client/windows/pack.py)
#   bhoptools-cstv-video.zip    Windows, optional: CS:TV's video helper; goes next to bhoptools.exe, which unpacks it
#                               (stored, not compressed: bhoptools.exe only copies bytes)
#   bhoptools-linux.tar.gz      Linux: bhoptools/ with the libraries, CS:TV and install.sh
# Usage: make-release.sh v<APP_VERSION>. The tag must be the version both builds carry: the Options tab compares its
# own version with the latest release's tag, so a mismatch would show "Update available" on the new build forever.
set -euo pipefail
TAG=${1:?usage: make-release.sh v<version in client/shared/version.h>}
R=$(cd "$(dirname "$0")/.." && pwd)
D=$R/dist
OUT=$R/release
grep -qF "\"${TAG#v}\"" "$R/client/shared/version.h" || { echo "version.h isn't ${TAG#v}"; exit 1; }
for f in "$D/windows/bhoptools.exe" "$D/linux/bhoptools.so"; do
    grep -qaF "${TAG#v}" "$f" || { echo "$f isn't built from ${TAG#v}"; exit 1; }
done
# pack.py leaves out what's missing; a release must have them all
for f in curtimefix.dll cstv_client.dll triggerlook.dll p2paint.dll maretf.exe mcassets.py; do
    [ -f "$D/windows/$f" ] || { echo "dist/windows/$f is missing"; exit 1; }
done
rm -rf "$OUT"
mkdir -p "$OUT/linux/bhoptools"

cp "$D/windows/bhoptools.exe" "$OUT/bhoptools.exe"
python3 "$R/client/windows/pack.py" "$OUT/bhoptools.exe" "$D/windows"
(cd "$D/windows" && zip -q -r -0 "$OUT/bhoptools-cstv-video.zip" cstv -x 'cstv/materials/*' 'cstv/resource/*' '*.bak*')

L=$OUT/linux/bhoptools
cp "$D/linux/"{bhoptools.so,libcurtimefix.so,triggerlook.so,p2paint.so} "$L/"
strip --strip-debug "$L"/*.so   # debug info holds the build folder's path
cp -r "$D/linux/cstv" "$L/"
find "$L" -name '*.bak*' -delete   # local backups in dist/ stay local
cp "$R/client/linux/install.sh" "$L/"
mkdir -p "$L/LICENSES" && cp "$R/LICENSE" "$L/LICENSES/bhoptools-GPL-3.0.txt" && cp -r "$D/windows/LICENSES/cstv" "$L/LICENSES/"
tar -C "$OUT/linux" -czf "$OUT/bhoptools-linux.tar.gz" bhoptools
rm -rf "$OUT/linux"
ls -la "$OUT"
