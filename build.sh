#!/bin/bash
# Build Manic Karts for the Dreamcast from your own Manic Karts CD image:
# makes manic-karts.cdi. Needs KallistiOS with SH4ZAM (kos-ports) and mkdcdisc, a C
# compiler (cc) and Python 3; MANICKARTS.cue and MANICKARTS.bin in this folder.
#     ./build.sh
set -e
cd "$(dirname "$0")"
if ! command -v kos-cc >/dev/null 2>&1; then
    if [ -f /opt/toolchains/dc/kos/environ.sh ]; then
        source /opt/toolchains/dc/kos/environ.sh
    else
        echo "Can't find KallistiOS (/opt/toolchains/dc/kos/environ.sh)"; exit 1
    fi
fi
if [ ! -f MANICKARTS.cue ] || [ ! -f MANICKARTS.bin ]; then
    echo "Put your own Manic Karts CD image (MANICKARTS.cue and MANICKARTS.bin) in this folder."; exit 1
fi
mkdir -p build data disc/data
# 1. the game's files off the CD image, unpacked (once)
if [ ! -f extracted/KART/VIRGIN.PCX ]; then
    echo "== Reading the CD image"
    python3 tools/extract_cd.py
    cc -O2 -o build/unkart tools/unkart.c
    build/unkart extracted/KART.DAT extracted/K.EXE extracted/KART
fi
# 2. the game's data converted for the Dreamcast (only when missing, or when a converter
#    or one of your own pictures in mods/ is newer)
if [ ! -f data/tracks.bin ] || [ tools/build_assets.py -nt data/tracks.bin ] || \
   [ -n "$(find mods -newer data/tracks.bin -type f 2>/dev/null)" ]; then
    echo "== Converting the tracks"
    python3 tools/build_assets.py
fi
if [ ! -f data/fe_HM1.bin ] || [ tools/build_front.py -nt data/fe_HM1.bin ]; then
    echo "== Converting the menus"
    python3 tools/build_front.py
fi
if [ ! -f data/race.bin ] || [ tools/build_race.py -nt data/race.bin ]; then
    python3 tools/build_race.py
fi
if [ ! -f data/AU_kqE3.tex ] || [ tools/build_vq.py -nt data/AU_kqE3.tex ]; then
    echo "== Compressing the karts (a few minutes, once)"
    python3 tools/build_vq.py
fi
# 3. the music: the CD's audio tracks as ADPCM for the sound chip (once)
if [ ! -f data/mus02.adp ]; then
    echo "== Converting the music"
    cc -O2 -o build/mkmusic tools/mkmusic.c
    build/mkmusic MANICKARTS.cue MANICKARTS.bin data
fi
# 4. the program
echo "== Compiling"
mkdir -p build/dc
rm -f build/mk.elf manic-karts.cdi
OBJS=""
for f in src/*.c platform/dc/plat_dc.c; do
    o=build/dc/$(basename "${f%.c}").o
    kos-cc -O2 -ffast-math -Wall -Wno-unused-function -Iinclude -Isrc -c "$f" -o "$o"
    OBJS="$OBJS $o"
done
kos-cc -o build/mk.elf $OBJS -lsh4zam -lm
# 5. the disc: the program and data/
echo "== Making the disc image"
cp data/* disc/data/
M=$(command -v mkdcdisc || find "$HOME/dreamcast-tools" "$HOME/dreamcast-projects" /opt /usr/local -name mkdcdisc -type f 2>/dev/null | head -1)
if [ -z "$M" ]; then
    echo "Can't find mkdcdisc"; exit 1
fi
"$M" -e build/mk.elf -D disc -n "MANIC KARTS" -o manic-karts.cdi --allow-overwrite
ls -la manic-karts.cdi
