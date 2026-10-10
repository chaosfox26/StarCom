# Data

Every piece of data StarCom stores or sends is listed here: what it is, where it lives, how long it is kept, and whether it is encrypted.

StarCom collects nothing about its users. There are no accounts, no analytics and no tracking. Nothing is ever sent to the people who make StarCom.

## Stored on your device

| Data | File | Kept | Encrypted |
|---|---|---|---|
| Your identity: a 32-byte secret seed and your 32-byte public key | `identity.key`, in the StarCom data folder: `%LOCALAPPDATA%\StarCom` on Windows (the local, non-roaming folder, so it is never copied to a school or company server), `~/.local/share/starcom` on Linux | Until you delete it. Deleting it deletes your identity for good. | **Not yet.** In this early version the file is protected only by your operating system account. Passphrase encryption is planned. Keep the file private and back it up. |
| Your @name (how friends find you) | `name.txt`, in the StarCom data folder | Until you change or delete it | No. Your @name is public by design: it is how friends find you. |
| Your friends list: each friend's code, the nickname you gave them, their @name, and "+met" once you have had a conversation with them (after that, your conversations with them skip volunteer bridges) | `friends.txt`, in the StarCom data folder | Until you delete it | No. Nicknames are only on your computer and are never sent to anyone. |
| Each hub you are in: its signed layout (name, channels, members with their @names, public keys and roles, and the ban list) | `hubs/<hub id>.manifest`, in the StarCom data folder | Until you leave the hub | No: it is signed by the hub's owner, not secret. It holds no IP addresses. |
| Each hub's signed join, kick, ban, unban and leave records | `hubs/<hub id>.records` | Until the owner folds them into the layout, or you leave the hub | No: signed, not secret |
| Each hub's channel keys, each with the signature of the owner or admin who made it | `hubs/<hub id>.keys` | Replaced every hour by a one-way hash (the old key is overwritten), replaced on every kick or ban and about every 30 days, deleted when you leave | **Not yet**, like `identity.key`: protected only by your operating system account. Keep the data folder private. |
| The invites you made | `hubs/invites` | Until each invite expires or is used up | Only a hash of each invite's token is stored, never the token itself |
| Volunteer relays you can use: up to 16 addresses (IP and port), most recently learned first | `relays.txt`, in the StarCom data folder | Until you delete it. It is replaced by newer lists from your relay. | No: addresses of public relays that their owners chose to publish |
| The key of a relay you run with `starcom volunteer relay` | `peer-relay.key`, in the StarCom data folder | Until you delete it | **Not yet**, like `identity.key`. It is a different key from your identity, so it cannot be tied to your @name. |
| Whether you help the network (on or off) | `volunteer.txt` | Until you change it | No: it holds only "on" or "off" |

## Sent over the network

| Data | Sent to | When | Encrypted |
|---|---|---|---|
| Your public key and your encoder capability list (which video codecs your hardware can encode) | The friend you connect to, through the relays | During the handshake | Yes, end to end (Noise protocol). The relays cannot read it. |
| Your signed @name record: your @name, your public key, your relay's address and key, an expiry time | Your relay, which stores it in Star DNS | When StarCom starts and every 30 minutes | The record is signed, not secret: it is how friends find you. **It contains no IP address of yours.** It travels inside your encrypted link to the relay. |
| The @names you look up | Your relay, which searches Star DNS for you | When you add or open a friend | Inside your encrypted link to the relay |
| Your @name, as a hint | The person you contact, inside the encrypted handshake | When you open a conversation | Yes, end to end. If they have not added you yet, their StarCom shows "@you wants to talk" and only trusts it after checking your public @name record matches your key. |
| Hub messages: your key, the time, the channel, the encrypted text and your signature | The hub's members who are online, passed along the hub's mesh through the relays | When you post | Yes: encrypted with the channel's key, inside each pair's end-to-end session. Relays cannot read them. |
| Hub presence: online or leaving, and which voice channel you are in | The hub's members | When it changes, and every 5 minutes | Signed, inside the end-to-end sessions |
| Joining a hub: the invite's token and your @name | The member who made the invite | When you join | Yes, end to end. Your @name is checked against your key in Star DNS before you are admitted. |
| A hub's layout, records and channel keys | The members you are connected to in that hub | When you connect to them, and after every change | Yes, end to end. Channel keys go only to current members. |
| Only if you help the network: a random bridge key (new on every start), your upload limit, and which entry relays you are linked to | The StarCom entry relays | When StarCom starts with the setting on | Inside the encrypted link. The bridge key is never your identity key, so a relay cannot tie your help to your @name. |
| **Your IP address** | **Never sent to any other user,** including hub members and volunteers. Only your relay sees it, because packets come from it. If you help the network, the entry relays also see your address for the bridge link. | | |

Note on the capability list: it includes the names of your video encoders (for example the brand of your graphics card). That is a hardware fingerprint. Only the peer you connect to can read it; later versions will send only the codec tiers, without names, to anyone who is not on your friend list.

Your secret key never leaves your device.

## Chat messages
- Messages you type are sent only to the one friend you are chatting with, encrypted end to end inside the session (ChaCha20-Poly1305). They travel through the relays, which cannot read them.
- **Messages are never stored.** Not on your device, not on the peer's device, not anywhere. There is no chat history file; when the program closes, the conversation is gone.
- Each message carries a sequence number so it arrives exactly once and in order; the numbers are inside the encryption.

## Hub messages
- A hub message goes to every member of the hub who is online, and to no one else. It is encrypted with the channel's key and signed by you, so members know you wrote it (see HUBS.md).
- **Hub messages are never stored.** There is no history file. A member who is offline when you post does not get the message.

## Voice calls
- Your microphone is opened only once a call is answered: after your friend accepts your call, or after you accept theirs. It closes when the call ends.
- While muted, your microphone's sound is replaced by silence before encoding, so nothing you say is sent. The microphone itself stays open (Windows shows it as in use) until the call ends.
- Your voice is compressed with Opus and encrypted end to end inside the session, like chat.
- **Audio is never stored or recorded** anywhere: not on your device, not on your friend's, not on any server or relay.
- Packets carry a sequence number and a timestamp (inside the encryption) so the other side can play them in order.
- **Hub voice channels** work the same way: your microphone opens only while you are in a voice channel, and your voice goes to each other person in that channel inside your end-to-end session with them. Nothing is recorded.

## Picture scrubber
- `starcom scrub <file> <out>` reads the one file you name and writes the one new file you name. It stores nothing else, sends nothing, and never changes the original. See SCRUB.md.

## The StarCom window
- While StarCom is open, your identity key stays in memory: it proves your link to your relay and signs your @name record. It is wiped from memory when StarCom closes.
- Chat messages shown in the window exist only in memory and are gone when you close it.
