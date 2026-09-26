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
- **Threads:** the window thread owns the interface. The network runs on its own threads:
  - the relay link;
  - one thread per connected person, which runs that pair's session and the direct-message chat and call on it;
  - one hub thread, which runs all hub logic;
  - an acceptor for incoming connections.
  They talk through small locked queues, so the interface never waits on the network.

## Layout

| Region | Width | Contents |
|---|---|---|
| Rail | 72 | The StarCom star at the top; Home (direct messages); one circle per hub (its first letter; a white bar marks the open one, a magenta dot means unread); "+ Add Server" to create or join a hub |
| Conversation column | 240 | Header "Direct Messages" with a "..." menu (Add a friend, Copy my @name); DIRECT MESSAGES with a +; one row per friend (initial, status dot, unread count, a phone mark while a call rings or runs); your panel at the bottom (@name, Online or Connecting..., the settings gear) |
| Chat | rest | Header: "@ nickname", then a status line, then icons (call, mute, deafen, notifications, network health). Messages grouped by sender with time stamps. Message box with + (files, coming soon), emoji picker and send. |
| Call panel | floats bottom right | "CALL", then Calling / Incoming (Accept, Decline) / In call with the timer. Mute, Deafen and DISCONNECT, plus "latency: N ms Ping". |
| Network Health | 240, toggled | Connection (through the relays), who sees your IP, encryption, latency (SPEC section 6) |

**Inside a hub** (following the reference picture):

| Region | Width | Contents |
|---|---|---|
| Hub column | 240 | The hub's name with a "..." menu (Invite people for the owner and admins; Hub settings, or Members for members; Leave hub, except for the owner). Then the text channels ("# name", unread count), then VOICE with the voice channels and, under each, who is in it. Your panel at the bottom. |
| Chat | rest | Header: "# channel", then "N Online", then icons (mute and deafen for the voice channel you are in, notifications, members). Messages grouped by author with their @name and time. Message box "Message #channel...". |
| Voice panel | floats bottom right | While you are in a voice channel: the channel's name, "N Active", the names, Mute, Deafen, DISCONNECT and "latency: N ms Ping". It stays visible when you switch to direct messages. |
| Members | 240, toggled | ONLINE and OFFLINE lists, with Owner and Admin marks |

**Hub screens:**
- **Add a hub** (the + on the rail): create one by name, or join with an invite code.
- **Invite people:** shows the code with Copy. It works for 24 hours and for up to 10 people, while you are online.
- **Hub settings:** the owner renames the hub and adds or removes text and voice channels. The owner and admins kick and ban; the owner also makes and removes admins, and sees a BANNED list (by friend code, or nickname for friends) with Unban. Kick, Ban and Remove ask "Sure?" before acting. Members see the member list.
- **Leave hub** asks first. The owner has no Leave button: an owner cannot leave their own hub yet.
- **Notes:** blue means done or in progress, magenta means something went wrong.

**Message requests:** when someone who is not in your friends list contacts you, the column shows them under MESSAGE REQUESTS ("@name?" and "not verified yet", with their friend code in the tooltip). The @name is only what they claim until you accept. A member of a hub you are in shows as "@name" and "in a hub with you": their @name was already checked when the hub connected you, so Accept adds them at once.
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
