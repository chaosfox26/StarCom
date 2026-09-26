# Network connections

Every network connection StarCom makes is listed here: to whom, when, why, and what is sent. If a connection is not on this page, StarCom does not make it.

| Connection | To whom | When | Why | What is sent |
|---|---|---|---|---|
| DNS lookup of the seed name | Your own DNS resolver (usually your internet provider or router) | When you run `seed` or `connect seed` | To find a network entry point | The seed name. Your resolver learns that you looked it up. |
| UDP handshake | The peer address you give (or that the seed returned) | When you run `connect`, or when a peer connects to your `listen` port | To set up an encrypted session | Encrypted handshake packets: your public key and codec capability list, readable only by that peer |

What the other side learns: a peer you handshake with sees your public IP address. Anyone watching the network sees two IPs exchanging encrypted packets, but not what is inside.

| Chat | The peer you completed the handshake with | While the chat is open | To carry your messages | Encrypted packets: your messages, acknowledgements, and a small keepalive about every 25 seconds so home routers keep the connection open. If the peer is silent for 75 seconds the chat ends. |

StarCom makes no other connections: no update checks, no telemetry, no analytics.
