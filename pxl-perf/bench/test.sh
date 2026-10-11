#!/bin/sh
# test.sh VARIANT [CC]: runs tests/pxl/test_pxl.c against the variant, with ASan+UBSan and the project warnings.
. "$(dirname "$0")/env.sh"
V=$1; CC=${2:-clang-20}
cd /home/user/libs
FLAGS=$(grep -v '^-I\.$' compile_flags.txt | tr '\n' ' ')
mkdir -p $SP/out/t-$V
cp tests/pxl/test_pxl.c tests/pxl/test_shaders.h $SP/out/t-$V/
$CC -I$SP/variants/$V -I. $FLAGS -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all \
  -isystem $PREFIX/include -o $SP/out/t-$V/test_pxl $SP/out/t-$V/test_pxl.c -L$PREFIX/lib -Wl,-rpath,$PREFIX/lib -lSDL3 -lm && \
PXL_TEST_REQUIRE_GPU=1 $SP/out/t-$V/test_pxl 2>&1 | tail -3
