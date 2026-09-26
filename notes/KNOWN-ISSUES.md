# Known issues (prototype)

- Stale notes: if your friend ran `chat` in the last 10 minutes but is offline now, StarCom finds their old note. It tries to connect for about 40 seconds before giving up, and the message says how old their note was.
- Packets that arrive while StarCom is talking to the meeting point are skipped; the handshake repeats itself, so this only costs a moment.
- The identity file is not encrypted yet (see DATA.md).
- Command line only; there is no window yet.
- One friend at a time; there are no hubs or group chats yet.
- The handshake replay list on a listening peer is not size-capped yet.
- Hide my IP edge case: if the two testers' computers look up the meeting-point name as different address types (IPv4 vs IPv6), the one without Hide my IP may spend about 10 seconds trying the relay address directly before switching to the relay. Nothing leaks; it only costs time.
