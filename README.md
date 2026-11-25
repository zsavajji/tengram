<p align="center">
  <img src="app/assets/images/icon-144.png" width="120" alt="Tengram icon">
</p>

<h1 align="center">Tengram</h1>

<p align="center">
  <b>A native Telegram client for BlackBerry 10</b><br>
  Made for the Q10 and its keyboard. Tested on the Classic too.
</p>

<p align="center">
  <img alt="Version" src="https://img.shields.io/badge/version-1.0.4-2AABEE">
  <img alt="Platform" src="https://img.shields.io/badge/platform-BlackBerry%2010.3.3-222222">
  <img alt="UI" src="https://img.shields.io/badge/UI-Cascades%20%2F%20Qt%204.8-41CD52">
  <img alt="Engine" src="https://img.shields.io/badge/engine-C%2B%2B20%20%2F%20TDLib-00599C">
  <img alt="License" src="https://img.shields.io/badge/license-GPLv3-blue">
</p>

---

Tengram brings Telegram to BlackBerry 10 as an app that looks and behaves like a stock BB10 app: Hub
notifications, an Active Frame, the action bar, keyboard shortcuts. Your chats, groups, channels, calls and
stickers are all there, and a background engine keeps them current after you close the app.

> [!NOTE]
> Tengram is an independent project. It is not affiliated with or endorsed by Telegram. It runs on BlackBerry
> 10.3.3 on rooted phones.

## Features

**Chats**
- Sign in with a code sent to your phone, two-step verification password, or a QR code
- Chat list with pinned, archived, muted and unread chats, and Mark all read
- Search with Telegram: chats, channels and apps, with the people you write to most
- Private chats, groups, channels, groups with topics, and secret chats
- Reply (from the menu or with a swipe), edit, delete for you or for everyone, copy and forward to any chat or topic
- Reactions, mentions, pinned messages and polls. A tap on a quote goes to the quoted message
- Telegram links open in the app: join a group or follow a channel from a link
- Search inside a chat, and export a chat's history
- Every emoji in colour (Google's Noto Color Emoji) with its own picker
- Stickers, animated ones too, with recent, favourite and installed sets
- Translation: long-press a message to translate it, or pick a language for a chat and read its incoming
  messages translated in place, in any of Telegram's 109 languages. Telegram does the work, or Google with your
  own API key

**Media**
- Photos, albums, videos, documents and audio, in the app's own viewers
- A download ring with its progress, and Download to save a file to the phone
- Send several photos and videos at once, as an album or as files, edited first in the phone's picture editor
- Voice notes: waveform playback, at your ear with the proximity sensor, and recording from the composer
- Share files and text into Tengram from other apps

**Calls**
- 1:1 voice and video calls, with Telegram's own call library
- Video on the phone's hardware H.264 encoder and decoder
- Rings with the phone's ringtone and brings Tengram to the front
- A call log

**Groups and channels**
- Group and channel info, members, admins, invite links and new groups
- Blocked users

**Settings**
- Account: name, bio, picture
- Devices: your active sessions, end one or all, and link Telegram Desktop by scanning its QR code
- Privacy and security, two-step verification included
- Data and storage: automatic downloads, storage and data used
- Notifications, and a Translation page for the optional Google key

**BlackBerry 10 integration**
- A Hub account, with one item per chat
- Messages keep arriving after you close the app
- Active Frame counting unread private chats, groups and channels
- Light and dark themes, a black or grey dark background, round or square look, text size, "Enter sends"
- English, Italian, German, Spanish, French, Polish, Russian, Ukrainian, Hindi, Indonesian, Vietnamese

## How it works

```
             one .bar, one sandbox
 ┌─────────────────────────┐            ┌──────────────────────────┐
 │ tengram   (UI)          │  loopback  │ tengramd   (engine)      │
 │ Cascades · Qt 4.8 · C++ │◄──────────►│ C++20 · TDLib · headless │◄──► Telegram
 │                         │  TCP+token │                          │
 └───────────┬─────────────┘            └────────────┬─────────────┘
             │ reads                         writes  │
             └──────────► data/chats.db (SQLCipher) ◄┘
```

Tengram ships as one `.bar` with two processes:

- **`tengramd`, the engine**, written in C++20 over [TDLib](https://github.com/tdlib/td) (the
  [TDLight](https://github.com/tdlight-team/tdlight) fork) and cross-compiled for BB10 (QNX on ARMv7). It is the
  only process that talks to Telegram. It runs as a BB10 headless service, so the connection outlives the UI,
  keeps an encrypted `chats.db` current and posts Hub notifications. Calls go through
  [tgcalls](https://github.com/TelegramMessenger/tgcalls) and [tg_owt](https://github.com/desktop-app/tg_owt),
  Telegram's WebRTC.
- **`tengram`, the UI**, a native Cascades app (Qt 4.8, C++ and QML), forked from
  [HyperBerry](https://github.com/zsavajji/hyperberry), the WhatsApp client. It reads `chats.db` directly, so the
  chat list and history show at once, even while the engine is still starting. It sends commands to the engine
  over a token-protected loopback socket as NDJSON ([docs/protocol.md](docs/protocol.md)).

The message cards and the chat list rows are drawn in C++ for speed on the Q10's dual-core Krait. Video calls
pass H.264 straight to the phone's hardware codecs through OpenMAX IL, and call audio runs in the UI process,
since the headless engine has a capped heap. Animated stickers are drawn by
[rlottie](https://github.com/desktop-app/rlottie).

## Building

You need Docker and the local images `bb10-cxx20` and `yamsergey/bb10-ndk:0.6`, and a Telegram API id and hash
from [my.telegram.org](https://my.telegram.org).

```sh
scripts/build-deps.sh    # TDLight, OpenSSL, zlib and libopus for BB10, once (about 12 min)
scripts/build-calls.sh   # tg_owt and tgcalls for BB10, once
scripts/build-app.sh     # build/tengram.bar: engine + UI, unsigned, the next build id
```

`build-app.sh` reads the API id and hash from `TG_API_ID` and `TG_API_HASH`, or from a `.tg-keys` file at the
repo's root (gitignored) with those two lines. It also needs
[authman-rlimit](https://github.com/zsavajji/authman-rlimit) cloned beside this repo as `../rlimit-patch`: the app
bundles its `headless-mem` addon, which raises BB10's 30 MB memory cap for background services to 128 MB, and
offers to install it.

The build fetches the pieces that aren't committed (Google's emoji font, qrcodegen, libwebp, the OpenMAX headers,
the SQLCipher driver), builds rlottie and compiles the translations. The resulting `.bar` is unsigned: install it
on a rooted BB10 phone ([docs/install-root-pps.md](docs/install-root-pps.md)).

[docs/HANDOFF.md](docs/HANDOFF.md) has the project's state, how to test on a phone and the decisions made;
[NOTES.md](NOTES.md) its history.

## License

Copyright © 2026 Andrea Faggin.

Tengram is free software: you can redistribute it and/or modify it under the terms of the
[GNU General Public License v3.0](LICENSE).

The libraries keep their own licenses: TDLib is under the Boost Software License, tgcalls is LGPL-3.0, tg_owt
(WebRTC) BSD-3-Clause, rlottie LGPL-2.1, OpenSSL Apache-2.0, and Noto Color Emoji is under the SIL Open Font
License. The About page in the app lists them all.
