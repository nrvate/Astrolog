#!/bin/sh
# Build one of Ephemeris Prometheia's own command-line tools from the
# submodule, against the library the console build already compiles, and
# print the binary's path.
#
#   tools/prometheia-tool.sh de-trim         # cut a JPL DE file to a span
#   tools/prometheia-tool.sh spk-trim        # cut an SPK kernel to a span
#   tools/prometheia-tool.sh catalog-subset  # cut an EPM1 catalog to a list
#
# Those three are what tools/cut-ephemeris.sh and tools/bundle-bodies.py
# run. Prometheia's own CMake build makes them too, but that build is on
# nobody's machine but a Prometheia developer's: the submodule carries
# their sources, and the library they link is the one Makefile.prometheia
# builds for ./astrolog (obj-prometheia/libprometheia.a), so a checkout of
# this tree is enough. Each needs only the library's own headers.
#
# Compiled with the library's flags (C++20, -ffp-contract=off) and its
# zstd (PROMETHEIA_ZSTD_CFLAGS / PROMETHEIA_ZSTD_LIBS, as for the library).
# Rebuilt when its source or the library is newer than the binary.
set -eu
cd "$(dirname "$0")/.."
case ${1:-} in
  de-trim)        src=prometheia/tools/detrim/de_trim_main.cpp ;;
  spk-trim)       src=prometheia/tools/spktrim/spk_trim_main.cpp ;;
  catalog-subset) src=prometheia/tools/convert/subset_main.cpp ;;
  *) echo "usage: tools/prometheia-tool.sh de-trim|spk-trim|catalog-subset" >&2; exit 2 ;;
esac
[ -f "$src" ] || { echo "$src is not there: git submodule update --init" >&2; exit 2; }
lib=obj-prometheia/libprometheia.a
make -s "$lib" >&2
out=obj-prometheia/tools/prometheia-$1
if [ ! -x "$out" ] || [ "$src" -nt "$out" ] || [ "$lib" -nt "$out" ]; then
  mkdir -p obj-prometheia/tools
  ${CXX:-g++} -std=c++20 -ffp-contract=off -O2 -Iprometheia/include \
    ${PROMETHEIA_ZSTD_CFLAGS:-} -o "$out" "$src" "$lib" \
    ${PROMETHEIA_ZSTD_LIBS:--lzstd} -lm >&2
fi
echo "$PWD/$out"
