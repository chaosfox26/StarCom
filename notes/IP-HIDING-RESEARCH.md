# Hiding your address: what the research found

Four independent searches covered anonymity networks built for real-time voice, mutual anonymity and friend-to-friend networks, network-layer addressing tricks, and what shipping products and attackers do in practice. This page combines them. It feeds the plan in PEER-RELAYS.md.

**How far to trust this page.** The research tool could not open most original papers and RFCs (the sites were blocked), so nearly every figure below comes from search-result summaries of the sources, not from reading them in full. Check a source before building on a specific number. Where no evidence was found, this page says so.

## 1. The one result all four searches agree on
No published design or shipping product gives two parties a direct packet flow in which neither can learn the other's address. Every system that hides an endpoint puts at least one relay between the two. One search states why as its own inference: a direct packet carries its source address, and hole punching needs both addresses. So the real choices are about how to make the hiding cheap, fast and trustworthy, not about avoiding relays altogether.

## 2. What real products do
| Product | How it hides your address from the other person |
|---|---|
| Signal, WhatsApp, Telegram (optional setting), Element (optional setting) | A relay carries the call. Direct is the default or an option, and relaying is the privacy setting. |
| Discord | Everyone talks to a media server, so peers never see each other. |
| Steam (Valve) | Relays, and Valve does not share addresses between untrusted players so that nobody can flood them. |
| Apple iCloud Private Relay | Two hops run by different companies, so neither knows both who you are and where you go. Browser traffic only. |
| Tox, Jami | Peer to peer, friends see your address. Tox says plainly it does not hide it from friends. |
| Session, Briar | Hidden for messages only (onion routing). Session's calls expose the address. Briar runs over Tor. |

## 3. Systems built to hide addresses, and what they cost for voice
- **Tor onion services.** Each side builds its own path to a meeting relay, so neither learns the other's address. A study of over 500,000 calls found good quality and under 400 ms one-way delay in over 85% of calls; the same study reports 210 ms on average for two-hop paths. **Donar** sends voice over several paths at once and reports under 360 ms for 99% of packets in most calls. Weaknesses: someone who sees both ends can match traffic, and attacks exist on finding where an onion service sits. Tor carries only TCP.
- **Herd (SIGCOMM 2015).** Anonymous calling with one extra relay hop and ordinary users helping to carry load. It uses dedicated mixes run by independent organisations. Reported delay about 400 ms.
- **Lokinet.** Onion routing that carries UDP and claims real-time voice works. The claim is the vendor's own and I found no independent measurement. Its relays are staked, pooled-money nodes, not ordinary peers.
- **I2P.** Separate inbound and outbound tunnels, so a round trip uses four. No voice measurements found. Practical attacks are published.
- **Tarzan, MorphMix.** Peer-run designs from the early 2000s. MorphMix's attack detection was broken in published analysis. No voice figures.
- **Too slow for live voice:** Nym and Loopix (500 to 800 ms and its own docs say calls are unsuitable), Vuvuzela, Karaoke, Riffle (seconds to tens of seconds). Voice designs that do work (Yodel, Pirates, Addra) all need servers.
- **HORNET, TARANET.** Fast network-layer onion routing, but it needs routers that support it, so it cannot run on today's internet.

## 4. Designs close to "direct but hidden"
- **Hordes.** The request goes out over a forwarding path; the reply goes straight back to a group of k addresses that includes yours. The friend cannot tell which of the k is you, so you are hidden among k, not fully. The cost grows with k and it needs multicast.
- **P5 and DC-nets (Herbivore, Dissent).** Hide the receiver or sender inside a group that sends fixed-size traffic all the time. Strong, but expensive in bandwidth and the group members see each other.
- **Anonymous anycast (Panini).** Hides which of several receivers a sender reached. 0.76 s with one of eight receivers. Unclear whether it can run on peers.
- **Friend-to-friend (Turtle, Freenet darknet, RetroShare).** You connect only to friends and traffic passes through chains of friends. Directly connected friends still see your address. RetroShare hides it from friends only by running over Tor or I2P.
- **Friend-chosen relays (a synthesis, not a published design).** Each person routes through a trusted friend or friend of a friend. The other friend sees only that relay's address.
- **Nothing found** for a direct path between two relays chosen one by each side, or for peer-run address translation beyond Tarzan.

## 5. Network-layer tricks
| Technique | What it does | What it does not do |
|---|---|---|
| IPv6 temporary addresses | The visible address rotates (RFC 8981: valid about 2 days). No administrator rights. | The prefix still shows your provider and area. |
| A different source address per friend from your /64 | Two friends cannot compare notes on an address. Linux can do it with elevated rights. | Same prefix, and no clean way found on Windows. |
| IPv6 address hopping (MT6D) | Both ends rotate addresses mid-session. | Research prototype, both sides must run it. |
| HIP, LISP | Separate identity from address. | The address is still exposed to the peer, or sits behind a relay. |
| QUIC connection IDs and migration | Sessions survive an address change, and observers cannot link paths by ID. | The peer and observers still see the real source address. |
| Shared carrier address (CGNAT) | The friend sees an address shared by many customers. | Your provider decides, not you, and the provider can trace it. |
| Forged source addresses | Used by attackers for anonymity and flooding. | Replies go to the forged address, so something must forward them to you. About a third of networks were found to allow it, but it needs administrator rights and is widely filtered. |
| MASQUE, Oblivious HTTP and DNS, Private Relay | Two proxies with split knowledge. Open-source implementations exist. | Two colluding hops, or one hop plus a traffic watcher, defeat it. |
| Authenticated silence (WireGuard, single-packet authorisation) | Unauthenticated packets get no answer, so scans find nothing and a leaked address is far less attackable. | Does not stop floods or hide your provider and area. |

## 6. How addresses leak and get abused
- Skype's "resolver" services turned usernames into addresses and were sold with flooding services.
- A 2022 paper collected peer addresses from every peer-assisted video service it tested, using plain STUN requests, even behind a VPN.
- In games, any lobby member with a packet sniffer sees every peer address.
- A Tox flaw (CVE-2018-25022) let an attacker learn a target's address from only their ID by abusing relayed packets. The fix was to restrict what can be relayed. The lesson for StarCom: never let an unauthenticated packet cause an address to be revealed.
- Evidence-backed defences: relaying; withholding addresses from untrusted peers (Valve); admitting traffic only after a cryptographic handshake.

## 7. What this means for StarCom
The evidence ranks these, best first:
1. **Two hops through ordinary peers, one relay on each side, meeting in the middle.** The only approach that fully hides your address from the person you talk to. Voice over two hops is measured at around 200 ms extra; multipath and redundancy (Donar) reduce the tail. Needs no administrator rights.
2. **Friends or friends of friends as the relays.** The same shape, with trust instead of strangers. Needs a trusted contact online.
3. **A reply to a group of k addresses (Hordes style).** The only "direct feeling" path found. Hides you among k, costs about k times the bandwidth, and has not been demonstrated for voice.
4. **Authenticated silence everywhere.** Cheap and compatible with everything else. Makes a leaked address much harder to attack.
5. **Rotating IPv6 source addresses plus sessions that survive an address change** (StarCom already supports migration). A stale leaked address stops working. The prefix remains.
6. **Direct connection as an explicit opt-in** for friends you trust, as Signal, Telegram, Element and WhatsApp do. It is the only mode in which the friend learns your address.

## 8. Gaps worth checking before building
- Read the primary sources for Donar, the Tor voice study, Hordes and Panini; this page relied on summaries.
- Measure real added delay for one and two relays per side with StarCom's own voice path.
- Find out whether Windows can give each friend a different source address without administrator rights.

## Sources
Anonymity networks and voice: [Herd](https://conferences.sigcomm.org/sigcomm/2015/pdf/papers/p639.pdf), [Tor voice study](https://petsymposium.org/popets/2020/popets-2020-0063.pdf), [Donar](https://www.usenix.org/conference/nsdi22/presentation/bromberg), [Tor onion services](https://community.torproject.org/onion-services/overview/), [Vanguards](https://blog.torproject.org/announcing-vanguards-add-onion-services/), [UDP over Tor proposal](https://spec.torproject.org/proposals/339-udp-over-tor.html), [Lokinet](https://lokinet.org/faq), [I2P tunnels](https://i2p.net/en/docs/overview/tunnel-routing/), [I2P attacks](https://sites.cs.ucsb.edu/~chris/research/doc/raid13_i2p.pdf), [Nym limits](https://nym.com/docs/developers/limitations), [Tarzan](https://www.freehaven.net/anonbib/cache/tarzan:ccs02.pdf), [MorphMix analysis](https://link.springer.com/chapter/10.1007/11957454_21), [Vuvuzela](https://pdos.csail.mit.edu/papers/vuvuzela:sosp15.pdf), [Karaoke](https://www.usenix.org/system/files/osdi18-lazar.pdf), [Riffle](https://petsymposium.org/popets/2016/popets-2016-0008.pdf), [Yodel](https://davidlazar.org/papers/yodel.pdf), [Pirates](https://link.springer.com/chapter/10.1007/978-981-97-5101-3_11), [HORNET](https://arxiv.org/abs/1507.05724), [TARANET](https://arxiv.org/pdf/1802.08415).

Mutual anonymity and friend networks: [Turtle](https://www.cs.vu.nl/~ast/Publications/Papers/sec_prot-2004.pdf), [Hordes and related designs (survey)](https://arxiv.org/pdf/1208.3192), [P5](http://www.cs.umd.edu/~srin/PDF/2005/p5-jou.pdf), [Herbivore](https://www.cs.cornell.edu/people/egs/papers/herbivore-tr.pdf), [Dissent](https://www.usenix.org/system/files/conference/osdi12/osdi12-final-115.pdf), [Panini](https://arxiv.org/pdf/2305.16629), [Cashmere](https://people.cs.uchicago.edu/~ravenben/publications/pdf/cashmere-nsdi05.pdf), [OneSwarm](https://homes.cs.washington.edu/~arvind/papers/oneswarm.pdf), [RetroShare over Tor](https://www.whonix.org/wiki/RetroShare).

Network-layer techniques: [RFC 7721](https://www.rfc-editor.org/info/rfc7721/), [RFC 8981](https://www.rfc-editor.org/rfc/rfc8981.html), [RFC 9000](https://www.rfc-editor.org/rfc/rfc9000.html), [RFC 9063](https://www.rfc-editor.org/rfc/rfc9063.html), [RFC 6888](https://www.rfc-editor.org/info/rfc6888/), [MT6D](https://security.vt.edu/docs/MT6D_A_Moving_Target_IPv6_Defense.pdf), [MIMIQ](https://www.usenix.org/system/files/foci20-paper-govil.pdf), [MASQUE](https://blog.apnic.net/2023/03/23/hiding-behind-masques/), [Private Relay traffic analysis](https://dl.acm.org/doi/fullHtml/10.1145/3579856.3595793), [CAIDA Spoofer](https://www.caida.org/projects/spoofer/), [IPv6 interface-ID tracking](https://arxiv.org/pdf/2203.08946), [WireGuard](https://www.wireguard.com/papers/wireguard.pdf), [fwknop](https://github.com/mrash/fwknop).

Products and attacks: [Signal](https://signal.org/blog/signal-video-calls/), [WhatsApp](https://engineering.fb.com/2023/11/08/security/whatsapp-calls-enhancing-security/), [Telegram](https://www.bleepingcomputer.com/news/security/telegram-leaks-ip-addresses-by-default-when-initiating-calls/), [Matrix TURN](https://matrix-org.github.io/synapse/v1.68/turn-howto.html), [Discord](https://discord.com/blog/how-discord-handles-two-and-half-million-concurrent-voice-users-using-webrtc), [Valve](https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/README_P2P.md), [Apple Private Relay](https://www.apple.com/icloud/docs/iCloud_Private_Relay_Overview_Dec2021.pdf), [Skype resolvers](https://krebsonsecurity.com/2013/03/privacy-101-skype-leaks-your-location/), [Tox flaw](https://blog.tox.chat/2018/04/security-vulnerability-and-new-toxcore-release/), [Tox FAQ](https://skyzohkey.github.io/tox-website/faq.html), [Jami](https://jami.net/establishing-peer-to-peer-connections-with-jami/), [Session calls](https://getsession.org/calls-on-session), [peer addresses in WebRTC services](https://arxiv.org/abs/2212.02740).
