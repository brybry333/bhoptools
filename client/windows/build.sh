#!/usr/bin/env bash
# Cross-builds bhoptools.exe on Linux: clang-cl + lld-link against xwin's MSVC CRT/SDK (pacman: clang llvm lld;
# `xwin splat` into ~/.local/share/xwin or $XWIN). The GitHub workflow builds the same sources with MSBuild.
# Output: build/bhoptools.exe. Needs `git submodule update --init` (Detours).
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
X=${XWIN:-$HOME/.local/share/xwin}
B=build
mkdir -p "$B/obj"

# -MD: the image copied into the game keeps the launcher's imports, so the VC runtime DLLs must be the same ones
# at the same address there. -Zc:threadSafeInit-: no TLS-based static guards (the copy has no TLS in the game).
CF=(--target=x86_64-pc-windows-msvc -nologo -O2 -MD -EHsc -Zc:threadSafeInit- -Wno-everything
    -imsvc "$X/crt/include" -imsvc "$X/sdk/include/ucrt" -imsvc "$X/sdk/include/um" -imsvc "$X/sdk/include/shared"
    -DNDEBUG -D_CONSOLE -D_CRT_SECURE_NO_WARNINGS -D_USE_MATH_DEFINES -I. -I../shared)

objs=() pids=()
for f in Detours/src/{detours,disasm,modules}.cpp main.cpp pollfix.cpp rawprecise.cpp \
         ../shared/{teleport_predict,speedo,jumpstats,checkpoint_predict,engine_trace,momsurffix_predict}.cpp \
         ../shared/{settings,options_tab,spectate,download_progress,fastdl}.cpp; do
    o=$B/obj/$(basename "$f" .cpp).obj
    clang-cl "${CF[@]}" -c "$f" -Fo"$o" & pids+=($!)
    objs+=("$o")
done
for p in "${pids[@]}"; do wait "$p"; done

llvm-rc -nologo -fo "$B/bhoptools.res" bhoptools.rc
lld-link -nologo -out:"$B/bhoptools.exe" -subsystem:console -opt:ref -opt:icf \
    -libpath:"$X/crt/lib/x86_64" -libpath:"$X/sdk/lib/um/x86_64" -libpath:"$X/sdk/lib/ucrt/x86_64" \
    "${objs[@]}" "$B/bhoptools.res" \
    msvcrt.lib vcruntime.lib ucrt.lib msvcprt.lib kernel32.lib user32.lib advapi32.lib shell32.lib winhttp.lib
# the module DLLs, maretf.exe, CS:TV's game files and the licenses in ../../dist/windows ride along at the end of the
# exe (pack.py; main.cpp UnpackFiles puts them next to it on start): one file to hand out
[ -d ../../dist/windows ] && python3 pack.py "$B/bhoptools.exe" ../../dist/windows
ls -la "$B/bhoptools.exe"
