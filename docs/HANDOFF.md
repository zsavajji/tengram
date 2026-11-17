# Tengram: handoff, 2026-11-17

For whoever picks this up next. It says what exists, what is proven and what is not, how to
build and test, what the user has decided, and what to do next. The details are in three other
files, which this one points into rather than repeats:

- `NOTES.md` — the plan and every measurement and finding, in the order they happened.
- `docs/protocol.md` — the contract between the engine and the UI. It changes with the code.
- `docs/ui-fork.md` — the survey of HyperBerry's UI, the traps of each change, the migration map.

## 1. What this is

A Telegram client for BlackBerry 10 (the user's Q10 is the test phone), in two processes:

- **`tengramd`, the engine** (`engine/`, C++20, clang through the user's `bb10-cxx20` toolchain):
  the only process that talks to Telegram, through TDLib (TDLight, pinned). It keeps an
  encrypted `chats.db` current and serves the UI over a loopback socket.
- **`tengram`, the UI** (`app/`, Cascades, Qt 4.8, QML and C++98, built with qcc): a fork of the
  user's WhatsApp client HyperBerry (`../hyperberry`). It reads `chats.db` and sends commands.

**Why it exists:** the other TDLib clients on BB10 drain the battery and their UI is slow.
Battery and speed come first; everything is measured on the phone.

## 2. Working with this user

These came from the user during the work, and from `~/.claude/CLAUDE.md`. They are not optional.

- **Delegation:** the main model plans and verifies; sub-agents carry out a decided plan
  (`sonnet` for new code from a detailed brief). Up to 2 at once was allowed on 2026-11-17; the
  standing rule is one. Never delegate debugging or design. Review every diff.
- **Surveys of HyperBerry are done by the main model, by hand,** in detail and "keeping pitfalls
  in focus". The user stopped a delegated survey to say so.
- **Migrate as much of HyperBerry as possible;** change only what Telegram or the engine forces.
  Where HyperBerry follows WhatsApp's layout and Telegram differs, **the Telegram way wins**
  (channels are in the main chat list).
- **One commit per step,** well separated. Commit only what the user asked for; nothing is
  pushed (there is no remote).
- **Report plainly:** what was verified on the phone, what only compiles, what failed. The
  user checks claims against the phone.
- **When told "after this step stop and report", stop.**
- **`-O2`, LTO, OpenSSL with ARM assembly** is the build; the user wants total results, not
  one build per optimisation.
- **Rule zero of HyperBerry's code:** never remove a workaround the phone proved.
  `../hyperberry/docs/plan/pitfalls.md` is required reading before touching `app/`.

## 3. The phone: what is allowed

Host `bb10rootx` in `~/.ssh/config`, **reachable over USB only** (169.254.0.1), a root shell.

- **Allowed:** copy and run test binaries under `/root/tengram`, through `scripts/push.sh`.
  Nothing outside that folder.
- **Never install a `.bar` by any route** without asking in that moment: the user installs
  every build themselves.
- **Never run `tengramd --service` from the root shell:** it could register a stray Hub account.
- **Never run `link.logout`** on the real session: it ends it, and logging in again needs the
  user (the login code goes to their other devices).
- **Tests on the real account stay in Saved Messages** (its chat id is the user's own id, `me`
  in the `hello` reply) and clean up after themselves.
- **Telega** (the user's current Telegram app) may be running; leave it alone.
- **A new file runs only after `/proc/boot/pathtrust !<path>`,** and a file the system refused
  stays refused even if recreated on the same inode: `scripts/push.sh` gives each build a fresh
  name, trusts it before its first run, and parks refused copies in `denied/`.
- **The phone's shell lacks `head`, `tr`, `uniq`, `diff`, `printf`, `rmdir`, `sqlite3`:** filter on
  the Mac.
- **The Telegram API id and hash are in no file.** The engine reads `TG_API_ID` and
  `TG_API_HASH` from the environment; ask the user for them and pass them on the command line
  only.

On the phone now, in `/root/tengram`: `data/` (the logged-in session: `db/`, `files/`,
`chats.db`, `db.key`, `prefs.json`), engine and tool binaries named `<name>-<sha8>-<n>`, the
trusted sampler `measure.sh-e6d288f2-1`, logs of runs 1 to 7. Nothing of ours is running.

## 4. Build

Everything builds in Docker (OrbStack) from the Mac.

| What | Command | Notes |
|---|---|---|
| TDLib, OpenSSL, zlib for BB10 | `scripts/build-deps.sh` | Image `tengram-build` from `docker/Dockerfile`, base `bb10-cxx20:emutls-lto` (a local tag of the user's toolchain with the faster emulated TLS and LTO). TDLight `ccf8956e…`, patches in `patches/td/` (0001 to 0006) and `patches/openssl/`. Stamped: reruns only when an input changes. About 12 minutes. |
| The engine and `dbq` | `engine/build.sh` | `build/tengramd` (37 MB, static), `build/dbq`. |
| The UI | `scripts/build-ui.sh` | Fetches the emoji font, qrcodegen, OpenMAX headers, libwebp, Qt's SQLite driver; parses the QML with the SDK's `lupdate`; `qmake` + `make` in `yamsergey/bb10-ndk:0.6` → `build/ui/app/tengram`. **No packaging yet** (the user: later). |
| The SQLCipher driver test | `scripts/build-qtsqlcipher-test.sh` | `build/qtsqlcipher/sqlciphertest`; on the phone it needs `LD_LIBRARY_PATH=/usr/lib/qt4/lib`. |
| Icons | `python3 scripts/gen-icon.py` | Needs Pillow. Writes `assets/icon.svg` and the PNGs in `app/assets/images`. |

**Traps of this Mac:**
- **It sleeps.** On the night of 2026-11-17 it slept for three hours and froze a build, a
  sub-agent and OrbStack. Start `caffeinate -i -m -s -t <seconds>` before any long stretch.
- **OrbStack can stay hung after a sleep:** `orbctl stop` (give it 100 s), `orbctl start`.
- **Docker under OrbStack may read a file just edited at its old size:** run again.
- **The editor's red C++ errors are noise** (no SDK on the Mac). The Docker compile is the check.

The toolchain repository `../bb10-cxx20` is the user's. It is on `main` with LTO committed
there (not by this work). Do not change it without being asked. `docker/Dockerfile` still
names the local tag `bb10-cxx20:emutls-lto`; whether to move to a released image is the user's.

## 5. Test

- **On the phone:**
  ```sh
  E=$(scripts/push.sh build/tengramd)
  ssh bb10rootx "cd /root/tengram && TG_API_ID=… TG_API_HASH=… $E data >engine.log 2>&1 </dev/null &"
  tests/engine_smoke.py [--batched]      # Saved Messages only, deletes its own message
  tests/engine_client.py <cmd> '<json>' --listen 10   # any command, prints replies and events
  ssh bb10rootx "cd /root/tengram && ./dbq-<sha>-1 data/chats.db data/db.key '<sql>'"
  ```
  Stop the engine with `kill <pid>` (`pidin -F '%a %n' | grep tengramd`): it closes TDLib,
  logs the count of updates by type, removes `engine.json`. Measure with
  `TDPROBE=<engine> ./measure.sh-e6d288f2-1 -i <s> -t <s> -o <prefix> data` on the phone
  (it samples `pidin` times and memory). When looking into `chats.db`, print counts and
  lengths, not the user's names and texts.
- **Off the phone:** a sub-agent tested every engine step on the Mac against a scripted fake
  of TDLib's `ClientManager` (the real engine code otherwise). That harness is **not in the
  repository and not reviewed**: a copy is in `build/harness/` (git-ignored: `fake_client.cpp`,
  `drive*.py`, `build.sh` with paths that point at a session folder that may be gone, and
  `tdhost/`, a native build of the TDLib parts it links). Bringing it into `tests/` properly is
  worth an early step: today the only test in the repository needs the phone.
- **The UI has never run.** It compiles and its QML parses; that is all.

## 6. The engine: state

All of this is committed and was run on the phone unless marked.

| Area | State |
|---|---|
| Loop, TDLib thread, socket server, lock file | Works. One `poll()` loop; no timer of its own when idle. |
| Database key | One key, 32 bytes from `/dev/random` at the first launch, in `data/db.key`, for `chats.db` and TDLib's database. Always encrypted: no toggle, no passcode (user's decision). |
| Login (`link.*`) | `unlinked`, a refused phone number, and the QR link were seen on the phone with a second, empty engine. **A real code, the password step and `link.logout` were never run.** |
| Chat list | Mirrors Telegram's main and archived lists into `chats` (468 chats). Full load once after a login (`syncing` in the link body meanwhile), one page at later starts. |
| Messages | Live messages, edits, deletes, read states; `chat.open` and `chat.history` verify each page against TDLib; `msg.send` (with `mentions`), `msg.edit`, `msg.delete`, `media.get`; text formatting as `extra.ent`; `forwarded`, `spoiler`. |
| Chat commands | `chat.read`, `chat.typing`, `chat.set`, `chat.close`. `chat.set` was only tried with `unread` on Saved Messages. |
| Live events | `typing` and `presence` for the open chat. Presence seen on the phone; typing only in the harness. |
| Delivery mode | instant, batched (a sync exactly on time, 2.3 s long), auto (batched while the account is online on another device: **confirmed by the user** on 2026-11-17 07:16). A request, or an answer TDLib still owes, keeps the network on. |
| Prefs | `mode`, `batchSecs`, `notifyPreview` act; `mediaOnMobile`, `language`, `cacheMB` are kept and do nothing yet. |
| Notifier | On TDLib's notification groups, to a Hub backend that only logs. **No notification arrived in any test run:** untested on real traffic. |
| `--service` | The real Hub (UDS through `dlopen`), the BPS thread (navigator exit, Hub actions, network status). **Compiled only;** the list of what to check is in `NOTES.md` ("To verify on the phone once the app is packaged"). |

**Measured:** 14 MB of data; 0.57 CPU s in the quietest night hour, 1.27 s in a morning hour
(Telega's service: 15 to 40 s an hour; the probe: 1.95 s and 47 MB). Ready 0.36 s after start.
A daytime hour on battery is still to be measured.

**Known loose ends:**
- **UI to the front while offline:** TDLib starts connecting after 50 ms or after 1.8 s, about
  half and half. Not understood.
- **Once, an own-status update was ignored as "our own doing"** with no request in the 10 s
  before: some TDLib request with a callback was unanswered. The log now names such requests
  by TDLib type id (`mode: busy with … of types …`).
- **Patch 0006** (SQLite cache 8 MB, `pread`): its benefit is unproven and its memory cost
  unmeasured.
- **Old message rows are never pruned** (`ponytail:` in `messages.cpp`).
- **The engine words previews in English only.**

## 7. The UI: state

`docs/ui-fork.md` is the reference; its section 8 lists what to test first on a phone.

- **Done (step 1, compiles, never run):** Tengram's identity, accent `#24A1DE`, no tabs (Calls,
  Stories, Starred removed; Calls returns later), channels in the main list, `chats.db` opened
  through `ChatsDb` (the SQLCipher driver and `db.key`), Telegram's chat keys and Hub item
  ids, every user-visible text.
- **`app/` was committed untouched first** (`390e3ec`), so `git diff 390e3ec -- app` is the
  whole difference from HyperBerry.
- **WhatsApp's rules are still in the UI's logic** (edit within 15 minutes, delete within 2.5
  days, `*bold*` typing, typing timeouts…): `grep -rn WhatsApp app/` finds each, and
  `docs/ui-fork.md` §3.9 lists them.

## 8. What to do next, in order

The user had asked for a stop and a report at this point; **ask before starting.**

1. **UI step 2** (`docs/ui-fork.md` §3.5, §3.6, §3.8):
   - the login pages for Telegram's states: phone (reuse `LinkNumberPage`), a code field that
     says where the code went by `codeType`, a password field, the QR as it is;
   - history paging against `chat.open` / `chat.history`, with the rule that keeps rows
     arriving only at an end of the list: the oldest loaded day has no date header until an
     older day or the start of the history is loaded; `rowid` replaced by the message id as a
     number;
   - the chat list reading only the changed rows, and nothing while the screen is off.
2. **Engine, the pages around the chat** (`docs/ui-fork.md` §4): `contact.info`, `group.info`
   (paged: a supergroup can have 200,000 members) and the `photo` event, `profile.get`,
   `contacts.list`, `channel.info`, `chat.clear`, `chat.delete`, `channel.unfollow`.
3. **Reactions and poll votes.** Telegram gives counts per emoji or option, not a row per
   person. Proposed to the user and **not yet answered:** change the tables' meaning (a row
   per emoji with a count) rather than the bubbles' queries.
4. **Engine, sending and managing:** `media.send`, `poll.create`, `group.participants`,
   `group.create`, invites, `contact.block`, `profile.set`, `chat.export`.
5. **Packaging** (the user: later; `docs/ui-fork.md` §5 has its traps): the engine must take
   its data folder from `HOME` and run as the service when started with no arguments, and the
   API id and hash must be compiled in from the build's environment. Then the user installs
   it and the `--service` list is checked.
6. **Voice notes** (Opus in the engine, both ways), then **Calls**.
7. **Measurements still owed:** a daytime hour on battery; the notifier's log against what
   really notified; warm query times through the SQLCipher driver.

## 9. Decisions on record

- Always encrypted; one key made on the phone; no toggle; no passcode.
- Notification delivery: UI open → instant; UI closed and another session active → batched
  (5 minutes, a setting); UI closed and no other session → instant. No relay server.
- The engine does all syncing; the UI never talks to Telegram.
- Address book sync would upload the contacts to Telegram: a setting the user turns on, never
  a default (proposed in the survey; the user has not objected).
- A Go engine on gotd/td was the accepted fallback if C++ had measured badly. It did not.
- Icons: the tangram paper plane; grey Hub glyph for read items, yellow for unread
  (HyperBerry's convention, not asked for by the user: ask), white for the account, which is
  what the lock screen draws.
