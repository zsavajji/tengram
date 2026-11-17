# Tengram

A Telegram client for BlackBerry 10 (10.3.3, rooted phones), fast and light on the battery.

- **`tengramd`, the engine** (`engine/`, C++20 over TDLib/TDLight): the only process that talks to Telegram. It keeps
  an encrypted `chats.db` current and serves the UI over a loopback socket (`docs/protocol.md`). Packaged, it is the
  app's headless service, and it feeds the BlackBerry Hub.
- **`tengram`, the UI** (`app/`, Cascades, Qt 4.8): a fork of HyperBerry, the user's WhatsApp client. It reads
  `chats.db` and sends the engine commands.

## What it does (0.1.0.23, 2026-11-17)

- **Login** by phone code, password or QR; the chat list (pinned, archived, muted, unread), synced across engine restarts.
- **Chats:** opens at the first unread message; paging both ways; search in a chat; reply (menu or swipe left), edit,
  delete for me or everyone, copy, **forward** (to any chat or a forum's topic, with search in the picker); a tap on
  a quote goes to the quoted message; a square button jumps to the newest message; letter shortcuts in a message's
  menu (R A V C E X D F) on the keyboard phones.
- **Media:** photos and videos in the app's own viewers (the video keeps its shape); documents and audio through
  the system; a download ring with "1.2 MB / 23 MB" (a tap cancels); sending photos, videos and files, a picture
  **as a file** (PNG or JPEG) or **edited** first in the system picture editor (crop, rotate).
- **Voice notes:** play (decoded from OGG Opus), record and send as real voice notes with waveform; the other side
  sees "recording…" for as long as you record.
- **Groups with topics:** a forum opens its topics (square tiles in the topic's colour, pinned first, closed
  marked); each topic is its own chat for reading, sending, voice and the unread divider; Mark all read.
- **Channels:** read-only, the composer row reduced to its menu.
- **Pinned messages** as Telegram has them, newest first, per chat or topic.
- **Settings:** Account (profile), **Devices** (this device and the active sessions; end one or all others; link
  Telegram Desktop by scanning its QR code), Appearance (theme, text size, Normal or Compact chat list), Chats,
  Notifications (instant, batched or automatic delivery), Data and storage.
- **Hub:** one item per chat with its name and last message; read state follows the app.
- **Ten languages** besides English: de, es, fr, hi, id, it, pl, ru, uk, vi.

## Build and install

From the Mac, with Docker (OrbStack); details and traps in `docs/HANDOFF.md` §3–§5.

```sh
scripts/build-deps.sh   # TDLight, OpenSSL, zlib, libopus for BB10, once (about 12 min, stamped)
scripts/build-app.sh    # engine + UI + build/tengram.bar, the next build id
```

The Telegram API id and hash come from the environment (`TG_API_ID`, `TG_API_HASH`) or from `.tg-keys` at the
repo's root (gitignored). The package is unsigned and installs on a rooted phone (`docs/HANDOFF.md` §4). The
engine's 30 MiB headless memory cap is lifted by the `../rlimit-patch` addon, which the app offers to install.

## Where things are

| | |
|---|---|
| `docs/HANDOFF.md` | the state, what is proven on the phone, how to test, the decisions, where to resume |
| `docs/protocol.md` | the engine–UI contract: commands, events, `chats.db`'s tables |
| `docs/ui-fork.md` | what changed from HyperBerry's UI and why |
| `NOTES.md` | the project's history: assessments, measurements, findings |
| `CLAUDE.md` | facts about the target every change assumes |
