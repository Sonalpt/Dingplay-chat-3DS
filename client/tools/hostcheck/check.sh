#!/bin/bash
# Syntax-checks the client against real devkitPro headers with the host clang,
# for machines without devkitARM. Usage: tools/hostcheck/check.sh <dir with libctru/ citro3d/ citro2d/ clones>
# (clone with: git clone --depth 1 https://github.com/devkitPro/{libctru,citro3d,citro2d}.git)
D="${1:?path to header clones}"
cd "$(dirname "$0")/../.." || exit 1
INC="-I$D/libctru/libctru/include -I$D/citro3d/include -I$D/citro2d/include -Isource -Isource/third_party -Itools/hostcheck"
fail=0
for f in source/*.c source/screens/*.c source/local/*.c; do
  out=$(clang -fsyntax-only -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-missing-field-initializers -Wno-gnu -Wno-sign-compare -Wno-pointer-to-int-cast -Wno-void-pointer-to-int-cast -Wno-int-to-pointer-cast -D__3DS__ -D_GNU_SOURCE $INC -include tools/hostcheck/shim.h "$f" 2>&1 | grep -v "$D" | grep -B1 -A3 "source/")
  if [ -n "$out" ]; then echo "== $f"; echo "$out" | head -60; fail=1; fi
done
[ $fail -eq 0 ] && echo "ALL CLEAN"
exit $fail
