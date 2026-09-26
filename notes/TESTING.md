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
