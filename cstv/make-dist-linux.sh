#!/usr/bin/env bash
# make-dist-linux.sh: build CS:TV and put it in the fork's dist/linux/cstv next to bhoptools.so. Needs the Source SDK
# headers in third_party/source-sdk-2013 and libmpv. Launch options then go through cstv/cstv-launch, which starts
# the helper outside Steam's runtime and adds client/libcstv.so to LD_PRELOAD.
set -euo pipefail
C=$(cd "$(dirname "$0")" && pwd)
R=$C/../dist/linux/cstv
make -C "$C/client" -B
make -C "$C/client" check-glibc
make -C "$C/helper" -B
rm -rf "$R"
mkdir -p "$R/client" "$R/helper" "$R/assets"
cp "$C/cstv-launch" "$R/"
cp "$C/client/libcstv.so" "$R/client/"
cp "$C"/helper/{cstvd,cstv_link.py,cstv_overlay.py,cstv_ytdl.py} "$R/helper/"
cp -r "$C/helper/fonts" "$R/helper/"
cp -r "$C/assets/materials" "$C/assets/resource" "$R/assets/"
strip "$R/client/libcstv.so" "$R/helper/cstvd"
du -sh "$R"
