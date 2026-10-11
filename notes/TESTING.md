# Testing StarCom (early prototype)

StarCom is built with AI, openly (see README.md). This early prototype does direct messages and voice calls between friends. **Nobody in a conversation can see anybody's IP address:** everything goes through relays, encrypted end to end.

## What you get
- `starcom.exe` (Windows) or `starcom` (Linux): the whole program in one file, nothing to install. The copy you get from whoever invited you already knows how to join the StarCom network.
- StarCom keeps its own files in its data folder: `%LOCALAPPDATA%\StarCom` on Windows, `~/.local/share/starcom` on Linux. Nothing is written next to the program.
  - Your identity (`identity.key`).
  - Your @name (`name.txt`).
  - Your friends list (`friends.txt`).

## The window (Windows)
1. Double-click `starcom.exe`.
2. Choose your @name and click Save. Tell your friends your @name.
3. Click the + next to Direct Messages, type your friend's @name and click Find.
   - StarCom shows their friend code. If you want to be sure it is really them, compare it with them.
   - Click Add.
4. Click your friend's name and start typing. They get your messages whenever they have StarCom open. You do not both need to open the chat first.
5. To call, click the phone at the top of the chat. Your friend clicks Accept. Use headphones: there is no echo cancellation yet.
   - The call panel has Mute, Deafen and Disconnect.
6. The gear at the bottom left opens Settings: your @name, your friend code, and where StarCom keeps its files.
7. **Hubs** (group spaces):
   - Click the + on the left ("Add Server"), type a name and click Create. You are the hub's owner.
   - Click "..." next to the hub's name, then Invite people, then Copy. Send the code to your friend privately. Keep StarCom open until they have joined.
   - Your friend clicks the + on the left, pastes the code and clicks Join.
   - Click a # channel to talk. Click a voice channel to join it; DISCONNECT in the voice panel leaves it.
   - Hub settings (in the "..." menu) adds channels, makes admins, kicks and bans.

## Helping the network (optional)
- In the window: Settings (the gear), then "Help the network", then On. It is off unless you turn it on.
- On the command line: `starcom volunteer on` or `off`; `starcom volunteer` shows the setting; `starcom volunteer run` helps right now and shows what it carries every 10 seconds (type `/quit` to stop).
- A volunteer carries only other people's encrypted packets between StarCom's entry relays, up to 2 Mbps. Other users never learn a volunteer's IP address, and a volunteer never learns theirs.

## Running a relay for others (optional, command line)
- `starcom volunteer relay 47010` runs a relay on UDP port 47010 that other people can use. It tells you plainly that your IP address becomes public. It prints `listed by 1 of 1 entry points` once an entry relay has checked that the port answers from outside; `listed by 0` means nobody can find it yet (usually a closed port or a router that needs a port forward).
- Its key is kept in `peer-relay.key`, separate from your identity.
- StarCom copies that start afterwards learn about it from their relay, save it in `relays.txt` and use it the next time they start. `tools/guards-test.sh` checks that relays already saved stay in front of newly listed ones and that a dead relay is only dropped on its third failed start.

## Command line (Windows and Linux)
- Choose your @name once:

      starcom name river

- Chat with a friend by their @name:

      starcom chat @skye

  The first time, StarCom shows your friend's code so you can compare it with them, then remembers it.
- While chatting, type:
  - `/call` to ring your friend;
  - `/accept` to answer a call, `/hangup` to end or decline one;
  - `/mute` and `/deafen`;
  - `/quit` to leave.
- `starcom call @skye` does the same as `chat`, and rings straight away.

## Hubs from the command line (Windows and Linux)
- Open your hubs:

      starcom hub

- Make a hub and invite people:
  - `/create Our Hub` creates it; you are its owner.
  - `/invite` prints an invite code (it works for 24 hours and for 10 people). Send it privately.
  - Stay online while people join with it.
- Join a hub: `/join SCH1-...@name` (paste the whole code).
- Talk: anything you type goes to the current channel.
  - `/hubs` lists your hubs and channels.
  - `/use 2` switches hub.
  - `/ch random` switches channel.
  - `/who` shows who is online.
- Voice: `/vc` joins the voice channel (use headphones), then `/mute`, `/deafen` and `/novc`.
- Owner and admins: `/kick @name`, `/ban @name`. Owner only: `/admin @name`, `/unadmin @name`, `/addch name` (add ` voice` for a voice channel), `/bans` to list bans and `/unban <number>` to lift one.
- `/leave` leaves the current hub; `/quit` closes StarCom. `/help` lists everything.

## Cleaning a picture (Windows and Linux)
- `starcom scrub photo.jpg clean.jpg` writes a copy of a JPEG, PNG, GIF or WebP picture without its EXIF, GPS, text and other hidden data, and lists what it removed. The original is not changed, and the new file must not exist yet.
- Any other kind of file is refused. Details are in SCRUB.md.
- Developers can check it with `python tools/scrub-test.py build/starcom scrub`.

## Developer checks (Linux)
- `python tools/scrub-test.py build/starcom scrub` checks the picture scrubber (SCRUB.md).
- `sh tools/smoke-test.sh build/starcom` starts a relay on the loopback address and runs two StarCom copies against it: a chat in both directions, then a hub (create, invite, join, messages both ways, roles), and checks that neither user is shown an address, that a banned member is removed and cannot join again with the same invite, that a second person claiming an @name that is in use is told so (and the first owner is not), and that the relay writes nothing. It takes about 35 seconds and touches only a temporary folder that it deletes.
- `SMOKE_VOICE=1 sh tools/smoke-test.sh build/starcom` also joins a hub voice channel before the member is banned. It is off by default because on a machine with a microphone it really opens the microphone for a few seconds.
- `sh tools/peer-relay-test.sh build/starcom` starts an entry relay and two volunteer relays on the loopback address, checks that both are verified and listed, that two clients learn them in one run and use them in the next, and (when it may watch the loopback interface) that the traffic goes to the volunteers, and that at most 4 relays from one network are listed (about three minutes). Same single-entry-point binary as the smoke test.
- `sh tools/relay-failover-test.sh build/starcom` starts a client on two volunteer relays, stops both, and checks that it notices and comes back online through another relay by itself (about two and a half minutes). Same binary as the smoke test.
- `sh tools/admin-test.sh build/starcom` runs three StarCom copies against a loopback relay (an owner, an admin and a member) and checks that the owner can make an admin, that an admin cannot remove the owner, and that a member kicked by an admin is told and disappears from the owner's list (about a minute). Same single-entry-point binary as the smoke test.
- `sh tools/two-relay-test.sh build/starcom` runs two relays and two StarCom copies, checks that a chat works across the two relays, then closes the chat and checks that the idle link between the relays stays quiet, and that six new users are never wrongly told their @name is taken (it takes about two minutes). It needs a binary built with two entry points: put `127.0.0.1:47000` and `127.0.0.1:47001` on separate lines in `.private/seeds.txt`.
- `sh tools/bridge-restart-test.sh build/starcom` stops a volunteer and starts another straight away, and checks that the relay first refuses it and then accepts it again within 150 seconds (it takes about three minutes). Same single-entry-point binary as the smoke test.
- The binary must have been built with the loopback address as its only entry point: put `127.0.0.1:47000` in `.private/seeds.txt` (the folder is never committed) and run `sh build.sh`. Use a separate checkout for this, so your normal build keeps its real entry points.

On Linux, run `chmod +x starcom` once, then use `./starcom` in place of `starcom`.

## Who sees what
- **Your friend never receives your IP address,** and you never receive theirs. Everything goes through relays.
- The relay you are linked to must see your IP address, because it has to send packets back to you. It never sees what you say, stores nothing about you on disk, and writes no logs.
- Messages and calls are encrypted end to end and never stored anywhere. When you close StarCom, the conversation is gone.

Full details are in NETWORK.md and DATA.md.

## If it does not connect
- Check the spelling of your friend's @name.
- Your friend needs StarCom open (the window, or `starcom chat`).
- If StarCom says it cannot reach the network, check your internet connection.
- Some firewalls ask the first time whether to allow `starcom.exe`: allow it on private networks.
