# Network connections

Every network connection StarCom makes is listed here: to whom, when, why, and what is sent. If a connection is not on this page, StarCom does not make it.

**The rule behind all of it:** no StarCom user ever receives another user's IP address. Every conversation, including a two-person chat, goes through relays. What travels between people is encrypted end to end, and the relays cannot read it.

| Connection | To whom | When | Why | What is sent |
|---|---|---|---|---|
| DNS lookup of the entry points (only if the entry points built into this copy are names, not numbers) | Your own DNS resolver (usually your internet provider or router) | When StarCom starts | To find the network's entry points (SPEC section 3, "DNS Seeding") | The entry-point names built into this copy of StarCom. Your resolver learns that you looked them up. |
| Relay key request | Your relay (an entry point) | Once, when StarCom first links to it | To learn the relay's public key, so the link to it can be encrypted | A 64-byte request holding nothing but "which key are you?". The relay answers with its public key. |
| Your link to your relay | Your relay | The whole time StarCom is open. A tiny "still here" packet every 25 seconds keeps your router's connection open. | Everything you do goes through this one encrypted link | An encrypted session (Noise IK, ChaCha20-Poly1305) proven with your identity key. Inside it: your signed @name record, @name lookups, and the encrypted packets of your conversations. |
| Your @name record | Star DNS (the StarCom network), through your relay | When StarCom starts, every 30 minutes, and when your relay changes | So friends can find you by your @name | A record signed with your key: your @name, your public key, your relay's address and key, and an expiry time. **It holds no IP address of yours.** The relay stores it in Star DNS on your behalf, so no Star DNS node ever sees your IP next to your @name. |
| @name lookups | Star DNS, through your relay | When you add a friend, or open a chat with one | To find which relay can reach your friend | The @name you are looking for (inside your encrypted link). Your relay searches Star DNS for you. You check the answer's signature yourself. |
| Conversations (chat and voice) | Your friend, through your relay and your friend's relay | While a chat or call is open | To carry messages and voice | Packets encrypted end to end between you and your friend (Noise IK, ChaCha20-Poly1305). Each relay passes them on without being able to read them. |

## Who learns what, plainly
- **Your friend never learns your IP address,** and you never learn theirs. Each of you sees only your own relay. Your friend learns your @name and your friend code (your public key), as they must in order to know it is you.
- **Your relay** necessarily sees your IP address: it has to know where to send your packets back. It also sees which key your conversations go to, when, and how much. It never sees the content. It keeps this only in memory while your link lasts, writes no logs, and never passes your address on to anyone.
- **Your friend's relay** sees your relay's address, never yours.
- **When you and your friend use the same relay** (today, while the network has one relay), that one relay sees both of your addresses. Planned: as more relays join, StarCom will prefer a relay different from your friend's, so that no single relay sees both ends.
- **Star DNS nodes** see the address of the relay that stores or looks up a record, never yours.
- **Anyone watching your internet connection** sees encrypted packets going to your relay, and nothing else.

StarCom makes no other connections: no update checks, no telemetry, no analytics.
