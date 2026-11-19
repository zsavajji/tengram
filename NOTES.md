# Tengram: Telegram for BlackBerry 10

Notes from the first assessment, 2026-11-15. Nothing is built yet. This is where the project
starts from: what was looked at, what the user decided, and what to do first.

Sources read: `../hyperberry` (WhatsApp for BB10, working on the Q10), `../../meegram2` (Telegram
on TDLib for the Nokia N9, working), `../tele10/PLAN.md` (an earlier Telegram-for-BB10 plan,
2026-10-14, never started), `../bb10-cxx20` (clang 19 + libc++ for BB10).

## Decisions (user, 2026-11-15)

- **100% C++**, the engine included: TDLib is C++, so the engine uses TDLib's native C++ API
  (`td::ClientManager`, `td_api` objects). No Go, no JSON relay.
- **The UI is a fork of HyperBerry's Cascades app**, and HyperBerry's split stays: the engine
  writes `chats.db`, the UI reads it read-only, and the two talk NDJSON over a token-protected
  loopback socket (`../hyperberry/docs/protocol.md`).
- **TDLib, with TDLight as a drop-in.** The TDLib source is a pinned (repo, commit) pair in the
  build script, and the QNX port patches are a series in `patches/td/`, so either can be built.
- The user's point: TDLib runs fine on the N9 (single-core 1 GHz Cortex-A8, 1 GB), so the Q10
  (dual Krait 1.5 GHz, 2 GB) has headroom. Speed isn't the risk; BB10's own limits are.

## Why HyperBerry ports well

HyperBerry is two processes with one contract: `chats.db` (engine writes, UI reads) and the
socket protocol (UI sends commands, the engine sends events that only say "re-read these rows").
The UI never speaks WhatsApp. An engine that fills the same tables and speaks the same protocol
keeps most of the UI working. Ids already cross the socket and QML as strings, so TDLib's int64
ids fit as they are.

Carries over almost unchanged: the chat list, the message cards (painted in C++), the composer,
emoji (Noto Color Emoji and the picker), voice notes (OGG/Opus on both sides), the media opener,
OMX H.264 video decode, the Hub account, the Active Frame, themes, translations, the per-device
sizing and the speed work.

Rough split of the UI's ~22.5k lines (13.2k C++, 9.4k QML): about 65% unchanged, 20% adapted,
15% dropped (mainly calls, ~3.5k lines).

## What changes

| Area | Size | Notes |
|---|---|---|
| Engine | large | A rewrite. HyperBerry's Go engine is 14.2k lines (+10k tests), almost all WhatsApp. TDLib does the heavy work (sync, encryption, files), so expect 5–8k lines of C++. `meegram2/src/daemon` (main.cpp, Notifier.cpp) and `../tele10/PLAN.md` §4–§6 are the starting design. |
| Login | small | QR or pairing code becomes phone → code → 2FA password → registration, plus TDLib's QR login. `LinkNumberPage.qml` mostly survives. |
| History | medium, the main shift | WhatsApp pushes history once, at linking; Telegram loads it on demand. The UI needs a "load older" command when scrolling up. `chats.db` stops being "the only copy, never rebuilt" and becomes a cache the engine can rebuild from TDLib (tele10's `view.db`). |
| Text styles | small–medium | `app/src/Markup.h` parses WhatsApp's `*bold*` markup. Telegram sends entities (bold, italic, links, spoilers, custom emoji): store them in `extra`, render from them. |
| Message kinds | medium | New: TGS animated stickers (rlottie, as meegram2), WebM video stickers, GIFs as MP4, round video notes, dice, quizzes, many more service messages. |
| Reactions, receipts | small | Telegram gives reaction counts, not a row per reactor. No "delivered" state: sent and read only. |
| Groups, channels | medium | Supergroups and channels can be huge: the member list must page. Forum topics are new; folders optional. |
| Stories | medium | TDLib has them, with a different model (story lists, privacy). `StoriesModel` adapts. |
| Calls | in progress (2026-11-19, branch `feature/calling`) | tgcalls + tg_owt ported to QNX; see "Calls" at the end. HyperBerry's call UI, `CallAudio`, ringing and video pieces are to be ported. |

## Risks, in order

1. **TDLib on QNX: known to work, but we must do our own port.** Closed-source BB10 clients (TbbX, Telega and
   others) run TDLib; their developer confirmed it to the user (2026-11-16). No patches are public.
   Asking that developer for port hints is the cheapest shortcut. The compiler is solved:
   `bb10-cxx20` gives clang 19 with libc++ (C++20; TDLib needs C++17). To do: TDLib's platform
   detection (`tdutils/td/utils/port/platform.h` errors on an unknown OS), `poll` instead of
   `epoll`, no `eventfd`, `TD_HAS_MMSG=0` (as meegram2), and OpenSSL and zlib built with
   `bb10-clang`. Expect a few days of patching.
2. **The headless memory cap.** BB10 kills the background engine at about 30 MB
   (`../hyperberry/docs/plan/phase6.md`; it needs `_sys_headless_xlarge`, else 3 MB).
   meegram2's daemon measured 24 MiB on the N9, on Linux. That is about 6 MB of margin, unmeasured
   on QNX. This is also why the engine is C++: a Go runtime would eat the margin. If TDLib doesn't
   fit, the engine has to run in the foreground UI process, and background notifications get
   much harder.
3. **Binary size and cold start.** A stripped TDLib is probably 15–25 MB. Measure the engine's
   start from boot.

### Cutting TDLib down (user, 2026-11-16)

Unused features such as gifts, Stars, payments, business and calls can come out. Cheapest first:
- **Linker flags:** `-ffunction-sections -fdata-sections -Wl,--gc-sections`, `-Os`, LTO. No patches
  to maintain.
- **Patches:** remove managers (`StarManager`, payments, gifts…) and their `td_api` methods. Every
  TDLib bump means a rebase, because `Td.cpp` and `MessageContent.cpp` call into these managers.
  Keep the `telegram_api` parsers: the server sends gift and payment updates anyway, and an
  unknown constructor breaks parsing.
- Decide from the probe's numbers: if QNX loads the whole binary into RAM, every MB of code counts
  against the 30 MB cap. If it pages code in on demand, unused code costs disk, not RAM.

## Battery: the reason this app exists (user, 2026-11-16)

The existing BB10 TDLib clients drain the battery. Tengram has to be much leaner, and the numbers
will prove it.

- **Baseline first.** With the same account on the same Q10, measure TbbX and Telega while idle
  in the background: CPU seconds per hour (`pidin times`), and network traffic per hour if QNX
  shows it per process. Tengram has to beat both clearly.
- **Background mode.** Set `online=false`. No requests from the engine. No auto-downloads, no
  preloading of stickers or emoji, no chat or history loads until the UI asks. Logging at level 0–1,
  never to a file on flash.
- **Notification delivery** is the hard trade-off:
  - **Instant:** keep TDLib's connection open. The cost is its ping keepalives, which wake the
    radio regularly.
  - **Batched:** `setNetworkType(none)` between syncs, then wake every N minutes and reconnect. A
    reconnect fetches only what changed, then shows notifications. Fewer radio wakeups, later
    notifications.
  - **Real push:** unlikely. TDLib has `deviceTokenBlackBerryPush`, but BlackBerry's push service
    most likely died with the BB10 infrastructure in January 2022. To check, not to build on.
  - Make the mode a user setting. Measure all three before choosing a default.
  - **Hybrid, the intended default (user, 2026-11-16):** instant while the UI is running and for
    10 minutes after it closes; after that, batched, one sync every 5 minutes. Both times are
    settings, as is the mode itself (instant always / hybrid / batched always).
    - Between syncs: `setNetworkType(networkTypeNone)`, so TDLib closes its connections and sends
      no pings. A sync is `setNetworkType(<real type>)`, wait for `updateConnectionState` Ready
      plus a short quiet period, post notifications, then back to None.
    - The UI opening, or the user sending anything, switches to instant at once.
    - The 5-minute timer is the engine's only timer. To check on the phone: whether a headless
      app's timer fires on time in deep standby, and whether the wakeup can be aligned with the
      system's own (BB10 batches radio wakeups; a sync that rides one is nearly free).
    - To measure with the probe: the cost of one reconnect+sync against 5 minutes of an idle open
      connection. Batched only wins if a sync is cheaper than the pings it replaces.
    - Calls would need instant mode. Not in v1.
  - **"Batched only while active elsewhere" (user, 2026-11-16):** a fourth mode. Detection: the
    engine sets `online=false`, so if `updateUserStatus` for the own user says
    `userStatusOnline`, another session (desktop, web) is in use. Instant → batched when that
    arrives; batched → instant when a sync finds the own status offline for longer than a
    grace time (setting, default 10 min). `getActiveSessions` (`last_active_date` per session) is
    the coarser fallback. To verify with the probe: that TDLib delivers the own-user status
    update while this session is offline, and how quickly it flips after the desktop goes idle.
    The cost: with no other session in use it holds the connection open all day, so it saves
    less battery than hybrid. Measure both.
- **Engine code:** no timers of its own. It wakes only for TDLib or the UI socket.
- The probe measures battery use alongside memory (`--background`, `--interval`, CPU time in
  every status line).

### Telega measured (2026-11-16, system monitor `sysmon2.db`, 24 h, 1.5 h screen on)

- Telega UI process: 24.2% of battery, 1820 CPU s. It uses 14–39 CPU s per hour with the screen
  off, and peaks at 160–260 MB of memory. Telega service: 2.1%, idle while the UI is alive; about 40 CPU s per hour
  once it runs TDLib alone.
- HyperBerry, same day: UI 1.2%, service 2.1%. **Target: HyperBerry's level, about 3% in total.**
- Telega: stock TDLib 1.8.67 as `libtdjson.so` (JSON interface), GCC 13.4, `poll`/`select` only,
  one TCP connection held open, `_sys_headless_xlarge` + `_sys_headless_nostop`.

- **HyperBerry is not a like-for-like baseline** (user, 2026-11-16): this Telegram account gets
  several messages a minute; WhatsApp gets far fewer. Same 24 h: Telega received 68 MB,
  HyperBerry 4.5 MB. So compare engines only on this account: Telega's service (about 40 CPU s
  per hour) is the baseline, and the probe reports CPU per update received and updates per hour
  by type. The 3% target stays a goal, not a pass mark.
- With this volume, instant mode wakes the radio, the CPU and the flash for every message, muted
  chats included. Batched mode and anything that makes muted traffic cheaper matter most.

- Most of the volume is muted groups and channels (user). On an open connection the engine can't
  filter them: Telegram sends everything, and the mute is applied after receipt. Two levers:
  - **Batched mode** receives the same messages in one burst per interval: one radio wakeup
    instead of hundreds, though the CPU and flash work per message is still paid.
  - **Decided (user, 2026-11-16): no relay.** The app is to be distributed, and nobody hosts a
    server for its users. Batched mode is the answer. **The default (final, user):** instant while
    the UI is open; with the UI closed, batched while another session of the account is active
    and instant otherwise, so calls and messages are never late when the phone is the only device
    in use. "Batched after a grace time even with no other session" (the earlier hybrid) stays
    as an opt-in setting for people who prefer battery over immediacy.
  - *Rejected:* server-side filtering through push. Telegram applies mutes
    before sending push notifications. TDLib has `registerDevice` with `deviceTokenWebPush`
    (an HTTPS endpoint plus keys) and `processPushNotification`. With a small relay we run, the
    engine could stay disconnected from Telegram and hold one cheap connection that carries only
    unmuted notifications. Open questions: whether Telegram accepts an arbitrary endpoint, and
    that it needs a server of ours.

## UI speed (user, 2026-11-16)

Telega's UI is heavy and many actions are slow. Tengram's UI must be lean, built on HyperBerry's:
- The UI process has no TDLib and no JSON parsing. It reads `chats.db` and does nothing when the
  screen is off (0 CPU s per hour in the background is the target).
- Keep HyperBerry's speed work (C++-painted message cards, demand-loaded lists, per-device sizing).
  Every new Telegram feature must fit that pattern, with no heavy QML per message.
- Measure the same actions in Telega and Tengram: cold start, opening a chat, scrolling history,
  sending, switching chats.
- **The engine does the syncing, never the UI (user, 2026-11-16).** In Telega the service only
  notifies; the UI process starts its own TDLib when opened and catches up, which takes up to
  10 s. In Tengram the engine is the only TDLib instance, stays running, and keeps `chats.db`
  current, so the UI opens onto finished data. In batched mode the data is at most one interval
  old; opening the UI switches to instant and the engine fetches the small remainder while the
  UI already shows the cache. Estimates are fine for the user; exact splits aren't needed.
- What the user finds slow in Telega, so these are timed first:
  1. **Opening a chat cold.** Tengram: the first screen of messages comes from `chats.db` in one
     query, with no round trip to the engine. The engine fetches newer and older messages afterwards.
  2. **Opening a chat from the Hub** (app not running). Tengram: the UI must show the chat from
     `chats.db` without waiting for the engine or the chat list. meegram2 measured 4.6 s here
     before its fix, so this path gets its own timing from the first build.
  3. **Scrolling long message lists.** Tengram: HyperBerry's C++-painted cards and paged loading;
     "load older" must fetch a page into `chats.db` ahead of the scroll position, so the list never
     stalls at the top.

## Engine ↔ UI data path (user asked for less data and no JSON, 2026-11-16)

HyperBerry's socket is already persistent and carries almost nothing: events are "re-read these
ids", batched at most every 50 ms; the data itself goes through `chats.db`. So the socket's JSON is
not the cost. Where JSON does cost, and what to do:
- **`messages.extra`** is JSON that the UI parses per message while painting and scrolling.
  Telegram adds text entities to it for most messages. Candidate: store entities as a compact
  binary blob (offset, length, type, optional argument), parsed without allocation. Measure the JSON version
  first in the scroll test.
- **Large replies** (`group.info` with every participant): for Telegram these become paged tables
  in `chats.db`, never one big socket reply.
- **Socket:** candidate swap of TCP loopback + token for a Unix domain socket with file
  permissions (no TCP stack, no token file). Small win, small change.
- A binary socket protocol is the last resort: only if the measurements show the control
  messages cost anything.

## TDLight (github.com/tdlight-team/tdlight)

Checked 2026-11-15: 25 commits of its own, 302 behind upstream, last commit 2026-10-19. So it
trails TDLib by a few weeks. The old "constant memory" cleanup is gone. What it adds: about 14
options, all off by default (it behaves as stock TDLib until one is set), and the method
`getMemoryStatistics`, which reports the size of each internal cache.

| Feature | For Tengram |
|---|---|
| `getMemoryStatistics` | the most useful part: shows which cache holds the memory during the cap test |
| `disable_group_calls` | on (no calls in v1) |
| `disable_top_chats` | on (unused) |
| `ignore_inline_thumbnails` | maybe: memory against the previews shown before a download |
| `disable_minithumbnails` | off: they make photos show at once |
| turning off `messages_db`, `users_db`, `files_db` | no: bot advice; a client needs offline history |
| `ignore_platform_restrictions`, `ignore_sensitive_content_restrictions` | off: a user client shouldn't lift Telegram's content limits |

Plan: build the first memory probe against TDLight, for `getMemoryStatistics` only. Ship stock
TDLib if it fits under the cap; switch to TDLight with the options above only if it's tight, so
the app never waits on TDLight to catch up.

## Fallback: gotd/td (github.com/gotd/td), user, 2026-11-16: "eventually"

A pure-Go MTProto client, not a TDLib port. Not checked in detail yet.
- **For:** full control of connections, pings and disk writes; a small binary; and HyperBerry's Go
  engine already runs on this phone at 18 MB and 2.1% battery, so a Go engine fits the cap
  (this corrects the "Go would eat the margin" line under risk 2).
- **Against:** it is the protocol only. No local database, chat list ordering, file cache,
  notification grouping or secret chats: everything TDLib does above MTProto becomes engine code.
  It also reverses the "100% C++" decision. The user accepts that (2026-11-16): battery and
  performance outrank the language, so gotd is allowed if the benchmarks point strongly to it.
- **When to switch:** only if the probe shows TDLib itself can't get near the target (idle CPU,
  cost per sync, flash writes) with TDLight's options and our settings.

## What carries over from meegram2

- The separate TDLib daemon (`meegramd`), same shape as HyperBerry's engine.
- `src/daemon/Notifier.cpp`: TDLib notification groups, written against TDLib, ports almost as is
  (D-Bus → BPS / Hub).
- The measured speed work, above all the demand-loaded chat list: a cold notification tap there
  waited 4.6 s for the chat to exist.
- `tools/setup-dependencies.sh`: TDLib pinned by commit, idempotent fail-loud patches, two-pass
  cross build, the `vector<bytes>` JSON patch if the JSON interface is ever used.
- Its CLAUDE.md "Hard rules" on object lifetime and threads (Client callbacks on the reader
  thread, hop to the GUI thread queued).

## Probe status (2026-11-16)

- **Risk 1 is closed.** TDLight `ccf8956` builds for BB10 with four small patches (`patches/td/`),
  `scripts/build-deps.sh` (about 4 min) and `probe/build.sh`. On the phone, `tdprobe` starts,
  creates its database, reaches `authorizationStateWaitPhoneNumber` in 0.8 s and closes cleanly.
  OpenSSL seeding, emulated TLS and the `poll` loop work.
- The binary is 23.9 MB stripped, with no LTO and nothing removed yet.
- On the phone: `/root/tengram/tdprobe-<sha8>`, `measure.sh`, data in `/root/tengram/data`.
- **Patch 0005 (poll busy loop):** TDLib's `Poll` backend asked for POLLOUT on every call, so a
  connected socket made it spin at 64% of a core. Fixed as `Select::run` does it; measured 0 CPU
  over 10 s while connected and waiting.
- **Login works (QR).** Code login failed: Telegram reported the code as sent in-app
  (`authenticationCodeTypeTelegramMessage`, no SMS fallback) and it never arrived. `--qr` plus
  `probe/login-qr.sh` logged in on the first scan. The app needs QR login from day one.
- **First sync on the user's account (580 chats), one run:** Ready 18 s after the QR was shown;
  500 chats loaded in 13.2 s; 961 messages of history from 20 chats in 27.8 s; 5834 updates;
  31.9 s of CPU in 59 s; `close` took 15.7 s; database 11.6 MB + 1.7 MB binlog. One-time costs,
  but the 15.7 s close and the 28 s for history need a look.
- On the phone: `/root/tengram/tdprobe-f73bb074`, logged in, data in `/root/tengram/data`.
- Next: the `--background` and `--batch 300` runs with Telega closed.

## Probe results, 2026-11-16 evening (phone on battery, screen off, Telega stopped)

One run of each, same account (about 1,000 updates per hour, of which about 130 new messages).

| | Instant (`--background`) | Batched (`--batch 300`) | Telega |
|---|---|---|---|
| CPU once settled | 10.5 s per hour (1.0–2.3 s per 10 min) | about 4–7 s per hour (0.03–0.8 s per sync) | 15–40 s per hour |
| Connection | held open | 3–5 s every 5 min; reconnect takes 0.2–0.9 s | held open |
| Whole-phone drain, screen-off samples | about 170 units per 10 min (3 samples) | about 126 per 10 min (5 samples) | – |
| Memory (`pidin` data column) | 69 MB | 41 MB | 33 MB (service) |

- **TDLib on our build is not the battery problem**: idle instant mode costs less CPU than
  Telega's, and batched about half of that again. The early "3–5 s per minute" was the session
  settling after its first login; it took about 20 minutes.
- **Timers fire on time in standby**: 12 syncs, each 300 s after the last to within 30 ms.
- The first three syncs after the restart hit the 60 s cap (backlog); the rest took 3–5 s, 3 s of
  which is the probe's own quiet wait.
- Whole-phone drain is lower in batched mode by about a quarter, but the instant samples are
  few and include the settling period: indicative only. The phone's monitor counts the probe
  under "System" (root process), so there is no per-app share.
- Memory is above Telega's and above the 30 MB figure. The probe ran as a root process, not as
  a headless app, so the cap itself is still untested.
- **Start-up, settled session** (`--first-page`, 3 runs): Ready 0.35–1.1 s after launch, first
  chats 35–70 ms later, 0.35–0.7 s of CPU, clean close in 0.3 s. Faster than meegram2's 5 s. The
  earlier 40 s was the whole chat list on a two-minute-old session; the whole list (578 chats)
  on the settled session took 14 s, almost all CPU, which is why the engine must load it on
  demand as meegram2 does. `loadChats(20)` returned after 2 chats, so page size needs a look.
- **Thread-locals** (`tlsbench`, 2 runs each): 66 ns per access with the old runtime, 33 ns with
  the new `src-rt/emutls.c` in bb10-cxx20 (image tag `bb10-cxx20:emutls`, uncommitted). The old
  runtime also fails three checks: `thread_local` destructors do not run at thread exit. The new
  one passes all. TDLib and the probe are still built with the old runtime.
- Draft for LTO in the toolchain: `docs/drafts/bb10-cxx20-lto.patch` (tested in a container only).

## Build for speed and the start-up profile (2026-11-16, late)

- **Default build now:** TDLib `Release` (`-O2`) + thin LTO, OpenSSL `linux-armv4` with NEON assembly
  at `-O2` (`patches/openssl/0001-qnx-armcap.patch`: no SIGILL probing, it leaves the thread
  without an FPU on BB10), toolchain image `bb10-cxx20:emutls-lto` (new emulated TLS; LTO through
  LLVM's linker plugin). Binary 36.6 MB stripped (23.9 MB at `-Os` without LTO).
- **Crypto on the phone** (`cryptobench`, plain C → NEON assembly): AES-256-CTR 18 → 59 MB/s,
  SHA-256 41 → 113, SHA-512 28 → 63, 2048-bit modexp 13 → 58 per second.
- **The "full chat list" test measures two different things**, depending on whether the network
  connects before the list is loaded:
  - list loaded first (no `connectionStateReady` yet): **2.3 s of CPU for 579 chats**, same with
    and without patch 0006;
  - connection first: 8.4–10.9 s of CPU. The extra 6–8 s is TDLib's start-up sync, and one item
    dominates: `StickersManager.cpp:5341` logs "Reload installed Regular sticker sets, because
    they have changed" at **every** start. Same 189 sets, different order from index 43, so it
    refetches and rewrites all of them into the encrypted database. To fix: find why the order
    differs each time.
- On the connection-first runs: old build (`-Os`, C crypto) 12.2 s mean, new build 9.6 s
  (5 runs each), 9.1 s with patch 0006 (5 runs). The LTO-at-`-Os` build was slower (14.4 s).
- **Stack sampling** (`pidin -p <pid> -F "%b %J %q"` in a loop, addresses resolved against
  `build/tdprobe.unstripped` and the phone's `/proc/boot/libc.so.3`): during the slow runs the
  threads are in `MsgSend`/`MsgSendv` from `read`, `lseek64`, `write`, `fcntl`, `fsync` (SQLite
  I/O, each a message to the filesystem process) and in `sha512_block_data_order_neon` and AES
  (SQLCipher per-page HMAC and decryption). A running thread reports its last kernel entry, so
  system calls are over-counted.
- **Patch 0006** (`cache_size` 8 MB per connection, `cipher_memory_security = OFF`, `USE_PREAD`):
  no effect on the local load, perhaps 5–15% on the sync. Its memory cost is unmeasured. Keep
  only if a memory run justifies it.

## Database encryption (user, 2026-11-16)

- **Decided: encrypted by default.** A setting turns encryption off, behind a prominent warning;
  it applies at the next engine start (TDLib converts the database either way at open:
  `SqliteDb::change_key`).
- To build: a patch that makes encryption alone switchable. Today an empty key still encrypts
  (fixed key "cucumber"); TDLight's `use_custom_database_format=true` in the database path gives
  an unencrypted database but also sets `journal_mode=OFF` and `synchronous=OFF`, which must not
  ship on a phone.
- **Decided (user, 2026-11-17): `chats.db` is encrypted too; security comes before speed here.**
  HyperBerry's UI opens it with Qt's `QSQLITE` driver (system SQLite, no cipher), so Tengram's UI
  needs a driver built on SQLCipher 4 (Qt 4's `qsql_sqlite.cpp` compiled into the app against an
  SQLCipher amalgamation, registered under its own name). The engine can use TDLib's own
  `td::SqliteDb` (SQLCipher 4.4.0, already linked) for `chats.db`. Both sides open with a raw
  key (`x'<64 hex>'`): a passphrase costs SQLCipher's 256 000 PBKDF2 rounds at every open.
  Estimated cost per uncached 4 KB page from the phone's crypto figures: about 0.15 ms.
- **What the key is today, and why that is not yet security.** TDLib encrypts SQLite with a
  random key kept in `td.binlog`, and encrypts the binlog with the key the app passes. With no
  key passed, that is the constant `"cucumber"` from the public source. So the default protects
  nothing from someone who has the files. Real protection needs a key that is not on the phone:
  a passcode. **Decided (user, 2026-11-17): C.** The options were:
  - A. Key on the phone (TDLib's default, or a random key file in the app's data folder). The
    engine starts by itself after a reboot. Protection is the phone's own lock and file
    permissions, nothing more.
  - B. Passcode typed in the UI, key derived from it once per engine start. Real protection for
    a lost phone or a copied backup. After a reboot or engine restart there are no notifications
    until the passcode is entered.
  - C. A by default, B as a "passcode lock" setting (what Telegram's own clients do). **Chosen.**
    Default: TDLib's own default key for its files, and a random key file in the app's data
    folder for `chats.db` (engine and UI both read it). With the passcode lock on: both keys are
    derived from the passcode, the UI asks for it and hands it to a waiting engine, and turning
    the lock on or off is `setDatabaseEncryptionKey` plus a rekey of `chats.db`.
- **Patch `0007-plain-database-keeps-journal`** is the "encryption off" switch: TDLight's
  `?use_custom_database_format=true` on the database path with an empty key gives an unencrypted
  binlog and SQLite, and with the patch it keeps WAL and sync on QNX and opens a binlog still
  under the default key (it is rewritten plain; SQLite is converted at the same open). Back to
  encrypted: open without the option. From a passcode key, call `setDatabaseEncryptionKey("")`
  first. Probe flag: `--plain`.
- **Tested on the phone, 2026-11-17, on a copy of the session** (probe `tdprobe-0fb2c739-1`):
  encrypted → plain at open works (10.5 s once, 2.5 s of CPU, 25.8 → 23.9 MB; header reads
  "SQLite format 3"), plain → encrypted works (one start, 11.8 s of CPU with the list load).

  | | Encrypted | Plain | Saved |
  |---|---|---|---|
  | Start to first page, wall (5 runs each, first cold run left out) | 373 ms | 306 ms | 18% |
  | Start to first page, CPU | 303 ms | 219 ms | 27% |
  | Full chat list, connected, CPU (4 runs each) | 8.6 s | 5.8 s | 32% |

  So encryption costs about a quarter to a third of the CPU on database-heavy work. Idle cost
  per update was not measured plain. **Open, the user's call:** keep the toggle (patch 0007, a
  setting, a warning screen) or ship always-encrypted.
- Sticker reload at start: cause found. One installed set loaded before the installed list
  (position 43 of 189) puts the temporary list out of order; TDLib then refetches all sets, and
  with a zero hash, so the server sends everything. **But it is not the 6–8 s:** with the reload
  patched out (log line gone), connected starts took 9.7–9.9 s of CPU against 8.4–8.7 s without
  the patch (6 alternating runs each). The patch is parked in
  `docs/drafts/td-stickers-no-reload-on-order.patch`, out of the build, until that is understood.
- **What the 6–8 s is:** in connected runs `loadChats` itself only finishes about 9 s after
  `connectionStateReady`: once online, loading the *whole* list means syncing its tail with the
  server and writing the result to the database. An engine that loads the list on demand does
  not pay this at start. The test is not worth more tuning; the numbers that matter are start
  to first page (0.35 s), idle CPU per hour and memory, to be re-measured on the `-O2` build.

### One hour idle on the `-O2` build (run 3, 2026-11-16 23:11 to 00:16, plugged in, instant mode)

| | `-Os` build, run 1 | `-O2` + LTO + NEON crypto + patch 0006, run 3 |
|---|---|---|
| CPU over 50 idle minutes | 8.8 s (10.5 s/h) | 1.6 s (1.95 s/h) |
| Updates in those 50 minutes | 819 | 401 |
| CPU per update | 10.7 ms | 4.0 ms |
| Memory (`pidin mem` data) | 69 MB, flat | 47 MB, flat |

Run 3 was at night with half the traffic, and run 1 loaded chat history at start, so compare
per update, and read the memory gain with care. Telega's service: 15–40 s/h. Patch 0006's own
memory cost is still unmeasured (no run without it on this build).

## Engine start-up time: resolved, see above (2026-11-16)

**Observed:** on the phone, the probe's second start took 40 s to finish loading the chat list
(515 chats, 9 `loadChats` calls) and used 37 s of CPU in its first minute. meegram2's engine on the
N9, a much weaker CPU, starts in about 5 s (user), even on an old session. While idle and
connected the probe then used 3–5 CPU s per minute, falling; Telega's idle cost on this account is
15–40 CPU s per hour.

**Why the 40 s is not comparable to the 5 s:**
- The probe loads the whole chat list. meegram2 loads one page on demand
  (`docs/architecture.md` there records "load every chat before…" as the old, slow behaviour).
- The session was two minutes old. TDLib was still catching up after the first login (the
  database grew from 11.6 to 24.8 MB in 10 minutes). The N9's session has been settled for weeks.

**Build differences that could make ours slower for the same work, unmeasured:**
1. No LTO (`-DTD_ENABLE_LTO=OFF`); meegram2 has it on. Same `MinSizeRel`.
2. Emulated TLS (below). The N9 has native TLS.
3. `poll` backend with patch 0005 against `epoll` on the N9. Measured at 0 CPU idle, not
   under load.
- Not a difference: OpenSSL is `linux-generic32` (C, no assembly) in both.

**Emulated TLS, what it is:** QNX 6.6's loader has no ELF TLS, so `bb10-clang` compiles with
`-femulated-tls`. A `thread_local` read is then a call to `__emutls_get_address`, which looks up
a per-thread table through `pthread_getspecific`, instead of two or three inline instructions.
TDLib's actor framework reads `Scheduler::scheduler_` and `context_` (both `TD_THREAD_LOCAL`)
on almost every actor operation; 32 source files use thread-locals.
- Telega has the same constraint: its `libtdjson.so` imports `__emutls_get_address` (GCC's
  emulation). So emulated TLS doesn't stop a TDLib build from idling at Telega's level.
- Toolchain-side options, only if a profile points here: a faster `__emutls_get_address` in
  `libbb10-cxx20-rt.a` that reads QNX's per-thread key table directly; real ELF TLS is not
  available without loader support.

**Plan, in order:**
1. Add `--first-page` to the probe: `loadChats(chatListMain, 20)` once, print the time, stop.
   That is what meegram2 does at start.
2. Run it on the settled session, after the two queued runs. This is the number to compare with
   5 s.
3. If still several times slower: rebuild with LTO, measure; then profile before touching TLS.
4. Read the idle run's status lines for where the 3–5 s per minute goes (update counts by type).

## First step

The TDLib-on-QNX probe: `scripts/build-deps.sh` (OpenSSL, zlib, TDLib with `patches/td/`) in
the `bb10-cxx20` image, then a static `tdprobe` on the Q10 that sets the TDLib parameters, reaches
`authorizationStateWaitPhoneNumber` over the network, logs in a real account, and records RSS
(`pidin`), `getMemoryStatistics`, binary size and start time, running headless with
`_sys_headless_xlarge`. That answers risks 1 to 3 before any app code.

Order after it: the engine with login, chats, messages and history paging; then media and the
message kinds; then the rest. If the probe passes, a rough guess at HyperBerry's pace is two to
four weeks to HyperBerry's features minus calls.

## Engine: the plan being built (2026-11-17, unattended session)

`tengramd`, C++, in `engine/`. Contract: `docs/protocol.md` (HyperBerry's protocol and tables
with Telegram's meanings). One commit per step.

1. Skeleton: the loop, the TDLib thread, login (`link.*`, QR and phone), the socket server,
   encrypted `chats.db`, `tests/dbq` (reads the encrypted database, as the UI will) and
   `tests/engine_client.py` (talks to the engine on the phone through an ssh tunnel).
2. Chat list: TDLib's main and archived lists mirrored into `chats`, names, chat photos.
3. Messages: live updates into `messages`, `chat.open`, `chat.history`, `msg.send`, `chat.read`,
   `chat.set`, `chat.typing`.
4. Delivery mode: `ui.state`, `prefs.set`, batched syncs.
5. Measure on the phone: one idle hour and memory against the probe's 1.95 s/h and 47 MB.

Decisions taken for it, with the reasons:
- **TDLib's SQLite and JSON are used directly** (`td::SqliteDb`, `td/utils/JsonBuilder.h`):
  both are already linked, so `chats.db` gets SQLCipher with no second SQLite and no JSON
  library. Checked on the phone (`build/spike`). The price: the engine includes TDLib-internal
  headers, tied to the pinned commit.
- **`chats.db` is a cache**, unlike HyperBerry's (TDLib and Telegram hold the data). A failed
  open or another `user_version` deletes and rebuilds it. No migrations.
- **The whole chat list is loaded once, at the first start after a login; later starts load one
  page.** Measured on the phone (step 1, 2026-11-17): TDLib with no list loaded holds 9 MB of
  data, against 47 MB with all 576 chats loaded. Which chats are in the main or archived list
  comes from `chat.chat_lists_` and `updateChatAddedToList`/`RemovedFromList`, not from position
  orders: TDLib reports order 0 for every chat below the loaded part. A chat below the first page
  still gets its `updateNewChat` and updates when something happens in it, so its row stays right.
- **History is verified page by page.** Live messages are written for every chat so a cold
  open paints from `chats.db` at once, but a channel can lose messages silently after a long
  offline gap (TDLib skips them). So `chat.open` and `chat.history` make each page exactly
  TDLib's and tell the UI the oldest verified id; older rows are shown only once covered.
- **Replies can come in any order** (TDLib answers asynchronously), matched on `"@"`.
- **Not in these steps:** Hub notifications and the headless packaging (BB10 platform work),
  media download, text entities, reactions, the passcode lock, pruning old message rows.
- **Testing on the real account, safely:** sends, reads and chat settings are only tried on
  Saved Messages; `link.logout` is never run (it would end the session).

### Engine progress, 2026-11-17 night (each step is one commit; all tested on the phone)

- **Step 1** (`079d989`): loop, TDLib thread, login, socket, encrypted `chats.db`. Ready 0.34 s
  after start on the existing session; clean stop on SIGTERM.
- **Step 2** (`37712c3`): chat list. 468 listed chats (TDLib announces 577: the rest are in
  no list). Finding: TDLib announces a chat it is still loading without its last message and
  sends it about 60 ms later; the engine keeps the known preview, or every start rewrote those
  rows and the list would flicker.
- **Step 3** (`cfa437a`): messages. `chat.open` on Saved Messages: 50 verified messages in
  `chats.db` 0.44 s after the request (first time, over the network). `getChatHistory` with
  offset 0 does not return `from_message_id` itself, and its first answer is often one message.
- **Step 4** (`2ff0ca0`): delivery modes. Batched on the phone: a sync exactly 60 s after going
  offline, 2.3 s long (ready after 0.2 s, then the 2 s of quiet). UI to the front while offline:
  TDLib starts connecting after 50 ms (2 of 4 tries) or 1.8 s (2 of 4); not yet understood.
- **Memory:** 13–15 MB of data (`pidin mem`) with the chat list loaded or not. The probe's 47 MB
  was not the chat list; TDLib announces nearly every chat at start even with one page asked for,
  so "one page at later starts" saves little. The engine is well inside a 30 MB cap.
- **A quiet account at night:** 7 new messages in 13 minutes at 01:50, so the night's idle
  figures are a floor, not a typical hour.
- **Unverified:** auto mode's detection of another active session (needs the user on another
  device: the log line `own status:` shows it); `link.logout` (never run, it would end the
  session); timers in deep standby with the phone unplugged.
- **Left on the account by the tests:** two messages in Saved Messages ("Tengram engine test…").

### The UI can read the encrypted `chats.db` through QtSql (spike, 2026-11-17)

`scripts/build-qtsqlcipher-test.sh` builds, in the Native SDK image the UI is built with (qcc),
Qt 4.8.6's own SQLite driver (two LGPL files fetched from Qt's repository, class renamed to
`QSQLCipherDriver`) on TDLib's SQLCipher amalgamation and our static `libcrypto.a`.
`ui/sqlcipher/` holds the pieces: the name mapping (TDLib prefixes SQLite's names with "td"),
the ARM `__aeabi_mem*` helpers clang-built objects need under qcc, and a test program.
- On the phone, on a copy of the engine's database, in a fresh process each time: open and key
  5 ms; the chat list's first 30 rows 52 ms; a chat's newest 20 messages 38 ms; counts right.
- HyperBerry's models keep their `QSqlQuery` code: the fork registers the driver
  (`QSqlDatabase::addDatabase(new QSQLCipherDriver, name)`) and runs `PRAGMA key = "x'<key>'"`
  as the first statement of each connection.
- To do when the UI is forked: time the same queries on a warm connection, and against the
  unencrypted figure HyperBerry has.

### One idle hour of the engine (run 4, 2026-11-17 02:06 to 03:16, plugged in, instant mode)

| | Probe, `-O2` build (run 3) | Engine, step 4 build (run 4) |
|---|---|---|
| CPU over the 60 idle minutes after the first 9 | 1.6 s per 50 min (1.95 s/h) | 0.57 s (0.57 s/h) |
| Memory (`pidin mem` data) | 47 MB | 14 MB, flat |
| New messages in the run | not counted (401 updates) | 19 |

The engine does more than the probe (it writes every message and chat change into the encrypted
`chats.db`) and still used less, but the hour was the quietest of the night, so this is a
floor. Start-up costs 3–4 s of CPU, once. Telega's service: 15–40 s/h. A daytime hour, on
battery, is still to be measured (run 7 was started at 06:28 for six hours with the final
build, default mode, sampled every 5 minutes: `run7-engine*.log` in `/root/tengram`).

### Steps 5, 6a, 6b and a fix (2026-11-17 morning)

- **Step 5** (`48e0cae`): `msg.delete`, `msg.edit`, `media.get`. On the phone: an edit, a photo
  of Saved Messages downloaded (row `media_state` 2 with its path), the test messages deleted.
- **Step 6a** (`d8cb597`): the notifier on TDLib's notification groups, Hub backend that logs
  (`hub:` lines). No notification arrived during the night's tests; run 5's log will show the
  first real ones.
- **Step 6b** (`bff3bbb`): `tengramd <data> --service` for the packaged app: the Hub through
  `dlopen` of `libunifieddatasourcec.so.1` (ported from HyperBerry's `hub_qnx.go`), a BPS thread
  for `NAVIGATOR_EXIT`, Hub actions (Mark as read, Mark as unread, Delete) and network status.
  **Compiled only.** Without the flag none of it runs and the binary needs no Qt library.
- **Fix found on the phone** (same commit): after `msg.send`, Telegram reports our own account
  online for 300 s. Auto mode read that as "another device is active", went offline, and the
  next requests hung. Now a request in the last 10 s, or an answer TDLib still owes, keeps the
  network on and brings it up, and our own online status is not believed meanwhile.
- **To verify on the phone once the app is packaged** (none of it could run from a root shell):
  - the app's public assets hold `hub_account2.png`, `hub_unread2.png`, `hub_read2.png`,
    `tick_action.png`, `unread_action.png`, `delete_action.png`;
  - `dlopen` of the UDS library works through the bar's library path, with the Hub permission
    (`_sys_access_pim_unified`), on a thread with no Qt event loop;
  - the account and its three actions register, and a fresh registration is reported;
  - the Hub actions arrive at `com.zsavajji.tengram.service`, and where the item id sits in
    them (the engine logs each invocation whole);
  - `NAVIGATOR_EXIT` stops the engine in time; `NETSTATUS_INFO` arrives with the right type;
  - items alert or stay silent as meant; the headless memory class (14 MB measured as root).
- **Bar descriptor to write with the UI fork:** ids `com.zsavajji.tengram` (UI) and
  `com.zsavajji.tengram.service` (engine, started with `--service`), item MIME
  `application/vnd.zsavajji.tengram.chat`, as HyperBerry's descriptor has them.

### What happened to the unattended night

The Mac went to sleep at about 02:35 and only woke for moments until 05:54, which froze the
sub-agent's build and OrbStack. From 05:54 `caffeinate` kept it awake; OrbStack was stopped
and started again with `orbctl` (it had no containers). About three hours were lost.

### Next, in order (superseded: see `docs/HANDOFF.md` §7)

1. Read run 7's log: `hub:` lines against what really notified, `own status:` lines against
   when another device was in use (the one unverified part of auto mode).
2. The user's calls: keep or drop the "encryption off" toggle (patch 0007); how the passcode
   lock should behave (option C's second half).
3. Fork HyperBerry's UI: the SQLCipher driver is ready (`ui/sqlcipher/`), the login pages need
   Telegram's states (`docs/protocol.md`), Stories and Calls go.
4. Package (`bar-descriptor.xml`, engine with `--service`), the user installs it, and the list
   above is checked on the phone.
5. Engine, later: text entities, reactions, pruning old rows, calls' notifications.
- **Login path on the phone** (2026-11-17 06:15, a second engine on an empty folder, removed
  afterwards; nothing was logged in): `hello` gives `unlinked`; `link.phone` with a bad number
  gives `bad_phone` from Telegram and the state returns to `unlinked`; `link.qr` gives a `link`
  event in state `qr` with the `tg://login?token=…` link 0.2 s later. Not tried: a real code
  (it would have sent one to the user's phone at night), the password step, `link.logout`.

- **Smoke test** (`tests/engine_smoke.py [--batched]`, commit `82b6252`): tonight's manual checks
  in one command, in Saved Messages only; all pass on the phone. It found that after the busy
  window nothing re-evaluated the mode (fixed in the same commit).
- **Unexplained, once:** in run 5 an own-status update was ignored as "our own doing" with no
  client request in the 10 s before, so some TDLib request with a callback was unanswered at that
  moment. The log now names such requests by TDLib type id (`mode: busy with … of types …`).
- **Step 7** (text formatting, `extra.ent`): on the phone 91 of 685 stored messages carry it.
  Stored as JSON for now; the compact binary form stays a candidate until the UI's scroll test
  says whether parsing it costs anything.
- **Auto mode's detection verified** (2026-11-17, run 7): the user opened Telegram on another
  phone at 07:16. The engine logged `own status: online, expires in 300 s` at 07:16:25 and went
  offline; at the sync five minutes later it got `online, expires in 199 s`, then `offline`,
  and stayed connected (instant). So Telegram does send our own status to a session that is
  itself offline. Still unverified: the notifier on real traffic (no notification had come by
  07:33), `link.logout`, everything under `--service`.
- **Run 7, stopped by the user at 07:33** (default mode, plugged in, Telega running beside it
  from some point): 1.27 s of CPU in the 60 minutes from 06:33 to 07:33, 14 MB, 24 new messages
  in the whole run, one five-minute batched period. No notification was logged (`hub: item`
  0 times), so the notifier is still untested on real traffic. Nothing of Tengram's is running
  on the phone now.

### Decisions, 2026-11-17 morning (user)

- **Always encrypted, no toggle, no passcode.** One key, 32 bytes from `/dev/random` at the first
  launch, kept in `data/db.key` (mode 0600), for `chats.db` and for TDLib's database
  (`database_encryption_key`). Patch 0007 and the probe's `--plain` are gone. A TDLib database
  made before the engine had its own key opens under TDLib's default key and is moved to ours
  at once (`setDatabaseEncryptionKey`). This supersedes the toggle and the passcode lock
  (option C) recorded above. What the key protects: the files alone are useless without
  `db.key`; someone with both, or with root on the phone, reads everything.
- **UI:** fork HyperBerry's as it is, accent `#24A1DE`, no tabs for now (Calls comes back later).
- **Packaging** comes later.
- **Icons:** a monochrome glyph of the app icon, grey for the Hub and white for the lock screen,
  made the way HyperBerry makes its own.
- **The key change on the phone** (2026-11-17 08:10): first on a copy of the session, then on
  the real one. First start: `made the database key`, `chats.db` rebuilt, TDLib's database
  opened under its default key and moved to ours; second start: opens directly, ready in
  0.36 s; smoke test passes. The probe (`tdprobe`) passes no key, so it can no longer open this
  session: use the engine.

## 2026-11-17 afternoon: the UI on the phone, the memory cap

The UI's step 2 (login pages, history paging, the chat list in front only), opening a chat at its first unread
message with a window of history for long-unread chats, and packaging: commits and details in
`docs/HANDOFF.md` §6, what to test in `docs/ui-fork.md` §8b. The plan from here is `docs/HANDOFF.md` §7.

- **The test phone is a BlackBerry Classic** (SQC100-1, OS 10.3.3.3216), read from BPS `deviceinfo`; earlier
  notes say Q10. The engine now tells Telegram the device it reads (`device_model` "BlackBerry Classic").
- **The first install** (0.1.0.1 to 0.1.0.8, root installer job, each at the user's ask). The navigator starts
  the headless entry point with one argument, `invoke://localhost` (launcher.cfg: `cmd_template=%b %a`):
  0.1.0.2 took it for the data folder and exited unseen, since a headless app's stderr goes nowhere. The
  service now uses `HOME` and logs to `logs/tengramd.log`; test runs pass `--data`.
- **QR login works** (the user scanned from Telega): `authorizationStateReady` 40 s after the start. The first
  full sync: 467 chats in 12.9 s. Chat list reloads cost 125 to 277 ms for 200 to 467 chats (log), so moves
  need a patch too (§3.8's question answered).
- **The headless memory cap is 30 MiB** (`RLIMIT_DATA` soft = hard), from `_sys_headless_xlarge` in
  `/etc/authman/sys.acl` on the read-only OS image. Under it the first sync died with `bad_alloc` every time,
  while TDLib reloaded the installed sticker sets. Raising it: `../rlimit-patch` (patches authman's in-memory
  `RLM` text at every boot, as a root addon). Restarting authman broke every app launch until a reboot (its
  clients never reconnect); authman refuses `RLM` in a capability loaded at run time; an overlay before
  authman needs root too early. With 128 MiB the engine gets through the first sync.
- **The navigator keeps an app's memory limit from its first launch** until every process of the app has
  ended (seen: an engine restarted under the open UI kept 30 MiB; with both ended, 128 MiB).
- **The app installs the patch itself** (0.1.0.7, verified on a clean boot): a one-button dialog, the addon
  through AutoRoot v4's `__root` and `tg-install-patcher` (VoLTE Manager's NEWAPP design), then "Close Tengram".

### Decisions, 2026-11-17 afternoon (user)

- A chat opens at its first unread message (Telegram's way), with a window of history when the unread ones
  don't fit; search and pin jumps leave the window.
- Every user is rooted: root-side fixes are product routes. The memory patch is installed by the app, with no
  "No": the engine doesn't fit without it, and refusing root means not using Tengram. No login on an
  unpatched engine.
- The engine never anchors `chat.newer` on the chat's last message (TDLib's `CHECK(!have_a_gap)` aborts).
- Engine restarts keep the chat list on screen, with a spinner in its title bar.

## Calls (2026-11-19, branch `feature/calling`)

**The stack (the user's choice):** Telegram's own, tgcalls + tg_owt (Telegram's WebRTC fork), ported to QNX.
Checked that day in the official apps' sources: libtgvoip ("2.4.4") is offered by none of Android, Desktop
and iOS any more; tgcalls v1 (2.7.7, 5.0.0) still is by all three; 7.0.0 and up need WebRTC (DTLS-SRTP, SCTP).
Pins: tg_owt `020feec5`, tgcalls `1c236c09` (what Telegram Desktop builds). `scripts/build-calls.sh`,
`patches/tg_owt/0001-qnx-port.patch` (8 small changes), `calls/` (the CMake project and the QNX platform:
no video codec). Clean build 2 min 40 s; libtg_owt.a 26.6 MB, libtgcalls.a 4.2 MB; the stripped engine
goes from ~37 to 44.7 MB.

**Measuring CPU on QNX:** pidin's times are wrong for a thread that wakes every 10 ms (QNX charges CPU by
clock tick): it said ~28 % of a core for an idle call instance that really used 4-6 %. `tests/cpu_spare`
(two lowest-priority spinners; the drop against an idle run is the cost, 200 % = both cores) is the
measure. A /proc PC sampler was tried and dropped: a held thread shows as stopped whether it was running
or asleep in a kernel call, so it can't attribute CPU.

**Gate T0, passed on the Q10 (0.1.0.35, 2026-11-19).** The Q10's engine (the user's second account) called
the main account (Android) and answered a call from it; the test tone came through and the peer's voice
reached the engine (rms 3-11 silent, 100-2000 speaking). Both calls: version **8.0.0** (tgcalls v2), p2p
allowed, 1 reflector endpoint, 6 servers. Outgoing: key exchange to active 2.0 s after the answer.
Incoming: `call.answer` to active 0.7 s. Hang-up from either side ends as `hangup`; Telegram's call
message arrives in the chat. Engine data 18 MB during a call, 17 MB after. **CPU: ~25 % of one Krait core
for the whole phone during a call** (spare 1.148e9/s idle against 0.996-1.005e9/s in two calls) against
4-6 % for an instance with no peer: Opus at 48 kHz both ways, NS and AGC, encryption, the network stack.
To find out where before the audio link. tgcalls v2's FinalState traffic counters are 0.
- **Incoming calls need the engine online:** in batched delivery (the main account online elsewhere) the
  Q10 was offline and only saw the ringing call at its next connection (108 s later, the caller still
  waiting). The Q10 was set to instant for the test (`prefs.set mode instant`).
- **Not verified yet:** echo with real audio (WebRTC's software AEC is off, the iOS way, on the guess that
  BB10's voice path cancels echo itself); Opus complexity 5 (as Android) against the default 9.
- **Incoming calls and the delivery mode (the user, 2026-11-19):** no special case. Ringing in batched mode would
  mean keeping the connection open, which is instant mode; in batched mode a call rings on the other devices and
  shows here as missed.

**Where a call's CPU goes (2026-11-19, the Q10, `tests/calls_check`):** the bench is a whole call on the phone,
two tgcalls ends connected peer to peer over the phone's own network, each sending a tone; CPU from 20 s windows
(shorter ones vary by several points, and runs drift by ±4 even interleaved). Both ends together, version 8.0.0:
- 42.6 % at 48 kHz, 39.5 % at 16 kHz (consistent): one end ~20 %, as the real call's ~25 % for the whole phone.
- Microphones muted (no audio flowing): ~10 %. The cost is the audio pipeline, not the connection.
- Outgoing audio processing off (tgcalls v2 forces echo cancellation and noise suppression on, whatever Config
  says): −6 to −15 points. The one large knob; whether BB10's voice path cancels echo and noise itself decides it.
- Opus complexity 0, 1, 5, 10: no consistent effect.
- A profile (a /proc sampler that reads each thread's state before holding the process, so only running threads
  count; spinners filtered out) **is biased and misled twice**: the sampler wakes on the clock tick, as the 10 ms
  audio threads do, so it over-counts what runs right after a wake (`TimerTimeout`, the first step of each frame:
  first the noise suppressor, ~31 %; with it off, `RmsLevel::Analyze`, 18 %). A/B runs with the spinners are the
  measure: **noise suppression off saves ~1 point** for both ends (42.6 → 41.3 %), gain control in Android's
  fixed-digital mode 0, all outgoing processing off ~10. Noise suppression stays off on QNX (the user's ask).
- **libopus is fixed point now** (`--enable-fixed-point`, NEON intrinsics and ARM asm; Android and WebRTC build it so
  on ARM): the float build's `silk_*_FLP` functions were in the profile. Small, steady gain; voice notes pass
  `voice_check`.
- Versions: 7.0.0, 8.0.0, 9.0.0, 12.0.0, 13.0.0 cost 39-41 %, 10.0.0 52 %, 11.0.0 60 % (PeerConnection, DTLS-SRTP).
  **tgcalls v1 (2.7.7, 5.0.0) crashes once media flows** (Memory fault): no longer offered; every current Telegram
  app speaks 7.0.0 or newer.
- **Next:** the audio link at 16 kHz (HyperBerry's `CallAudio` rate; ~1.5 points an end cheaper), then a real-mic
  call to decide the software processing; the audio link to the UI (HyperBerry's `CallAudio`, at 48 kHz 10 ms or
  resampled); the call screen, ringing (Hub item, mm-renderer ringtone) and the Calls list from HyperBerry;
  an engine stop or logout during a call.

