#!/bin/sh
set -u
BIN="${1:?use: tools/relay-failover-test.sh <starcom binary built with the entry point 127.0.0.1:47000>}"
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
W="$(mktemp -d)"
FAILS=0
CHECKS=0
PIDS=""

cleanup(){
    exec 3>&- 2>/dev/null
    for p in $PIDS; do kill "$p" 2>/dev/null; done
    if [ -n "$W" ] && [ -d "$W" ]; then rm -rf "$W"; fi
}
trap cleanup EXIT INT TERM

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

expect(){
    if wait_for "$1" "$2" "$3"; then check ok "$4"; else check no "$4"; fi
}

mkdir -p "$W/e" "$W/v1" "$W/v2" "$W/a"
(cd "$W/e" && exec timeout 900 "$BIN" relay 47000 > "$W/e.out" 2>&1) &
PIDS="$PIDS $!"
sleep 2
STARCOM_HOME="$W/v1" timeout 900 "$BIN" volunteer relay 47010 > "$W/v1.out" 2>&1 &
V1=$!
STARCOM_HOME="$W/v2" timeout 900 "$BIN" volunteer relay 47011 > "$W/v2.out" 2>&1 &
V2=$!
PIDS="$PIDS $V1 $V2"
expect "$W/v1.out" "listed by 1 of 1 entry points$" 30 "volunteer 1 is listed"
expect "$W/v2.out" "listed by 1 of 1 entry points$" 30 "volunteer 2 is listed"
STARCOM_HOME="$W/a" "$BIN" name alice > /dev/null

(sleep 12; echo "/quit") | STARCOM_HOME="$W/a" timeout 60 "$BIN" hub > "$W/a1.out" 2>&1
if grep -qE ":4701[01]" "$W/a/relays.txt" 2>/dev/null; then check ok "first run: the volunteers were learned and saved"; else check no "first run: the volunteers were learned and saved"; fi

mkfifo "$W/fa"
STARCOM_HOME="$W/a" timeout 400 "$BIN" hub < "$W/fa" > "$W/a2.out" 2>&1 &
PIDS="$PIDS $!"
exec 3> "$W/fa"
expect "$W/a2.out" "Online as @alice" 60 "second run: online through the volunteers"
kill $V1 $V2
expect "$W/a2.out" "Lost the connection to your relay" 120 "both volunteers go away: the client notices"
sleep 2
LOST_AT="$(grep -n "Lost the connection" "$W/a2.out" | head -1 | cut -d: -f1)"
if tail -n +"${LOST_AT:-1}" "$W/a2.out" | grep -q "Online as @alice"; then
    check ok "the client switches to another relay and is online again"
else
    n=0
    while [ "$n" -lt 90 ]; do
        if tail -n +"${LOST_AT:-1}" "$W/a2.out" | grep -q "Online as @alice"; then break; fi
        sleep 1
        n=$((n + 1))
    done
    if tail -n +"${LOST_AT:-1}" "$W/a2.out" | grep -q "Online as @alice"; then check ok "the client switches to another relay and is online again"; else check no "the client switches to another relay and is online again"; fi
fi
echo "/quit" >&3
exec 3>&-

echo "$CHECKS checks, $FAILS failed"
[ "$FAILS" -eq 0 ]
