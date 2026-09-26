# Identity and discovery

How a StarCom user gets an identity, how two users recognise each other, and how two computers behind home routers find and reach each other without a central server. Each section ends with **who learns what**, because users are told exactly that.

Status: design draft. No code yet.

## 1. Goals (from RULES.md)
- No accounts, no login, no email, no phone number.
- Nothing central stores users, messages, contacts or metadata.
- Everything is encrypted end to end.
- Whatever cannot be hidden is said plainly.

## 2. Identity
- On first start, the app generates one **Ed25519 key pair** on the device.
  - The private key never leaves the device, except when the user explicitly exports a backup.
  - The public key **is** the user. There is no username registry.
- The same key, converted to its **X25519** form, is used for key agreement.
  - This is the standard, well-studied Ed25519-to-X25519 conversion.
  - One key per user means one thing to back up and one thing to verify.
- **Display name:** free text chosen by the user. It is only a label; two users may pick the same name. The key is what counts.
- **Key storage:** in a local file, encrypted with a passphrase if the user sets one. With no passphrase the file is protected only by the operating system account.
- **Losing the device means losing the identity**, unless a backup was exported. We say this at first start.
- Multiple devices per user: later. Planned model: the main key signs a certificate for each device key.

**Who learns what:** nobody. Creating an identity sends nothing anywhere.

## 3. Friend codes
- **Friend code** = the public key (32 bytes) plus a 2-byte checksum, in base32 with dashes. About 55 characters, for example `SC1-ABCD-EFGH-...`.
- It is shared outside the app: copy/paste, a QR code, or a `starcom://add/<code>` link.
- It is long on purpose. A short code would need a lookup service, and that service would learn who adds whom.
- **Short pairing code (later):** two people type the same short phrase (e.g. `7-orbit-velvet`) and a PAKE exchange (CPace) swaps their full codes through the rendezvous (section 5). The rendezvous sees only random-looking bytes and cannot guess the phrase offline.
- **Verification:** each pair of friends can compare a **safety number** (a hash of both public keys, shown as 12 groups of digits or as a QR code) in person or over a call. This catches a code that was swapped in transit.

**Who learns what:** whoever carries the code (a chat, an email, a paste site) learns your public key. A public key alone reveals no name, address or IP.

## 4. Handshake
- **Noise protocol, pattern IK:** `Noise_IK_25519_ChaChaPoly_BLAKE2b`.
  - Adding a friend means already knowing their static key, so the first packet is encrypted to them.
  - It hides the initiator's identity from anyone watching the network.
  - Both sides are authenticated. Fresh ephemeral keys give **forward secrecy** per session.
- **Fallback:** `Noise_XX` for the pairing-code flow, where the key is not known yet.
- **Unknown keys:** a key that is not in your friend list gets no reply beyond the Noise failure, so a stranger cannot probe whether you are online.
- **Messages that must survive going offline** (section 7) are additionally encrypted per message with a ratchet, so one leaked session key does not open old messages. This is a later milestone.

**Who learns what:** a network observer sees two IPs exchanging encrypted packets of certain sizes at certain times. They learn no keys, names or content.

## 5. Finding each other: rendezvous mailboxes
Two home computers usually cannot connect directly: both are behind routers, and their public IPs change. They need somewhere to leave "here is how to reach me" notes. That place must not become a user database.

**Mailbox ids.** Each pair of friends shares a secret nobody else can compute: the X25519 agreement between their two identity keys. From it, each derives a **mailbox id** that changes every hour:

    mailbox = BLAKE2b(shared_secret, "starcom-mbox" || hour_number)

- Only the two friends can compute it. Different pairs have unrelated ids, and one hour's id is unlinkable to the next hour's.
- **Clock skew:** a client puts to the current hour's mailbox, and gets from the previous, current and next hour's mailboxes. Clocks that are up to an hour apart still meet.

**Contact note.** When Alice comes online, she writes a note to each friend's current mailbox:
- the note holds her connection candidates (local address, public address seen by the rendezvous, a relay address if any) and a timestamp;
- it is sealed with an AEAD cipher (ChaCha20-Poly1305), using a key derived from the shared secret, so it is both secret and authenticated: nobody else can forge or change a note;
- it carries a counter and a timestamp; a note older than the newest one already seen, or more than 15 minutes old, is ignored, so a replayed old note cannot redirect a friend to a stale address;
- the rendezvous sees only the mailbox id and random-looking bytes.

**The rendezvous server** is a small, open-source, stateless program anyone can run.
- **RAM only:** notes expire after at most 10 minutes. There is no disk, no database and no logs.
- It answers two requests: put(mailbox, blob) and get(mailbox).
- It also tells a client which public IP and port it sees (the STUN function) so clients can learn their own public address.
- **The prototype ships with no built-in servers.** The server list is supplied by the user: a local config file that is never part of the repo, or an "add a server" screen. Testers receive a server address privately. No server address is ever written into the source, the binaries or these notes.
- A client uses 2 or 3 servers at once, so one being down does not matter.

**Who learns what: the honest part.**
- The rendezvous **cannot read** notes, and **does not know** which keys or names are behind a mailbox.
- It **does see the IP address** of every client that talks to it. That cannot be avoided when talking over the internet.
- If one rendezvous sees IP A put to mailbox M and IP B get from M, it can infer that **A and B are probably friends**, until the id rotates.
  - Hourly rotation stops it building a long-term friend graph under one label, but it could still link IPs hour by hour.
  - A server that promises "no logs" could still be modified to log. Running your own, or spreading requests across servers, reduces this exposure; it does not remove it.
- **Mitigation, later:** clients can pad and batch, and poll a few decoy mailboxes, so a single put/get pair is less telling. A user who wants to hide even this can route rendezvous traffic through Tor (a setting, off by default, documented).
- **Rejected alternative, a public DHT** (as BitTorrent uses): there is no operator to trust, but every lookup reveals your IP to random strangers, and the friend graph leaks to whoever runs many nodes. That is more exposure than a few known servers, not less.

## 6. Connecting: hole punching, then relay
**Direct first (UDP hole punching).**
1. Both sides read each other's candidates from the mailbox notes.
2. Both send packets to each other's candidates at the same time. Most home routers then let the traffic through.
3. The first candidate that completes the Noise handshake wins.
- This works for most home connections. It fails when both sides are behind strict ("symmetric") NAT, which is common on mobile carriers and some ISPs.

**Relay fallback.**
- If a direct connection does not work within about 5 seconds, both sides connect to a **relay** and it forwards the already-encrypted Noise packets.
- A relay is the same kind of small open-source program as the rendezvous, and it can be the same process.
- It stores nothing. It sees traffic volume and timing, never content.

**Who learns what:**
- **Direct connection: your friend learns your public IP address.** This is the biggest privacy difference from services that route everything through their own servers, and it is stated plainly in the app and in NETWORK.md. From an IP you can usually tell someone's rough city and ISP.
- **Relay connection:** your friend sees only the relay's IP. The relay sees both IPs, when you talk and how much.
- **Setting "Hide my IP from friends":** always use a relay and never connect directly. The cost is a little more latency, and the relay operator learns both IPs.
- **Setting "Never use relays":** direct only. Some pairs of friends will then be unable to connect, and the app says so.

**If the app is built on a browser engine (WebRTC).** The browser's own connection machinery (ICE for hole punching and TURN relays, DTLS-SRTP for encrypted media) replaces the hand-rolled hole punching and the Noise transport for media above. Noise IK still authenticates the two friends through the mailbox notes. The WebRTC connection offers (SDP), **including the DTLS certificate fingerprints**, travel only inside those authenticated notes, and each side refuses a connection whose fingerprint does not match. Otherwise a relay or rendezvous could swap the offers and read the media. The who-learns-what list above stays the same: a TURN relay learns what a relay learns.

## 7. Being offline
- No server stores messages, so a message reaches a friend only while both devices are online, **or** if someone both trust holds it.
- **Prototype:** messages wait in the sender's outbox and deliver when both are online. This is honest and simple.
- **Later, store-and-forward:** in a group, members who are online can hold encrypted messages for members who are not (padded, ratchet-encrypted, deleted after delivery or after N days). The holders learn that a message of some size exists for someone in the group, not what it says.
- This is the "persistent files without a server" problem. It needs Chaos's specs on group size and history before it is designed.

## 8. Local network discovery (optional, off by default)
- Devices on the same Wi-Fi can find each other by broadcast and skip the internet.
- It is off by default because broadcasting "a StarCom user is here" tells everyone on that network.

## 9. What is in the prototype
| Piece | Prototype | Later |
|---|---|---|
| Ed25519 identity, passphrase-encrypted key file | yes | multi-device certificates |
| Friend code + QR + safety number | yes | PAKE short pairing codes |
| Noise IK / XX handshake | yes | per-message ratchet for offline delivery |
| Hourly mailbox ids on 2-3 rendezvous servers | yes | decoys, padding, Tor option |
| UDP hole punching | yes | TCP fallback for UDP-blocked networks |
| Relay fallback + "Hide my IP" setting | yes | community relay list |
| Offline delivery | sender outbox only | group store-and-forward |
| LAN discovery | no | opt-in |

## 10. Components this needs (for COMPONENTS.md)
- A small, audited crypto library for X25519, Ed25519, ChaCha20-Poly1305 and BLAKE2b. The candidate is **Monocypher**: one C file, public domain, independently audited. It is named here per rule five, and the final choice is made with the stack.
- No other dependency for this layer.

## 11. Open questions for Chaos
1. Is it acceptable that friends connected directly can see each other's IP, with "Hide my IP" as an opt-in setting? Or should relaying be the default?
2. Who runs the first rendezvous/relay servers for the friend test: one small machine we run, or should each tester be able to run one? Either way the address is handed to testers privately and never appears in the repo.
3. Offline messages in the prototype: is "delivered when both are online" enough for the first test?
