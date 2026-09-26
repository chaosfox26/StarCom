# Known issues (prototype)

- Stale notes: if your friend ran `chat` in the last 10 minutes but is offline now, StarCom finds their old note. It tries to connect for about 40 seconds before giving up, and the message says how old their note was.
- Packets that arrive while StarCom is talking to the meeting point are skipped; the handshake repeats itself, so this only costs a moment.
- The identity file is not encrypted yet (see DATA.md).
- Command line only; there is no window yet.
- One friend at a time; there are no hubs or group chats yet.
- The handshake replay list on a listening peer is not size-capped yet.
