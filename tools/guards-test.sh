#!/bin/sh
set -u
BIN="${1:?use: tools/guards-test.sh <starcom binary built with the entry point 127.0.0.1:47000>}"
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

run_client(){
    (sleep 12; echo "/quit") | STARCOM_HOME="$W/a" timeout 60 "$BIN" hub > "$W/a.$1" 2>&1
}

mkdir -p "$W/e" "$W/v1" "$W/v2" "$W/v3" "$W/a"
(cd "$W/e" && exec timeout 600 "$BIN" relay 47000 > "$W/e.out" 2>&1) &
PIDS="$PIDS $!"
sleep 2
for i in 1 2 3; do
    STARCOM_HOME="$W/v$i" timeout 600 "$BIN" volunteer relay 4701$((i - 1)) > "$W/v$i.out" 2>&1 &
    PIDS="$PIDS $!"
done
expect "$W/v1.out" "listed by 1 of 1 entry points$" 30 "volunteer 1 is listed"
expect "$W/v2.out" "listed by 1 of 1 entry points$" 30 "volunteer 2 is listed"
expect "$W/v3.out" "listed by 1 of 1 entry points$" 30 "volunteer 3 is listed"
STARCOM_HOME="$W/a" "$BIN" name alice > /dev/null

printf '127.0.0.1:47012 0\n127.0.0.1:47020 0\n' > "$W/a/relays.txt"
run_client r1
if [ "$(sed -n 1p "$W/a/relays.txt")" = "127.0.0.1:47012 0" ]; then check ok "the relay the client already trusted stays first after the entry relay lists others"; else check no "the relay the client already trusted stays first after the entry relay lists others"; fi
if grep -qE "^127.0.0.1:4701[01] 0$" "$W/a/relays.txt"; then check ok "newly listed relays are added behind the existing ones"; else check no "newly listed relays are added behind the existing ones"; fi
if grep -q "^127.0.0.1:47020 1$" "$W/a/relays.txt"; then check ok "a relay that did not answer gets one strike and is kept"; else check no "a relay that did not answer gets one strike and is kept"; fi
if grep -q "Online as @alice" "$W/a.r1"; then check ok "the client came online through the kept relay"; else check no "the client came online through the kept relay"; fi

run_client r2
if grep -q "^127.0.0.1:47020 2$" "$W/a/relays.txt"; then check ok "second failed start: two strikes"; else check no "second failed start: two strikes"; fi
run_client r3
if ! grep -q "47020" "$W/a/relays.txt"; then check ok "third failed start: the dead relay is dropped"; else check no "third failed start: the dead relay is dropped"; fi
if [ "$(sed -n 1p "$W/a/relays.txt")" = "127.0.0.1:47012 0" ]; then check ok "the first relay is still the same after three runs"; else check no "the first relay is still the same after three runs"; fi

echo "$CHECKS checks, $FAILS failed"
[ "$FAILS" -eq 0 ]
