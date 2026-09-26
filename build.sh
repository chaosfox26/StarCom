#!/bin/sh
set -e
cd "$(dirname "$0")"
mkdir -p build
CC="${CC:-cc}"
CXX="${CXX:-c++}"
"$CC" -O2 -c -o build/monocypher.o third_party/monocypher/monocypher.c
"$CC" -O2 -c -o build/monocypher-ed25519.o third_party/monocypher/monocypher-ed25519.c
"$CXX" -std=c++17 -O2 -Wall -Wextra -o build/starcom src/*.cpp build/monocypher.o build/monocypher-ed25519.o -ldl
echo "BUILD OK: build/starcom"
