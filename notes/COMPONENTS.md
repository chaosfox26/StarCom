# Components

Every library, service or outside component StarCom uses is listed here, with what it is for and its license.

| Component | Version | What it is for | License | Where |
|---|---|---|---|---|
| Monocypher | 4.0.3 (official release tarball monocypher-4.0.3.tar.gz, sha256 8cc9bc341a66249016db9bd70e9142d8d0aef9945973744b1ac05dbc55d8ee66) | All cryptography: Ed25519 identity keys and signatures (optional ed25519 module), X25519 key agreement, ChaCha20-Poly1305 encryption, BLAKE2b hashing | CC0 1.0 or BSD-2-Clause (dual) | third_party/monocypher, unmodified |

Operating system APIs used (part of the OS, not shipped by us): Winsock2 and Media Foundation on Windows; BSD sockets and, if present, VA-API (libva) on Linux.
