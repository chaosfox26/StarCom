#!/bin/sh
set -e
cd "$(dirname "$0")"
mkdir -p build build/opus
CC="${CC:-cc}"
CXX="${CXX:-c++}"
"$CC" -O2 -c -o build/monocypher.o third_party/monocypher/monocypher.c
"$CC" -O2 -c -o build/monocypher-ed25519.o third_party/monocypher/monocypher-ed25519.c
OPUS="third_party/opus"
OPUS_FLAGS="-O2 -DOPUS_BUILD -DUSE_ALLOCA -DHAVE_LRINT -DHAVE_LRINTF -I$OPUS/include -I$OPUS/celt -I$OPUS/silk -I$OPUS/silk/float -I$OPUS/src"
OBJS=""
for f in $(tr -d '\r' < third_party/opus-sources.txt); do
    o="build/opus/$(echo "$f" | tr '/' '_').o"
    if [ ! "$o" -nt "$OPUS/$f" ]; then "$CC" $OPUS_FLAGS -c -o "$o" "$OPUS/$f"; fi
    OBJS="$OBJS $o"
done
rm -f build/libopus.a
ar rcs build/libopus.a $OBJS
set --
if [ -f .private/seeds.txt ]; then
    SEEDS=$(tr -d '\r ' < .private/seeds.txt | grep -v '^$' | paste -sd, -)
    if [ -n "$SEEDS" ]; then set -- "$@" "-DSTARCOM_SEEDS=\"$SEEDS\""; fi
fi
"$CXX" -std=c++17 -O2 -Wall -Wextra "$@" -I$OPUS/include -o build/starcom src/*.cpp build/monocypher.o build/monocypher-ed25519.o build/libopus.a -ldl -lm -lpthread
echo "BUILD OK: build/starcom"
