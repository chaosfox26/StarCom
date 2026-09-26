# Network connections

Every network connection StarCom makes is listed here: to whom, when, why, and what is sent. If a connection is not on this page, StarCom does not make it.

| Connection | To whom | When | Why | What is sent |
|---|---|---|---|---|
| DNS lookup of the seed name | Your own DNS resolver (usually your internet provider or router) | When you run `seed` or `connect seed` | To find a network entry point | The seed name. Your resolver learns that you looked it up. |
| UDP handshake | The peer address you give (or that the seed returned) | When you run `connect`, or when a peer connects to your `listen` port | To set up an encrypted session | Encrypted handshake packets: your public key and codec capability list, readable only by that peer |

What the other side learns: a peer you handshake with sees your public IP address. Anyone watching the network sees two IPs exchanging encrypted packets, but not what is inside.

| Chat | The peer you completed the handshake with | While the chat is open | To carry your messages | Encrypted packets: your messages, acknowledgements, and a small keepalive about every 25 seconds so home routers keep the connection open. If the peer is silent for 75 seconds the chat ends. |

| Meeting point (rendezvous) | The meeting-point server named in your starcom.conf (StarCom ships with none built in) | When you run `chat` with a friend code | To learn your own public address and to leave and collect "how to reach me" notes for one friend | A lookup request, and a note encrypted so only that friend can read it. The note holds your public and local network addresses and a timestamp. The server stores notes in memory only, for at most 10 minutes, and keeps no logs. |
| Direct connection to a friend | Your friend's addresses from their note | Right after both notes are found | To connect without any server in between | First a few small "hole punching" packets, then the encrypted handshake and chat |
| Relay (only if a direct connection fails) | The same server as the meeting point | If a direct connection has not worked within about 10 seconds | Some home routers block direct connections | Your already-encrypted handshake and chat packets, which the relay forwards unread |

**Who learns what, plainly:**
- **The meeting-point server sees your IP address.** If it sees you and a friend use related mailboxes in the same hour, it could guess the two of you are talking. It cannot read notes, keys, names or messages. Mailbox names change every hour.
- **A direct connection means your friend sees your public IP address.** From an IP you can usually tell a rough city and internet provider.
- **Through the relay, your friend does not see your IP.** The relay sees both IP addresses, when you talk and how much, but never what you say.

StarCom makes no other connections: no update checks, no telemetry, no analytics.
