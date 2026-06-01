# Contributing to Numotirus

Thanks for considering contributing. This is not a "help us" project. This is "build with us" project.

## Issues

### Bug Report

- Steps to reproduce
- Expected behavior
- Actual behavior
- Environment (OS, version)

### Feature Request

- User scenario (not "I want feature X", but "I want to do Y, and X would help")
- If related to protocol design, mention which layer (Key Exchange / P2P / Encryption)

## Pull Requests

### Branch Naming

- `feature/short-description`
- `fix/short-description`
- `doc/short-description`

### PR Title Format

`[Layer] Short description`

Examples:

- `[Protocol] Add X25519 key exchange stub`
- `[Network] Bootstrap node discovery skeleton`
- `[GUI] Initialize Avalonia main window`

### PR Description

Must include: `Closes #(issue number)`

### Review Requirements (By Stage)

| Stage | Active Contributors | Review Rule |
| ------- | --------------------- | -------------- |
| Early | < 3 people | No approval needed. Author squashes and merges. |
| Mid | 3-10 people | At least 1 **other** contributor approves |
| Mature | 10+ people | At least 2 approvals |

Currently in **Early stage**. Stage changes will be announced in README or a public post.

### Merge Strategy

Squash merge only. Keep commit history clean.

## Code Style

Not finalized while language is TBD. For now:

- Run `clang-format` (C++) / `rustfmt` (Rust) / `dotnet format` (C#) before commit
- Source code comments: English first, then Chinese. Format: `// English text. 中文文本。`

## Code of Conduct

Be respectful. Technical disagreements are not personal attacks.

Discrimination, harassment, or doxxing → immediate ban.

Full CoC: see `CODE_OF_CONDUCT.md`

## Questions

Open a Discussion on GitHub, or find us on Discord.
