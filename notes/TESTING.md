# Testing StarCom (early prototype)

StarCom is built with AI, openly (see README.md). This is a very early command-line prototype for testing text chat between two friends. It has no window yet.

## What you get
- `starcom.exe`: the program (Windows, about 450 KB, nothing to install).
- `starcom.conf`: a small settings file with the address of a meeting-point server. Whoever invited you gives you this file privately. StarCom has no built-in servers.

Put both in the same folder.

## 1. Get your friend code
Open a Command Prompt in that folder and run:

    starcom id

The first time, this creates `identity.key` in the folder: that file IS your identity.
- Keep it private and back it up.
- Deleting it means your friend code is gone for good.

The command prints your friend code (it starts with `SC1-`). Send it to the friend you want to test with, and get theirs.

## 2. Chat
Both of you run, at roughly the same time (within 2 minutes of each other):

    starcom chat <your friend's code>

Type a message and press Enter. Type `/quit` to leave.

## What happens, and who sees what
1. Your program asks the meeting point what your public address is, and leaves an encrypted note that only your friend can read.
2. When both notes are found, the two computers connect directly. **In a direct connection your friend can see your public IP address.**
3. If a direct connection is impossible (some routers block it), traffic goes through the relay instead. Your friend then does not see your IP; the relay sees both IPs but cannot read anything.
4. Messages are encrypted end to end and are never stored anywhere. When you close the program, the conversation is gone.

Full details are in NETWORK.md and DATA.md.

## Hide my IP
Add this line to `starcom.conf`:

    hideip=1

StarCom then never connects directly: your note carries no addresses of yours, and everything goes through the relay. Your friend cannot see your IP address. The relay still sees both addresses, never the content. It can be a little slower.

## Other commands
- `starcom probe`: shows which video encoders your hardware has (for future video calls).
- `starcom id`: shows your friend code again.

## If it does not connect
- Check that both of you typed the other's code exactly.
- Check that both started within 2 minutes of each other.
- Check that `starcom.conf` is in the same folder as `starcom.exe`.
- Some firewalls ask the first time whether to allow `starcom.exe`: allow it on private networks.
