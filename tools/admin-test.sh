#!/bin/sh
set -u
BIN="${1:?use: tools/admin-test.sh <starcom binary built with the entry point 127.0.0.1:PORT> [PORT]}"
PORT="${2:-47000}"
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
W="$(mktemp -d)"
FAILS=0
CHECKS=0
PIDS=""

cleanup(){
    exec 3>&- 4>&- 5>&- 2>/dev/null
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

mkdir -p "$W/relay" "$W/a" "$W/b" "$W/c"
(cd "$W/relay" && exec timeout 300 "$BIN" relay "$PORT" > /dev/null 2>&1) &
PIDS="$PIDS $!"
sleep 2
STARCOM_HOME="$W/a" "$BIN" name alice > /dev/null
STARCOM_HOME="$W/b" "$BIN" name bob > /dev/null
STARCOM_HOME="$W/c" "$BIN" name carol > /dev/null
mkfifo "$W/fa" "$W/fb" "$W/fc"
STARCOM_HOME="$W/a" timeout 250 "$BIN" hub < "$W/fa" > "$W/a.out" 2>&1 &
PIDS="$PIDS $!"
STARCOM_HOME="$W/b" timeout 250 "$BIN" hub < "$W/fb" > "$W/b.out" 2>&1 &
PIDS="$PIDS $!"
STARCOM_HOME="$W/c" timeout 250 "$BIN" hub < "$W/fc" > "$W/c.out" 2>&1 &
PIDS="$PIDS $!"
exec 3> "$W/fa"
exec 4> "$W/fb"
exec 5> "$W/fc"
expect "$W/a.out" "Online as @alice" 60 "roles: alice is online"
expect "$W/b.out" "Online as @bob" 60 "roles: bob is online"
expect "$W/c.out" "Online as @carol" 60 "roles: carol is online"
echo "/create Roles Hub" >&3
expect "$W/a.out" "You are in Roles Hub" 20 "roles: alice creates a hub"
echo "/invite" >&3
expect "$W/a.out" "SCH1-" 20 "roles: alice makes an invite"
CODE="$(grep -o 'SCH1-[A-Za-z0-9_.@-]*' "$W/a.out" | head -1)"
echo "/join $CODE" >&5
expect "$W/c.out" "You are in Roles Hub" 60 "roles: carol joins"
echo "/join $CODE" >&4
expect "$W/b.out" "You are in Roles Hub" 60 "roles: bob joins"
echo "/admin @carol" >&3
sleep 6
echo "/who" >&5
expect "$W/c.out" "@carol (admin, online)" 20 "roles: carol sees herself as an admin"
echo "/kick @alice" >&5
expect "$W/c.out" "same or a higher role" 20 "roles: an admin cannot remove the owner"
echo "/kick @bob" >&5
expect "$W/b.out" "no longer a member" 40 "roles: a member kicked by an admin is told"
sleep 2
before="$(grep -c "@bob (member" "$W/a.out")"
echo "/who" >&3
sleep 3
after="$(grep -c "@bob (member" "$W/a.out")"
if [ "$after" -eq "$before" ]; then check ok "roles: the owner no longer lists the kicked member"; else check no "roles: the owner no longer lists the kicked member"; fi

echo "$CHECKS checks, $FAILS failed"
[ "$FAILS" -eq 0 ]
