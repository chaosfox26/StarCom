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

## 5. Finding each other: Star DNS and @names (SPEC section 3)
- Every user chooses an @name (3 to 32 characters: a-z, 0-9, '_' and '-').
- Friends find each other through Star DNS, a Kademlia-style network (SPEC section 3), entered through the entry points built into each copy of StarCom (SPEC section 3, "DNS Seeding").
- What Star DNS stores for an @name is a record signed with its owner's key:
  - the @name;
  - the owner's public key;
  - the owner's relay (its address and key);
  - an expiry time of at most one hour.
  **It holds no IP address of the user.**
- Records are published and looked up by the user's relay on the user's behalf, so no Star DNS node ever sees a user's IP next to an @name.
- Names are first come, first served. A record can be replaced only by a newer one from the same key.
- The friends list remembers each friend's key the first time you add them (you can compare friend codes then), so nobody can later take over a friend's @name in your list.

## 6. Connecting: always through relays (the owner's order; it overrides SPEC section 5's "Direct Path")

Update: the owner has since decided that friends connect directly by default and that relays may be run by ordinary users. This section describes what the prototype does today; the plan is in PEER-RELAYS.md.

- No two users ever connect directly, and no user ever receives another user's IP address, in any form, even encrypted.
- Each user keeps one encrypted link (Noise IK, proven with their identity key) to a relay.
- A conversation is a circuit: your relay, then your friend's relay (when it is a different one), then your friend.
- The two people then run their own end-to-end Noise IK session inside the circuit, so the relays pass along packets they cannot read.
- **Who learns what:**
  - your friend: never your IP;
  - your relay: your IP (it must, to send packets back), the key you talk to, when and how much; never the content;
  - your friend's relay: your relay's address, never yours.
  - One relay today: see KNOWN-ISSUES.
- A relay stores nothing about users on disk and writes no logs.

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
| Star DNS @names, published through your relay | yes | many volunteer relays |
| Masked path through relays (no direct connections) | yes | two different relays per conversation by default |
| Offline delivery | sender outbox only | group store-and-forward |
| LAN discovery | no | opt-in |

## 10. Components this needs (for COMPONENTS.md)
- A small, audited crypto library for X25519, Ed25519, ChaCha20-Poly1305 and BLAKE2b. The candidate is **Monocypher**: one C file, public domain, independently audited. It is named here per rule five, and the final choice is made with the stack.
- No other dependency for this layer.

## 11. Open questions for Chaos
1. Is it acceptable that friends connected directly can see each other's IP, with "Hide my IP" as an opt-in setting? Or should relaying be the default?
2. Who runs the first rendezvous/relay servers for the friend test: one small machine we run, or should each tester be able to run one? Either way the address is handed to testers privately and never appears in the repo.
3. Offline messages in the prototype: is "delivered when both are online" enough for the first test?
