# Data

Every piece of data StarCom stores or sends is listed here: what it is, where it lives, how long it is kept, and whether it is encrypted.

StarCom collects nothing about its users. There are no accounts, no analytics and no tracking. Nothing is ever sent to the people who make StarCom.

## Stored on your device

| Data | File | Kept | Encrypted |
|---|---|---|---|
| Your identity: a 32-byte secret seed and your 32-byte public key | `identity.key`, in the StarCom data folder: `%LOCALAPPDATA%\StarCom` on Windows (the local, non-roaming folder, so it is never copied to a school or company server), `~/.local/share/starcom` on Linux | Until you delete it. Deleting it deletes your identity for good. | **Not yet.** In this early version the file is protected only by your operating system account. Passphrase encryption is planned. Keep the file private and back it up. |
| Your settings, such as a different meeting point | `starcom.conf` in the StarCom data folder, only if you create it | Until you delete it | No (it holds no secrets) |
| Your friends list: each friend's code and the nickname you gave them | `friends.txt`, in the StarCom data folder | Until you delete it | No. Nicknames are only on your computer and are never sent to anyone. |

## Sent over the network

| Data | Sent to | When | Encrypted |
|---|---|---|---|
| Your public key and your encoder capability list (which video codecs your hardware can encode) | The one peer you connect to | During the handshake | Yes, end to end (Noise protocol) |

Note on the capability list: it includes the names of your video encoders (for example the brand of your graphics card). That is a hardware fingerprint. Only the peer you connect to can read it; later versions will send only the codec tiers, without names, to anyone who is not on your friend list.

Your secret key never leaves your device.

## Chat messages
- Messages you type are sent only to the one peer you are chatting with, encrypted end to end inside the session (ChaCha20-Poly1305). No server sees or relays them in this version.
- **Messages are never stored.** Not on your device, not on the peer's device, not anywhere. There is no chat history file; when the program closes, the conversation is gone.
- Each message carries a sequence number so it arrives exactly once and in order; the numbers are inside the encryption.

## Voice calls
- Your microphone is opened only after you start `call` with a friend's code and the two of you connect. It closes when the call ends.
- While muted, your microphone's sound is replaced by silence before encoding, so nothing you say is sent. The microphone itself stays open (Windows shows it as in use) until the call ends.
- Your voice is compressed with Opus and encrypted end to end inside the session, like chat.
- **Audio is never stored or recorded** anywhere: not on your device, not on your friend's, not on any server or relay.
- Packets carry a sequence number and a timestamp (inside the encryption) so the other side can play them in order.

## The StarCom window
- While the window is open, your identity key stays in memory so StarCom can connect to friends you click. It is wiped from memory when the window closes.
- Chat messages shown in the window exist only in memory and are gone when you close it.
