# User interface

## Decisions (from the owner)
- **Look:** as close as possible to the familiar desktop chat layout (SPEC section 6, "zero learning curve"), following the owner's reference picture: a far-left rail, a conversation column, the chat, and a floating call panel. Other products' logos, names and icons are never copied.
- **Colours:** the owner's theme only: magenta, blue, black and white, with greys in between. No green, amber or red. Where the reference picture uses green (online) or red (disconnect), StarCom uses blue and magenta.
- **Beginner friendly, intuitive, lightweight:**
  - the first start is one screen ("Choose your @name");
  - adding a friend is: type the @name, then Find, then Add;
  - every button has a plain tooltip, and no settings are needed to start.
- **Toolkit:** fully custom, our own C++ interface. No third-party UI library; only the operating system's drawing APIs sit underneath.

## Architecture
- **UI core** (portable C++): layout, widgets, theme, hit testing and input. It draws only through a small Canvas interface: fill, rounded rectangle, circle, line, text (measure, draw, wrap), and clipping.
- **Redraw only on change:** input, a new message, a network or call change. Never a busy loop (rule seven).
- **Windows backend:** a Win32 window, Direct2D for shapes, DirectWrite for text. Both are part of Windows.
  - Colour emoji come from the system emoji font.
  - Icons come from the icon font built into Windows (Segoe MDL2 Assets): vector icons, no image files, nothing to download.
- **Threads:** the window thread owns the interface. The network runs on its own threads (the relay link, one worker per conversation, and an acceptor for incoming conversations). They talk through a small locked event queue, so the interface never waits on the network.

## Layout

| Region | Width | Contents |
|---|---|---|
| Rail | 72 | The StarCom star at the top; Home (direct messages) with the selection bar; "+ Add Server" (servers are being built next, and the button says so) |
| Conversation column | 240 | Header "Direct Messages" with a "..." menu (Add a friend, Copy my @name); DIRECT MESSAGES with a +; one row per friend (initial, status dot, unread count, a phone mark while a call rings or runs); your panel at the bottom (@name, Online or Connecting..., the settings gear) |
| Chat | rest | Header: "@ nickname", then a status line, then icons (call, mute, deafen, notifications, network health). Messages grouped by sender with time stamps. Message box with + (files, coming soon), emoji picker and send. |
| Call panel | floats bottom right | "CALL", then Calling / Incoming (Accept, Decline) / In call with the timer. Mute, Deafen and DISCONNECT, plus "latency: N ms Ping". |
| Network Health | 240, toggled | Connection (through the relays), who sees your IP, encryption, latency (SPEC section 6) |

**Message requests:** when someone who is not in your friends list contacts you, the column shows them under MESSAGE REQUESTS ("@name?" and "not verified yet", with their friend code in the tooltip). The @name is only what they claim until you accept.
- **Accept:** StarCom first checks that their public @name record carries the same key as the person calling, then adds them and answers.
- **Ignore:** refuses the request.
- Nothing is answered before Accept. Requests expire after 60 seconds without a new attempt, and at most 5 are held at once.

**Settings (the gear):**
- change your @name;
- your friend code (with Copy);
- the data folder;
- About, with the AI statement.

## What each colour means
- **Blue:** online, connected, found.
- **Magenta:** connecting, attention, and the main buttons (Save, Add, Disconnect).
- **Grey:** not connected.
