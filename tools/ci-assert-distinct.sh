#!/bin/sh
# Assert a directory of renders is neither empty nor repetitive.
#
#   tools/ci-assert-distinct.sh out/qtg 24
#
# QT_CI_PLAN.md item 6.5. The QTGRAPHDIR run draws every graphics chart
# type offscreen in about four seconds, and "24 files appeared" is not a
# pass criterion: a blank chart is byte-identical to every other blank
# chart, so a run where the drawing code did nothing produces a full
# directory of matching files. Distinctness is the cheap property that
# catches it.
#
# The expected count is exact and optional. Exact because a floor tests
# the guess -- this project has the scar, twice: chart-matrix.sh shipped
# with 15 of 70 invocations erroring and switch-matrix.sh capped its
# output at 30 lines of 159, and both diffed to zero and read as proofs.
# When a chart type is added, this number moves in the same commit, which
# is the point.
#
# Exit 0 when the directory holds the expected number of files and no two
# of them are identical.
set -eu

dir=${1:?usage: ci-assert-distinct.sh <dir> [expected-count]}
want=${2:-}

if [ ! -d "$dir" ]; then
  echo "NOT DISTINCT: $dir does not exist -- the render step produced nothing."
  exit 1
fi

got=$(find "$dir" -type f | wc -l)
if [ "$got" -eq 0 ]; then
  echo "NOT DISTINCT: $dir is empty -- the render step produced nothing."
  exit 1
fi
if [ -n "$want" ] && [ "$got" -ne "$want" ]; then
  echo "NOT DISTINCT: $dir holds $got files, expected exactly $want."
  echo "A chart type added or removed moves this number; update the caller."
  exit 1
fi

uniq_count=$(find "$dir" -type f -exec md5sum {} + | cut -d' ' -f1 | sort -u | wc -l)
if [ "$uniq_count" -ne "$got" ]; then
  echo "NOT DISTINCT: $got files, only $uniq_count distinct checksums --"
  find "$dir" -type f -exec md5sum {} + | sort | awk '
    { if ($1 == prev) { if (!shown[prev]++) print "  " prevfile; print "  " $2 }
      prev = $1; prevfile = $2 }'
  echo "Identical renders mean the drawing code produced the same image twice,"
  echo "which is what a blank chart looks like."
  exit 1
fi

echo "distinct: $got renders in $dir, $uniq_count distinct checksums"
