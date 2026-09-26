# PROJECT STARCOM: TECHNICAL SPECIFICATIONS & ARCHITECTURAL DIRECTIVE

## 1. PROJECT VISION & MOTIVATION
Starcom is a "middle finger" to centralized, data-farming communication platforms. It is a strictly Peer-to-Peer (P2P), server-less, anonymous, and government-proof communication client. It is designed to replace Discord by eliminating central identity verification, data harvesting, and server infrastructure costs.

The mantra is "Stupidly Fast, Minimal Footprint." No Electron, no WebView2, no AI integration. This is a pure C++ endeavor designed for maximum hardware efficiency and user sovereignty.

## 2. CORE ENGINE: THE "STAR BUS" (NETWORKING)
- **Language:** Native C++ only.
- **Interoperability:** Full binary-level communication compatibility between Windows (Winsock2) and Linux (Berkeley Sockets).
- **Transport:** Multi-lane UDP-based protocol with "Lanes" prioritized by data type (Voice > Signal > Video > Background Data).
- **Bandwidth Budget:** Hard-coded sliding scale from 1 Mbps to 20 Mbps aggregate throughput.
- **Traffic Shaping:** Implement a "Leaky Bucket" algorithm to ensure a 20 Mbps ceiling.
- **Network Awareness:** Hard-coded mathematical feedback loops (no AI). The engine must monitor RTT (Ping) and Jitter; if latency spikes or packet loss occurs, it must automatically downshift bitrates to protect the user's game/application stability.
- **NAT Traversal:** Implement UDP Hole Punching via public STUN servers to allow direct P2P connections without central relay servers.

## 3. SECTOR: IDENTITY & "STAR DNS"
- **Identity:** Cryptographic sovereignty. Every user is an Ed25519 Public/Private key pair generated locally. No emails, no phone numbers, no IDs.
- **Discovery:** Distributed Name Service (Star DNS) utilizing a Kademlia-style DHT. Users map easy-to-read "Pointers" (@names) to their current IP addresses.
- **Sticky Sessions:** Implement Connection Migration. Peers are identified by a 64-bit SessionID + Cryptographic Signature. If a user's IP changes mid-session, the connection must "follow" the new IP without dropping the call or chat.
- **Bootstrapping:** Use DNS Seeding (hardcoded domains returning A/AAAA records) to find initial network entry points.

## 4. SECTOR: MEDIA ENGINE & CODEC NEGOTIATION
- **Hardware Probing:** On startup, the client must use native OS APIs (Windows Media Foundation / Linux VA-API) to detect GPU capabilities (Nvidia NVENC, AMD AMF, Intel QuickSync).
- **Negotiation Protocol:** A mandatory "Handshake" must occur before any media stream. Peers trade "Capability JSONs." The system must lock to the highest common denominator codec supported by both hardware sets to prevent overloading weaker machines.
- **Codec Tiers:**
    1. AV1 (Hardware)
    2. HEVC/H.265 (Hardware)
    3. H.264 (Hardware)
    4. H.264 (Low-impact Software Fallback)
- **Voice:** Mandatory Opus codec (6kbps to 510kbps variable).
- **On-Demand Media:** Implement "Hyperlink Streaming." Shared videos are announced via metadata hashes. The video only streams (via chunked P2P transfer) when a recipient hits "Play."

## 5. SECTOR: ANONYMITY & ANTI-DOXING
- **Tiered Privacy:**
    - **Direct Path:** IP-to-IP for trusted "Friend List" peers (Max Speed).
    - **Masked Path:** For Hubs/Strangers, traffic must be routed through a "Volunteer Peer Relay."
- **Blind Relaying:** Starcom clients must be able to act as community bridges. Relaying must be "Blind" (End-to-End Encrypted via AES-NI/ChaCha20). The relaying peer has zero visibility into the payload and acts as a "Dumb Pipe."
- **Dandelion++ Scrambling:** In "Hubs," messages must hop through random peers (Stem Phase) before broadcasting (Fluff Phase) to obfuscate the sender's IP address.
- **Metadata Scrubbing:** A C++ "Scrubber" must strip all EXIF, GPS, and identifying XML data from every file before it enters the Star Bus.

## 6. FRONTEND: NATIVE UI
- **Architecture:** Zero browser dependencies. Use a lightweight C++ UI framework (Slint, Qt Widgets, or custom SDL2).
- **Aesthetic:** High-density, three-column layout (Hubs > Channels > Chat) mimicking the industry standard for zero learning curve.
- **Transparency:** The UI must feature a real-time "Network Health" monitor showing current Latency, Codec in use, and Active Bandwidth Lane allocation.

## 7. INITIAL SPRINT GOAL: SECTOR 1 SKELETON
The immediate priority is a C++ command-line or minimal-window prototype that can:
1. Generate a local Ed25519 identity.
2. Probe local hardware for AV1/H265/H264 encoding support.
3. Perform a DNS Seed lookup and initiate a direct UDP handshake between a Windows and Linux peer.
