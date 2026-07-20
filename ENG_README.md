# Numotirus —— DETAILED Branch

**English | [中文版](./README.md)**

This is not an ordinary code branch. It is the "learning edition" of Numotirus.

---

## Why does this branch exist?

The code in the `main` branch is complicated. It runs, but it doesn't tell you *why* it was written that way.

The `DETAILED` branch is where I (Domirus) read the code line by line and write down my understanding as comments. Its goal is: **to help the next person who doesn't understand find answers here.**

This branch is not written for "experts." It is written for people who want to learn but don't know where to start.

---

## What it is NOT

- ❌ Not "official documentation" — these comments are personal understanding, may contain errors
- ❌ Not "the final answer" — comments will be updated as my understanding deepens
- ❌ Not for "production" — this branch is for learning, not for running

---

## What it IS

- ✅ **Learning notes** — every comment represents "this is how I understood it at the time"
- ✅ **Signposts** — if you're stuck on a piece of code, these comments might just help you through it

---

## How to use

```bash
git checkout DETAILED
```

Start with `core/crypto/crypto.cpp`, from the `derive_key` function.

**Comment format:**

- `// DOMIRUS:` — my understanding
- `// AI:` — AI-generated explanation (kept for reference)
- `// TODO:` — not yet understood, leaving it for later

---

## Modules covered so far

| Module | File | Status |
| -------- | ------ | -------- |
| Crypto Layer | `core/crypto/crypto.cpp` | 🟡 In progress |
| Crypto Test | `core/crypto/crypto_test.cpp` | ⬜ Not started |
| DHT Routing | `core/p2p/dht.cpp` | ⬜ Not started |
| P2P Core | `core/p2p/p2p_core.cpp` | ⬜ Not started |
| Noise Protocol | `core/protocol/noise.cpp` | ⬜ Not started |
| Transport Layer | `core/transport/transport.cpp` | ⬜ Not started |

🟡 In progress · ⬜ Not started · ✅ Complete

---

## Maintenance of this branch

- I will keep updating it as I learn
- Speed is not the goal — **clarity** is
- If I find earlier comments were wrong, I'll correct them
- I will not delete "I don't understand this yet" just to look professional

---

## How to contribute

If you're also reading Numotirus code:

1. Switch to the `DETAILED` branch
2. Add comments next to code you understand (format: `// [your-name]:`)
3. Submit a PR
4. After review, it gets merged

You don't need to write C++ code. You just need to write down **what you understood**.

---

## Finally

Numotirus is a project that says: **"First let people understand, then let people use."** The `DETAILED` branch is the first part of that promise.

If you're willing to learn and write as you go, you're welcome here.

—— Domirus

---

**Your keys are in your hands. Your understanding is welcome here.**
