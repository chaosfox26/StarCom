# Peer-run relays and hidden paths

Status: design draft. No code yet. Everything here is a plan; NETWORK.md and DATA.md describe what StarCom does today.

## 1. Decisions (from the owner)
1. **Peer-run relays are acceptable.** The aim is that no dedicated server is needed: ordinary StarCom users can carry other people's traffic.
2. **Who we hide from:** the person you are talking to, and bad actors (strangers, hub members, anyone probing the network, anyone trying to find an address to attack).
3. **Friends connect directly.**

Points 2 and 3 pull against each other: a direct connection shows the friend your IP address, and nothing can change that (see section 2). The plan below keeps all three by making it a choice per conversation:

| Conversation | Path | Who can learn your IP |
|---|---|---|
| A friend, default | Direct, peer to peer | That friend only |
| A friend you chose "Hide my IP" for | Hidden path through peer relays | Your first relay only |
| A message request, a stranger, any hub member | Hidden path | Your first relay only |

## 2. Why a direct connection cannot hide your address
The other side must send replies somewhere, and your packets carry where they came from. Forging the source address does not help: providers drop forged packets and replies would go to the forged address. So hiding an address needs something in the middle, and that is the whole reason for relays.

## 3. The hidden path
- **Relays are peers.** A StarCom that is reachable from the internet and has "Help the network" switched on can act as a relay. It is off by default, as today.
- **Layers.** You build the path one hop at a time. Each hop gets its own encrypted session (Noise, as now), so each relay knows only the hop before it and the hop after it. Inside all of that, you and your friend keep the end-to-end session you already have, so no relay reads anything.
- **Meeting in the middle.** Neither side learns the other's address, because both build their own path to a meeting relay chosen by one of them: you, your first relay, the meeting relay, your friend's first relay, your friend. The meeting relay sees only the addresses of the two first relays.
- **Hops.** One relay on each side by default, which keeps voice delay low. A "stronger" setting adds a second relay on each side.
- **Directory.** Relays publish a signed description (key, address, capacity, expiry) in Star DNS. A relay's address is public by its own choice, and anyone who volunteers accepts that.
- **First relays ("guards").** You keep using the same few first relays for weeks instead of picking new ones each time. Otherwise someone who runs many relays would, sooner or later, be picked as your first relay and learn your address. Keeping the same few limits that.
- **Bootstrapping.** A brand-new copy must contact some built-in entry point once, which sees its address once. This cannot be avoided. The dedicated entry relays shrink to this one job.

## 4. The direct path for friends
- **Finding each other's addresses.** You and your friend first connect over the hidden path. You then send each other your addresses *inside* the end-to-end encryption, so no relay learns them, and both send packets at once to open a way through home routers ("hole punching").
- **Switching.** If that works, the conversation moves to the direct path (SPEC section 3, connection migration). The hidden path stays as the fallback and for signalling, and falling back reveals nothing.
- **Only friends.** Addresses are exchanged only with keys on your friends list. A key you do not know gets no answer, so nobody can make your StarCom send them its address.
- **Choice.** "Hide my IP from this friend" keeps one friend on the hidden path. Adding a friend says plainly that friends connect directly and can see your IP.

## 5. Who learns what
- **Your friend, direct:** your IP. Hidden: never.
- **Your first relay:** your IP, which key you talk to is hidden from it behind the layers; it sees timing and volume.
- **Middle and meeting relays:** only neighbouring relays' addresses.
- **Anyone watching your connection:** encrypted packets to your first relay or to your friend, depending on the path.
- **Bad actors who run relays:** they see what any relay sees. If they control both your first relay and your friend's, they can match the two ends by timing. Guards make that rarer; they do not remove it.

## 6. Honest limits
- **Small network, small privacy.** With few volunteers, a "random" path is mostly the same few people, so the layers add little until there are many independent relays.
- **Timing.** Someone who can watch both ends can often match traffic by timing and size. Padding helps and costs bandwidth, which collides with the good neighbour rule (RULES.md rule 7). Not decided.
- **Reachability.** Most home connections cannot accept incoming connections, so relays will mostly be people with a public IPv6 address or a forwarded port. Everyone else only makes outgoing connections to relays, which works.
- **Hole punching** depends on the kind of router on each side and will fail for some pairs. Those pairs stay on the hidden path.
- **Voice delay** grows with every relay. It must be measured, not guessed.

## 7. Plan, in order
1. This document, and the owner's answers to section 8.
2. A StarCom that volunteers also accepts incoming relay links and publishes its description. Tested here with several copies on one machine.
3. Hop-by-hop path building with layered encryption and a meeting relay. Tested on one machine with five or more copies.
4. Guard choice and relay selection rules (different networks, never yourself, never the same relay twice).
5. The direct path for friends: encrypted address exchange, hole punching, switching and falling back. Real routers cannot be reproduced in the test setup here, so this is tested on one machine and needs real-world testing by people.
6. Take the dedicated relays out of the path, keeping them only as the first-contact entry points.
7. Padding policy.

Each step updates NETWORK.md and DATA.md before it ships (new connections, and the guards file).

## 8. Questions for the owner
1. Is it right that friends see your IP by default, with "Hide my IP from this friend" as the opt-out?
2. One relay on each side by default (faster) or two (stronger)?
3. Should volunteers be asked about publishing their address when they switch on "Help the network"? It is public in the directory.
4. How much padding is acceptable, given the good neighbour rule?
