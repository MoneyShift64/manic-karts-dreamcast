#!/usr/bin/env python3
"""extract_cd.py - the game's files off your own Manic Karts CD image.

Reads MANICKARTS.cue and the .bin it names (in the project folder): track 1, the data
track (MODE1/2352), is an ISO 9660 file system; K.EXE (the game) and KART.DAT (all its
pictures, tracks and sounds, packed) are copied from it into extracted/. The music is
the CD's audio tracks: tools/mkmusic.c reads those from the .bin itself.

Checks that K.EXE is the one this port was made from (the PC CD-ROM release, v2.00).
Plain Python 3, nothing to install. Usage: python3 tools/extract_cd.py"""
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.dirname(HERE)
OUT = os.path.join(TOP, "extracted")
CUE = os.path.join(TOP, "MANICKARTS.cue")
KEXE_SHA1 = "134bc3e474f2c5b5cb065d7773a442b7cbfdffbb"
WANTED = ("K.EXE", "KART.DAT")


def main():
    if not os.path.exists(CUE):
        sys.exit("Put your Manic Karts CD image (MANICKARTS.cue and MANICKARTS.bin) in the project folder.")
    binname, mode = None, None
    for line in open(CUE, errors="replace"):
        w = line.split()
        if w and w[0].upper() == "FILE" and binname is None:
            binname = line.split('"')[1] if '"' in line else w[1]
        if len(w) >= 3 and w[0].upper() == "TRACK" and w[1] == "01":
            mode = w[2].upper()
    if binname is None or mode != "MODE1/2352":
        sys.exit("MANICKARTS.cue: expected track 1 to be MODE1/2352 data, in one .bin file")
    binpath = os.path.join(TOP, binname)
    if not os.path.exists(binpath):
        sys.exit("Can't find %s (named in MANICKARTS.cue)" % binname)
    f = open(binpath, "rb")

    def sectors(lba, n):
        out = bytearray()
        for i in range(n):
            f.seek((lba + i) * 2352 + 16)    # (16 bytes of sync and header, then 2048 of data)
            out += f.read(2048)
        return bytes(out)

    pvd = sectors(16, 1)
    if pvd[1:6] != b"CD001":
        sys.exit("%s: track 1 isn't an ISO 9660 data track" % binname)
    root = pvd[156:156 + 34]
    lba, size = struct.unpack("<I", root[2:6])[0], struct.unpack("<I", root[10:14])[0]
    data = sectors(lba, (size + 2047) // 2048)
    found = {}
    o = 0
    while o < len(data):
        n = data[o]
        if n == 0:                           # (the rest of this sector is empty)
            o = (o // 2048 + 1) * 2048
            continue
        e = data[o:o + n]
        name = e[33:33 + e[32]].decode("latin-1").split(";")[0].upper()
        if name in WANTED:
            found[name] = (struct.unpack("<I", e[2:6])[0], struct.unpack("<I", e[10:14])[0])
        o += n
    os.makedirs(OUT, exist_ok=True)
    for name in WANTED:
        if name not in found:
            sys.exit("%s isn't on this CD image: is it Manic Karts?" % name)
        flba, fsize = found[name]
        path = os.path.join(OUT, name)
        with open(path, "wb") as w:
            left, at = fsize, flba
            while left > 0:
                chunk = sectors(at, min(512, (left + 2047) // 2048))
                w.write(chunk[:left])
                left -= len(chunk)
                at += 512
        print("  %-9s %9d bytes" % (name, fsize))
    if hashlib.sha1(open(os.path.join(OUT, "K.EXE"), "rb").read()).hexdigest() != KEXE_SHA1:
        print("Warning: this K.EXE isn't the version the port was made from; the build may not work.")


if __name__ == "__main__":
    main()
