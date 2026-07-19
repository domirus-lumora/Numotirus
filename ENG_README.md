# Numotirus — The Messenger Protocol

**Decentralized Key Exchange · No Central Server · Open Communication Infrastructure**
Numotirus is not a "chat app". It is a communication protocol. No company runs the servers. No platform moderates your content. No age gates. You hold your keys. You build your connections. You decide who can find you.

Not "social media". Social infrastructure.

📖 **[Read this talk](./docs/ENG_humanity.md)** — A monologue about connection, disaster, and trust.

## Why Numotirus?

Because centralized platforms are controlling our conversations. Because age, identity, and location are becoming barriers to speech. Because code should not be "banned" — code should be built, together.

## What can it do?

| Feature | Description |
| --------- | ------------- |
| **Key Exchange** | Noise protocol — XX pattern handshake + SAS verification. You own your identity. |
| **P2P Communication** | UDP + KCP reliable transport. Direct node-to-node messaging. No central server. |
| **Open Source** | Apache 2.0 — view, modify, and distribute freely. |
| **Embeddable** | Lumora integration planned as default assistant. |

## Tech Stack

- **Core Protocol**: C++20 + C11 (cross-platform)
- **Network Layer**: Custom P2P stack (UDP + KCP + non-blocking `select()`)
- **Cryptography**: libsodium (X25519 + XChaCha20-Poly1305 + Noise)
- **GUI**: C# / Avalonia (cross-platform, in development)

## Not a One-Person Project

This is open source. This is community. This is yours.

You don't need to "register". You just need to "download".
You don't need to be "approved". You just need to hold your keys.
You don't need to "wait for permission". You just need to run the code.

## Status

🚧 Core protocol and P2P layer have working implementations. Contributions welcome.

- [x] Key exchange protocol (Noise XX + SAS)
- [x] P2P network layer (UDP + KCP + cross-platform)
- [ ] GUI client prototype
- [ ] Lumora integration

## License

Apache 2.0. Use it, modify it, include it in your own projects. Just keep the [copyright notice](./LICENSE).

## Join Us

This is not a cry for help. It's a call for collaboration.

- **Repository**: [https://github.com/domirus-lumora/Numotirus](https://github.com/domirus-lumora/Numotirus)
- **Issues**: [https://github.com/domirus-lumora/Numotirus/issues](https://github.com/domirus-lumora/Numotirus/issues)
- **Discussions**: [Discord](https://discord.com/invite/KdjnEtpSP8)

You're not "helping Domirus". You're building a better internet.
