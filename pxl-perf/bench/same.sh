#!/bin/sh
# same.sh VARIANT: renders each scene with base and VARIANT, compares the pixels.
. "$(dirname "$0")/env.sh"
V=$1
for v in base $V; do
  mkdir -p $SP/out/px-$v && $SP/out/bench-$v-clang-20 -f 2 -d $SP/out/px-$v > /dev/null || exit 1
done
for f in $SP/out/px-base/*.rgba; do
  n=$(basename $f)
  if cmp -s $f $SP/out/px-$V/$n; then echo "same  $n"; else echo "DIFF  $n ($(cmp -l $f $SP/out/px-$V/$n | wc -l) bytes)"; fi
done
