#!/bin/sh
# cg.sh VARIANT [scenes...]: callgrind instruction counts per item, per scene.
. "$(dirname "$0")/env.sh"
V=$1; shift
D=$SP/out/cg-$V; rm -rf $D; mkdir -p $D
cp $SP/out/bench-$V-clang-20 $D/bench
valgrind --tool=callgrind --callgrind-out-file=$D/cg.out --toggle-collect='scene_*' \
  --toggle-collect=pxl_end_frame \
  $D/bench -f 3 "$@" > $D/bench.txt 2> $D/valgrind.txt
python3 -I $SP/bench/cg_parse.py $D
