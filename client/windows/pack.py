#!/usr/bin/env python3
"""pack.py <bhoptools.exe> <dist/windows>: appends the module DLLs, maretf.exe, mcassets.py (the Progress Log's
Minecraft book, fetched on the player's PC), CS:TV's game files (cstv/materials, cstv/resource) and the licenses to bhoptools.exe, after the program image: main.cpp UnpackFiles puts them next to the
exe on start. Files missing from dist/windows are left out. CS:TV's video helper (the rest of dist/windows/cstv) is
the separate zip (tools/make-release.sh).

Layout: per file  u32 path length, path (utf-8, '/'), u64 size, bytes;  then  u64 offset of the first file, "BHTPACK1".
"""
import os
import struct
import sys

exe, dist = sys.argv[1], sys.argv[2]
files = [n for n in ("curtimefix.dll", "cstv_client.dll", "triggerlook.dll", "p2paint.dll", "maretf.exe", "mcassets.py")
         if os.path.isfile(os.path.join(dist, n))]
for sub in ("cstv/materials", "cstv/resource", "LICENSES"):
    for root, _, names in os.walk(os.path.join(dist, sub)):
        files += [os.path.relpath(os.path.join(root, n), dist).replace(os.sep, "/") for n in sorted(names)]

data = open(exe, "rb").read()
if data[-8:] == b"BHTPACK1":
    data = data[:struct.unpack("<Q", data[-16:-8])[0]]   # packed before: start from the bare exe
with open(exe + ".tmp", "wb") as out:
    out.write(data)
    for rel in files:
        blob = open(os.path.join(dist, rel), "rb").read()
        name = rel.encode()
        out.write(struct.pack("<I", len(name)) + name + struct.pack("<Q", len(blob)) + blob)
    out.write(struct.pack("<Q", len(data)) + b"BHTPACK1")
os.replace(exe + ".tmp", exe)
print("packed %d files into %s" % (len(files), exe))
