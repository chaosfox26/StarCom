#!/bin/sh
set -u
BIN="${1:?use: tools/peer-relay-test.sh <starcom binary built with the entry point 127.0.0.1:47000>}"
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

cat > "$W/count.py" <<'PY'
import socket, struct, sys, time
dur = float(sys.argv[1])
try:
    s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.ntohs(3))
    s.bind(('lo', 0))
except Exception:
    print('unavailable')
    sys.exit(0)
s.settimeout(0.5)
entry = volunteer = 0
end = time.time() + dur
while time.time() < end:
    try:
        d = s.recv(65535)
    except socket.timeout:
        continue
    if len(d) > 42 and d[12:14] == b'\x08\x00' and d[23] == 17:
        ihl = (d[14] & 15) * 4
        dp = struct.unpack('!H', d[14 + ihl + 2:14 + ihl + 4])[0]
        if dp == 47000:
            entry += 1
        elif dp in (47010, 47011):
            volunteer += 1
print('%d %d' % (entry, volunteer))
PY

mkdir -p "$W/e" "$W/v1" "$W/v2" "$W/a" "$W/b"
(cd "$W/e" && exec timeout 600 "$BIN" relay 47000 > "$W/e.out" 2>&1) &
PIDS="$PIDS $!"
sleep 2
STARCOM_HOME="$W/v1" timeout 600 "$BIN" volunteer relay 47010 > "$W/v1.out" 2>&1 &
PIDS="$PIDS $!"
STARCOM_HOME="$W/v2" timeout 600 "$BIN" volunteer relay 47011 > "$W/v2.out" 2>&1 &
PIDS="$PIDS $!"
expect "$W/v1.out" "listed by 1 of 1 entry points$" 30 "volunteer 1 is verified and listed by the entry relay"
expect "$W/v2.out" "listed by 1 of 1 entry points$" 30 "volunteer 2 is verified and listed by the entry relay"

STARCOM_HOME="$W/a" "$BIN" name alice > /dev/null
STARCOM_HOME="$W/b" "$BIN" name bob > /dev/null

run_pair(){
    rm -f "$W/ca" "$W/cb"
    mkfifo "$W/ca" "$W/cb"
    STARCOM_HOME="$W/a" timeout 120 "$BIN" chat @bob < "$W/ca" > "$W/ca.$1" 2>&1 &
    PA=$!
    STARCOM_HOME="$W/b" timeout 120 "$BIN" chat @alice < "$W/cb" > "$W/cb.$1" 2>&1 &
    PB=$!
    PIDS="$PIDS $PA $PB"
    exec 3> "$W/ca"
    exec 4> "$W/cb"
}

run_pair r1
expect "$W/ca.r1" "Chat open with @bob" 90 "round 1: chat works through the entry relay"
expect "$W/cb.r1" "Chat open with @alice" 90 "round 1: the other side is connected too"
sleep 6
echo "/quit" >&3
echo "/quit" >&4
exec 3>&- 4>&-
sleep 3
if grep -qE ":4701[01]" "$W/a/relays.txt" 2>/dev/null && grep -qE ":4701[01]" "$W/b/relays.txt" 2>/dev/null; then check ok "round 1: both clients learned the volunteer relays and saved them"; else check no "round 1: both clients learned the volunteer relays and saved them"; fi

python3 "$W/count.py" 40 > "$W/count.out" 2>&1 &
CP=$!
sleep 1
run_pair r2
expect "$W/ca.r2" "Chat open with @bob" 90 "round 2: chat works using the volunteer relays"
expect "$W/cb.r2" "Chat open with @alice" 90 "round 2: the other side is connected too"
echo "via-volunteers-a" >&3
echo "via-volunteers-b" >&4
expect "$W/cb.r2" "\[@alice\] via-volunteers-a" 20 "round 2: bob receives alice's message"
expect "$W/ca.r2" "\[@bob\] via-volunteers-b" 20 "round 2: alice receives bob's message"
wait $CP
read ENTRY VOL < "$W/count.out" 2>/dev/null
if [ "${ENTRY:-}" = "unavailable" ] || [ -z "${VOL:-}" ]; then
    echo "skipped: packet counts need permission to watch the loopback interface"
elif [ "$VOL" -gt "$ENTRY" ]; then
    check ok "round 2: traffic goes to the volunteer relays ($VOL packets) rather than the entry relay ($ENTRY packets)"
else
    check no "round 2: traffic goes to the volunteer relays ($VOL packets) rather than the entry relay ($ENTRY packets)"
fi
echo "/quit" >&3
echo "/quit" >&4
exec 3>&- 4>&-

for i in 3 4 5; do
    mkdir -p "$W/v$i"
    STARCOM_HOME="$W/v$i" timeout 600 "$BIN" volunteer relay 4701$((i - 1)) > "$W/v$i.out" 2>&1 &
    PIDS="$PIDS $!"
done
sleep 75
LISTED=0
for i in 1 2 3 4 5; do
    if tail -n 1 "$W/v$i.out" | grep -q "listed by 1 of 1 entry points$"; then LISTED=$((LISTED + 1)); fi
done
if [ "$LISTED" -eq 4 ]; then check ok "limit: at most 4 relays from one network are listed ($LISTED of 5 volunteers)"; else check no "limit: at most 4 relays from one network are listed ($LISTED of 5 volunteers, expected 4)"; fi

echo "$CHECKS checks, $FAILS failed"
[ "$FAILS" -eq 0 ]
