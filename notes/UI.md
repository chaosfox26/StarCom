# User interface: design

Status: design draft.

## Decisions (from the owner)
- **Layout:** as close as possible to the familiar desktop direct-message layout people already know (spec section 6: "zero learning curve"). Direct messages only, with no servers or hubs: a conversation column with the user panel at the bottom, the chat in the middle, and a right-hand panel. Other products' logos, names and icons are never copied.
- **Look:** a dark structure with StarCom's own colors: magenta and blue accents on near-black, with white text.
- **Toolkit:** fully custom, our own C++ interface. No third-party UI library. Only the operating system's drawing APIs sit underneath.

## Architecture
- **UI core** (portable C++):
  - layout;
  - widgets (buttons, list rows, text box, scroll view, tooltips);
  - theme;
  - hit testing and input.
  - It draws only through a small **Canvas** interface: fill rect, rounded rect, circle, line, text (measure + draw, with wrapping), clip push/pop.
  - It is retained-light: the whole frame is redrawn only when something changed (input, a new message, a stats tick), never in a busy loop (rule seven).
- **Platform backend** (one per OS):
  - Windows: a Win32 window, Direct2D for shapes, DirectWrite for text. Both are part of Windows. DirectWrite gives proper font fallback and color emoji.
  - Linux: an X11/Wayland window with our own backend (later).
- **Threads:**
  - The UI thread owns the window and the UI state.
  - The network runs on its own thread (sessions, chat, calls).
  - The two talk through two small locked queues (events in, commands out). The UI never blocks on the network.

## Layout (widths in device-independent pixels, dark theme)

| Region | Width | Contents |
|---|---|---|
| Conversation column | 240 | "Find or add a friend" box; Home row; direct-messages list (nickname, status dot); user panel pinned at the bottom (you + short code + copy) |
| Chat | rest | Header (friend name, call button, network-health toggle); message list (grouped by sender, time stamps); message box at the bottom |
| Right panel | 240 | Network Health (spec section 6): latency, jitter, loss, codec in use, bandwidth budget, per-lane allocation; connection type (direct / relay) and who can see your IP |

## Colors (StarCom theme)

| Token | Value | Used for |
|---|---|---|
| rail | #0B0B12 | darkest surface (search box, code box) |
| column | #12121A | conversation column |
| chat | #181822 | chat area |
| panel | #0F0F16 | user panel, right panel |
| hover | #24243A | row hover |
| selected | #2E2E4A | selected row |
| text | #F4F4FA | primary text |
| muted | #A3A6BD | secondary text |
| magenta | #E0359B | primary accent (buttons, unread, connecting, errors) |
| blue | #3D7BFF | secondary accent (online, connected, focus rings, links) |

## What the first GUI version does (using what already works)
1. On first start, create the identity in the StarCom data folder and show your friend code, with a copy button, a short how-to and the AI-transparency statement.
2. Add a friend by pasting their code and giving them a local nickname.
   - Friends are stored locally in `friends.txt` in the data folder: code + nickname only.
   - This is listed in DATA.md.
3. Click a friend to open a chat. Connecting (meeting point, direct or relay) happens in the background, with status in the header.
4. Messages appear grouped Discord-style. The history lives only in memory while the window is open, as in the command-line version.
   - Typing `/quit` as a message leaves the chat, as in the command-line version; your friend sees that you left. Clicking the friend again reconnects.
5. Call button: voice calls in the window arrive with the engine update (until then it says so).
6. Network Health panel shows live numbers from the engine.

## Text safety
Text from a friend, and anything pasted, passes through one filter (`src/textsafe.cpp`) before it is shown. It replaces with `?`:
- invisible control characters (everything below space except tab, DEL, and the C1 range);
- characters that silently flip or reorder the direction of text (used to disguise what a message or file name really says);
- line and paragraph separators, so one message cannot fake several;
- the byte-order mark and the interlinear-annotation characters;
- broken or disguised UTF-8.
Ordinary text in any language, accents and emoji (including joined emoji) are kept as they are.

## Not in the first version
Group chat, file sharing, video, settings screens beyond the basics, the Linux backend. (Servers and hubs are not planned: StarCom is direct messages only.)

Only the four StarCom colors are used: magenta, blue, black and white (with greys between black and white). No green, amber or red.
