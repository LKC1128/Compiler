#!/bin/sh
set -eu
project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output=${1:-"$project_root/compiler"}
if [ -n "${CXX:-}" ]; then
    cxx=$CXX
elif command -v clang++-18 >/dev/null 2>&1; then
    cxx=clang++-18
else
    cxx=clang++
fi
if ! command -v "$cxx" >/dev/null 2>&1; then
    printf '%s\n' "C++ compiler not found: $cxx (contest baseline: Clang 18)" >&2
    exit 1
fi
"$cxx" --std=c++17 -O2 -I"$project_root/include" \
    "$project_root"/src/*.cpp "$project_root"/src/frontend/*.cpp \
    "$project_root"/src/ir/*.cpp "$project_root"/src/analysis/*.cpp \
    "$project_root"/src/passes/*.cpp "$project_root"/src/backend/*.cpp \
    -lm -o "$output"
printf 'Compiler: %s\n' "$output"
