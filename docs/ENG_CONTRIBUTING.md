# Contributing to Numotirus

Thanks for considering contributing. This is not a "help us" project. This is a "build with us" project.

## Current Progress

| Module | Status | Notes |
| -------- | -------- | ------- |
| `core/crypto` | ✅ Complete | C API, X25519 + XChaCha20-Poly1305 + ECIES |
| `core/p2p` | ✅ Complete | UDP + KCP + non-blocking select, cross-platform |
| `core/protocol` (ZRTP) | ✅ Complete | Key exchange + SAS generation + TOFU |
| `p2p_chat.c` | ✅ Working | Command line chat example |
| `core/plugin` | ❌ Pending | Plugin system |
| GUI | ❌ Pending | Avalonia (C#) |

**Next steps where you can help**: NAT traversal, ZRTP async callbacks, GUI prototype, Lumora integration.

## Issues

### Bug Report

- Steps to reproduce
- Expected behavior
- Actual behavior
- Environment (OS, compiler version)

### Feature Request

- User scenario (not "I want feature X", but "I want to do Y, and X would help")
- If related to protocol design, mention which layer (Crypto / Network / Protocol / GUI)

## Pull Requests

### Branch Naming

- `feature/short-description`
- `fix/short-description`
- `doc/short-description`

### PR Title Format

`[Layer] Short description`

Examples:

- `[Crypto] Add X25519 key exchange stub`
- `[Network] Add KCP retransmission optimization`
- `[Protocol] ZRTP SAS async callback`
- `[GUI] Initialize Avalonia main window`

### PR Description

Must include: `Closes #(issue number)`

### Review Requirements (By Stage)

| Stage | Active Contributors | Review Rule |
| ------- | --------------------- | -------------- |
| Early | < 3 people | No approval needed. Author squashes and merges. |
| Mid | 3-10 people | At least 1 **other** contributor approves |
| Mature | 10+ people | At least 2 approvals |

Currently in **Early stage**. Stage changes will be announced.

### Merge Strategy

Squash merge only. Keep commit history clean.

## Code Style

Core languages: **C11 + C++20**

- C/C++: Run `clang-format` before commit
- C#: Run `dotnet format` before commit
- Comments: English first, then Chinese. Format: `// English text. 中文文本。`
- AI-generated code: Allowed, but you must understand every line, test yourself, and declare in PR

Full guide: see `CODE_STYLE.md`

## Code of Conduct

Be respectful. Technical disagreements are not personal attacks.

Discrimination, harassment, or doxxing → immediate ban.

## Questions

Open a Discussion on GitHub, or find us on [Discord](https://discord.gg/jbAyBtWw).
