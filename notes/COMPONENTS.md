# Components

Every library, service or outside component StarCom uses is listed here, with what it is for and its license.

| Component | Version | What it is for | License | Where |
|---|---|---|---|---|
| Monocypher | 4.0.3 (GitHub tag archive, sha256 a7cbae546fbdc489bca632c3747e1ceb8ca3d4bd39e2706a0916f28ccd280e50) | All cryptography: Ed25519 identity keys and signatures (optional ed25519 module), X25519 key agreement, ChaCha20-Poly1305 encryption, BLAKE2b hashing | CC0 1.0 or BSD-2-Clause (dual) | third_party/monocypher, unmodified |

Operating system APIs used (part of the OS, not shipped by us): Winsock2 and Media Foundation on Windows; BSD sockets and, if present, VA-API (libva) on Linux.
