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
