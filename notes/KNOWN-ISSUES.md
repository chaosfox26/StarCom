# Known issues (prototype)

- If you close the window instead of typing `/quit`, StarCom cannot post its "I'm offline" note, so for up to 45 seconds your friend may try to reach you and then fall back to waiting at the relay. Type `/quit` when you can.
- If your friend closes and reopens StarCom while you are already waiting at the relay, you connect through the relay rather than directly.
- Packets that arrive while StarCom is talking to the meeting point are skipped; the handshake repeats itself, so this only costs a moment.
- The identity file is not encrypted yet (see DATA.md).
- The window is Windows-only for now (Linux uses the command line). Voice calls are command-line only (`starcom call`) until the engine update; the call button in the window says so. The window has no scroll bar yet (use the mouse wheel).
- One friend at a time; there are no hubs or group chats yet.
- The handshake replay list on a listening peer is not size-capped yet.
- Hide my IP edge case: if the two testers' computers look up the meeting-point name as different address types (IPv4 vs IPv6), the one without Hide my IP may spend about 10 seconds trying the relay address directly before switching to the relay. Nothing leaks; it only costs time.
- Double-clicking starcom.exe briefly flashes a console window before the StarCom window opens (the same file also runs the command-line tools).
- Voice packets are capped at 400 bytes of audio per 20 ms frame (about 160 kbps), plenty for voice; a future music mode at up to 510 kbps will need a larger cap.
- The voice playout buffer is fixed at 60 ms for now; it becomes adaptive to measured jitter when voice moves onto the engine.
