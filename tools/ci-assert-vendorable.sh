#!/bin/bash
# tools/ci-assert-vendorable.sh -- ephsrv/ephproto.h stands on its own.
#
# The protocol header is a pinned interface: Ephemeris Prometheia vendors it
# under third_party/ and compiles it with -Wall -Wextra -Werror in C++20,
# from a tree where none of Astrolog's other headers exist. So it must be
# header-only, standard-library-only, and include nothing of this tree --
# and stay that way, which is what this asserts. It compiles a two-line
# translation unit that includes it, from a scratch directory with only the
# header's own directory on the include path.
#
#   tools/ci-assert-vendorable.sh [HEADER]     # default ephsrv/ephproto.h
#
# Exit 0 with a line saying so; nonzero with the compiler's complaint.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
HDR=${1:-ephsrv/ephproto.h}
SCRATCH=$(mktemp -d /tmp/ephproto-vendor.XXXXXX)
trap 'rm -rf "$SCRATCH"' EXIT

[ -f "$ROOT/$HDR" ] || { echo "VENDORABLE FAIL: no $HDR"; exit 1; }
# Copied, not included in place: a header that reached for a sibling of its
# own directory would still compile there and not where it is vendored.
mkdir -p "$SCRATCH/third_party"
cp "$ROOT/$HDR" "$SCRATCH/third_party/"
cat > "$SCRATCH/tu.cpp" << 'CPP'
#include "ephproto.h"
int main() { return (int)eph::kProtoVersion - 4; }
CPP
cd "$SCRATCH"
if ! g++ -std=c++20 -Wall -Wextra -Werror -I third_party -o tu tu.cpp 2> compile.log; then
  echo "VENDORABLE FAIL: $HDR does not compile standalone under C++20 -Werror"
  sed 's/^/  /' compile.log
  exit 1
fi
./tu || { echo "VENDORABLE FAIL: the vendored header's protocol version is not 4"; exit 1; }
echo "VENDORABLE PASS: $HDR compiles alone (C++20, -Wall -Wextra -Werror)"
