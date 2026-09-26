# Components

Every library, service or outside component StarCom uses is listed here, with what it is for and its license.

| Component | Version | What it is for | License | Where |
|---|---|---|---|---|
| Monocypher | 4.0.3 (official release tarball monocypher-4.0.3.tar.gz, sha256 8cc9bc341a66249016db9bd70e9142d8d0aef9945973744b1ac05dbc55d8ee66) | All cryptography: Ed25519 identity keys and signatures (optional ed25519 module), X25519 key agreement, ChaCha20-Poly1305 encryption, BLAKE2b hashing | CC0 1.0 or BSD-2-Clause (dual) | third_party/monocypher, unmodified |

| Opus | 1.5.2 (official release opus-1.5.2.tar.gz from xiph.org, sha256 65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1, matching xiph.org's published SHA256SUMS) | Voice codec (encode and decode), required by the spec | BSD 3-Clause, with royalty-free patent licences from its contributors | third_party/opus. Unmodified files, but only part of the release: the neural-network features (the dnn folder) are left out on purpose, because StarCom uses no AI; processor-specific assembly is left out so the same portable C builds everywhere. The compiled file list is third_party/opus-sources.txt. The release's AUTHORS credits file is left out because it lists contributors' email addresses and StarCom's repository carries no email addresses; the license (COPYING, which names the copyright holders) is kept, as the BSD license requires. The Windows build hides one harmless type-conversion warning inside Opus (C4244); the Opus code itself is not edited. |

Operating system APIs used (part of the OS, not shipped by us): Winsock2, Media Foundation and WASAPI (audio) on Windows; BSD sockets and, if present, VA-API (libva) on Linux.
