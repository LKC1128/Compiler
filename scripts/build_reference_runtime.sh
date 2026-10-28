#!/bin/sh
set -eu
project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output=${1:-"$project_root/build/reference-runtime"}
mkdir -p "$output"
cc=${AARCH64_CC:-aarch64-linux-gnu-gcc-11}
"$cc" -march=armv8-a -O2 -c "$project_root/tests/reference-runtime/sylib.c" -o "$output/sylib.o"
aarch64-linux-gnu-ar rcs "$output/libsysy.a" "$output/sylib.o"
printf '%s\n' "$output/libsysy.a"
