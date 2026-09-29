#!/bin/sh
set -u
BIN="${1:?use: tools/two-relay-test.sh <starcom binary built with the entry points 127.0.0.1:47000 and 127.0.0.1:47001>}"
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
W="$(mktemp -d)"
FAILS=0
CHECKS=0
PIDS=""

cleanup(){
    exec 3>&- 4>&- 2>/dev/null
    for p in $PIDS; do kill "$p" 2>/dev/null; done
    if [ -n "$W" ] && [ -d "$W" ]; then rm -rf "$W"; fi
}
trap cleanup EXIT INT TERM

udp(){ awk '/^Udp:/ && $2 ~ /^[0-9]+$/ { print $2 }' /proc/net/snmp; }

wait_for(){
    n=0
    while [ "$n" -lt "$3" ]; do
        if grep -q -- "$2" "$1" 2>/dev/null; then return 0; fi
        sleep 1
        n=$((n + 1))
    done
    return 1
}

check(){
    CHECKS=$((CHECKS + 1))
    if [ "$1" = ok ]; then echo "ok: $2"; else echo "FAIL: $2"; FAILS=$((FAILS + 1)); fi
}

mkdir -p "$W/r1" "$W/r2" "$W/a" "$W/b"
(cd "$W/r1" && exec timeout 300 "$BIN" relay 47000 > "$W/r1.out" 2>&1) &
PIDS="$PIDS $!"
(cd "$W/r2" && exec timeout 300 "$BIN" relay 47001 > "$W/r2.out" 2>&1) &
PIDS="$PIDS $!"
sleep 2
STARCOM_HOME="$W/a" "$BIN" name alice > /dev/null
STARCOM_HOME="$W/b" "$BIN" name bob > /dev/null
mkfifo "$W/ca" "$W/cb"
STARCOM_HOME="$W/a" timeout 200 "$BIN" chat @bob < "$W/ca" > "$W/ca.out" 2>&1 &
PIDS="$PIDS $!"
STARCOM_HOME="$W/b" timeout 200 "$BIN" chat @alice < "$W/cb" > "$W/cb.out" 2>&1 &
PIDS="$PIDS $!"
exec 3> "$W/ca"
exec 4> "$W/cb"
if wait_for "$W/ca.out" "Chat open" 90 && wait_for "$W/cb.out" "Chat open" 90; then check ok "two relays: alice and bob connect"; else check no "two relays: alice and bob connect"; fi
echo "across-two-relays" >&3
if wait_for "$W/cb.out" "across-two-relays" 20; then check ok "two relays: a message arrives"; else check no "two relays: a message arrives"; fi
echo "/quit" >&3
echo "/quit" >&4
sleep 35
before="$(udp)"
sleep 10
after="$(udp)"
moved=$((after - before))
if [ "$moved" -lt 1000 ]; then check ok "two relays: an idle relay link stays quiet ($moved datagrams in 10 seconds)"; else check no "two relays: an idle relay link stays quiet ($moved datagrams in 10 seconds, expected under 1000)"; fi

taken=0
for i in 1 2 3 4 5 6; do
    mkdir -p "$W/u$i"
    STARCOM_HOME="$W/u$i" "$BIN" name "user$i" > /dev/null
    (sleep 9; echo "/quit") | STARCOM_HOME="$W/u$i" timeout 20 "$BIN" hub > "$W/u$i.out" 2>&1
    if grep -q "already taken" "$W/u$i.out"; then taken=$((taken + 1)); fi
done
if [ "$taken" -eq 0 ]; then check ok "two relays: nobody is wrongly told their @name is taken (6 new users)"; else check no "two relays: $taken of 6 new users were wrongly told their @name is taken"; fi

echo "$CHECKS checks, $FAILS failed"
[ "$FAILS" -eq 0 ]
