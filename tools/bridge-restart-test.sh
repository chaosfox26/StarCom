#!/bin/sh
set -u
BIN="${1:?use: tools/bridge-restart-test.sh <starcom binary built with the entry point 127.0.0.1:47000>}"
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
W="$(mktemp -d)"
FAILS=0
CHECKS=0
PIDS=""

cleanup(){
    for p in $PIDS; do kill "$p" 2>/dev/null; done
    if [ -n "$W" ] && [ -d "$W" ]; then rm -rf "$W"; fi
}
trap cleanup EXIT INT TERM

check(){
    CHECKS=$((CHECKS + 1))
    if [ "$1" = ok ]; then echo "ok: $2"; else echo "FAIL: $2"; FAILS=$((FAILS + 1)); fi
}

mkdir -p "$W/r" "$W/v"
(cd "$W/r" && exec timeout 400 "$BIN" relay 47000 > /dev/null 2>&1) &
PIDS="$PIDS $!"
sleep 2
(sleep 14; echo "/quit") | STARCOM_HOME="$W/v" timeout 40 "$BIN" volunteer run > "$W/v1.out" 2>&1
if grep -q "relays 1," "$W/v1.out"; then check ok "volunteer: the first run is accepted by the relay"; else check no "volunteer: the first run is accepted by the relay"; fi
(sleep 150; echo "/quit") | STARCOM_HOME="$W/v" timeout 200 "$BIN" volunteer run > "$W/v2.out" 2>&1
if grep -q "relays 0," "$W/v2.out"; then check ok "volunteer: a run started right after is refused at first"; else check no "volunteer: a run started right after is refused at first"; fi
if grep -q "relays 1," "$W/v2.out"; then check ok "volunteer: the refused run is accepted again within 150 seconds"; else check no "volunteer: the refused run is accepted again within 150 seconds"; fi

echo "$CHECKS checks, $FAILS failed"
[ "$FAILS" -eq 0 ]
