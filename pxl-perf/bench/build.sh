#!/bin/sh
# build.sh VARIANT [CC] [extra flags...]: builds out/bench-VARIANT-CC
. "$(dirname "$0")/env.sh"
V=$1; shift
CC=${1:-clang-20}; [ $# -gt 0 ] && shift
$CC -std=c23 -O2 -g -Wall -Wextra -I$SP/variants/$V -isystem $PREFIX/include "$@" \
  -o $SP/out/bench-$V-$CC $SP/bench/bench.c -L$PREFIX/lib -Wl,-rpath,$PREFIX/lib -lSDL3 -lm
