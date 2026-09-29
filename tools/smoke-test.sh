#!/bin/sh
set -u
BIN="${1:?use: tools/smoke-test.sh <starcom binary built with the entry point 127.0.0.1:PORT> [PORT]}"
PORT="${2:-47000}"
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

mkdir -p "$W/relay" "$W/a" "$W/b"
(cd "$W/relay" && exec timeout 600 "$BIN" relay "$PORT" > "$W/relay.out" 2>&1) &
PIDS="$PIDS $!"
expect "$W/relay.out" "Star DNS seed on UDP port $PORT" 10 "relay starts on loopback port $PORT"

STARCOM_HOME="$W/a" "$BIN" name alice > /dev/null
STARCOM_HOME="$W/b" "$BIN" name bob > /dev/null

mkfifo "$W/ca" "$W/cb"
STARCOM_HOME="$W/a" timeout 150 "$BIN" chat @bob < "$W/ca" > "$W/ca.out" 2>&1 &
PIDS="$PIDS $!"
STARCOM_HOME="$W/b" timeout 150 "$BIN" chat @alice < "$W/cb" > "$W/cb.out" 2>&1 &
PIDS="$PIDS $!"
exec 3> "$W/ca"
exec 4> "$W/cb"
expect "$W/ca.out" "Chat open with @bob" 90 "chat: alice reaches bob through the relay"
expect "$W/cb.out" "Chat open with @alice" 90 "chat: bob reaches alice through the relay"
echo "message-from-alice" >&3
echo "message-from-bob" >&4
expect "$W/cb.out" "\[@alice\] message-from-alice" 20 "chat: bob receives alice's message"
expect "$W/ca.out" "\[@bob\] message-from-bob" 20 "chat: alice receives bob's message"
if grep -q "127\.0\.0\.1" "$W/ca.out" "$W/cb.out"; then check no "chat: no address shown to either user"; else check ok "chat: no address shown to either user"; fi
echo "/quit" >&3
echo "/quit" >&4
exec 3>&- 4>&-
wait_for "$W/ca.out" "you left the chat" 10 > /dev/null

mkfifo "$W/ha" "$W/hb"
STARCOM_HOME="$W/a" timeout 150 "$BIN" hub < "$W/ha" > "$W/ha.out" 2>&1 &
PIDS="$PIDS $!"
exec 3> "$W/ha"
expect "$W/ha.out" "Online as @alice" 60 "hub: alice is online"
echo "/create Smoke Hub" >&3
expect "$W/ha.out" "You are in Smoke Hub" 20 "hub: alice creates a hub"
echo "/invite" >&3
expect "$W/ha.out" "SCH1-" 20 "hub: alice makes an invite"
CODE="$(grep -o 'SCH1-[A-Za-z0-9_.@-]*' "$W/ha.out" | head -1)"
STARCOM_HOME="$W/b" timeout 120 "$BIN" hub < "$W/hb" > "$W/hb.out" 2>&1 &
PIDS="$PIDS $!"
exec 4> "$W/hb"
expect "$W/hb.out" "Online as @bob" 60 "hub: bob is online"
echo "/join $CODE" >&4
expect "$W/hb.out" "You are in Smoke Hub" 60 "hub: bob joins with the invite"
echo "hello-hub-from-bob" >&4
expect "$W/ha.out" "@bob: hello-hub-from-bob" 30 "hub: alice receives bob's hub message"
echo "hello-hub-from-alice" >&3
expect "$W/hb.out" "@alice: hello-hub-from-alice" 30 "hub: bob receives alice's hub message"
echo "/who" >&3
expect "$W/ha.out" "@bob (member, online)" 10 "hub: alice sees bob online as a member"
echo "/who" >&4
expect "$W/hb.out" "@alice (owner, online)" 10 "hub: bob sees alice online as the owner"
if grep -q "127\.0\.0\.1" "$W/ha.out" "$W/hb.out"; then check no "hub: no address shown to either user"; else check ok "hub: no address shown to either user"; fi
if [ "${SMOKE_VOICE:-0}" = 1 ]; then
    echo "/vc" >&4
    sleep 4
    echo "/who" >&3
    expect "$W/ha.out" "@bob (member, online, in voice)" 15 "voice: alice sees bob in the voice channel"
    echo "/who" >&4
    expect "$W/hb.out" "@bob (member, online, in voice)" 15 "voice: bob is still in the voice channel after several refreshes"
    echo "/kick @bob" >&3
    expect "$W/hb.out" "no longer a member" 30 "voice: bob is told he was removed"
fi
echo "/quit" >&4
echo "/quit" >&3
exec 3>&- 4>&-
wait_for "$W/ha.out" "StarCom closed" 10 > /dev/null

if [ "$(wc -l < "$W/relay.out")" -gt 1 ]; then check no "relay: wrote nothing after its start line"; else check ok "relay: wrote nothing after its start line"; fi

echo "$CHECKS checks, $FAILS failed"
[ "$FAILS" -eq 0 ]
