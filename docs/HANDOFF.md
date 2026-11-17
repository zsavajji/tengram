# Tengram: handoff, 2026-11-17 (afternoon)

For whoever picks this up next. It says what exists, what is proven on the phone and what only compiles,
how to build, install and test, what the user has decided, and where to resume. Details live in:

- `NOTES.md` — the plan and every measurement and finding, in the order they happened.
- `docs/protocol.md` — the contract between the engine and the UI. It changes with the code.
- `docs/ui-fork.md` — HyperBerry's UI survey, the traps of each change; **§8b lists what to test on the phone.**
- `docs/headless-cap.md` — the engine's memory cap, and the fix in `../rlimit-patch`.

## 1. What this is

A Telegram client for BlackBerry 10, in two processes:

- **`tengramd`, the engine** (`engine/`, C++20, clang through the user's `bb10-cxx20` toolchain): the only
  process that talks to Telegram, through TDLib (TDLight, pinned). It keeps an encrypted `chats.db` current
  and serves the UI over a loopback socket. Packaged, it is the app's headless service.
- **`tengram`, the UI** (`app/`, Cascades, Qt 4.8, QML and C++98, built with qcc): a fork of the user's
  WhatsApp client HyperBerry (`../hyperberry`). It reads `chats.db` and sends commands.

**Why it exists:** the other TDLib clients on BB10 drain the battery and their UI is slow. Battery and speed
come first; everything is measured on the phone.

## 2. Working with this user

From the user during the work, and from `~/.claude/CLAUDE.md`. Not optional.

- **Delegation:** the main model plans and verifies; sub-agents carry out a decided plan (`sonnet` for new code
  from a detailed brief). Up to 2 at once is allowed (2026-11-17). Never delegate debugging or design.
  Review every diff; the sub-agents' code needed fixes in review every time this session.
- **Surveys** (HyperBerry, meegram2, other repos) are done by the main model, by hand, pitfalls in focus.
- **Migrate as much of HyperBerry as possible;** change what Telegram or the engine forces. **The Telegram
  way wins** where HyperBerry follows WhatsApp (channels in the main list; a chat opens at its first unread).
- **One commit per step,** well separated, with what was verified and what only compiles. Nothing is pushed
  (no remote).
- **Report plainly:** verified on the phone, only compiles, failed. The user checks claims against the phone.
- **When told "stop and report", stop.**
- **Every user is rooted** (the user: "there is no 'without root' user"): root-side fixes are product routes.
  Don't spend effort on no-root alternatives.
- **The test phone is a BlackBerry Classic** (Q20, SQC100-1, OS 10.3.3.3216), not a Q10 as older notes say;
  the user has both. **Never hardcode the device:** read it (BPS `deviceinfo`, as `engine/link.cpp` does).
- **Rule zero of HyperBerry's code:** never remove a workaround the phone proved.
  `../hyperberry/docs/plan/pitfalls.md` is required reading before touching `app/`.
- **`-O2`, LTO, OpenSSL with ARM assembly** is the build.

## 3. The phone: what is allowed

Host `bb10rootx` in `~/.ssh/config`, **USB only** (169.254.0.1), a root shell.

- **Allowed without asking:** copy and run test binaries under `/root/tengram` (`scripts/push.sh`); read
  anything (logs, `pidin`, files).
- **Ask in the moment** before: installing a `.bar` or an addon; any change outside `/root/tengram` (OS state,
  `/var`, authman, PPS). The user approved each such step this session one at a time; approval doesn't carry
  over. **One of them broke every app launch until a reboot** (restarting authman, `../rlimit-patch`
  research): check how a system process's clients behave before touching it.
- **Never run `tengramd` as the service from the root shell** (a stray Hub account). With no `--data` it *is*
  the service now (below), so a test run always passes `--data`.
- **Never `link.logout`** a session the user needs. **Tests on the real account stay in Saved Messages.**
- **Telega** (the user's current Telegram app) may be running; leave it alone.
- **A new file runs only after `/proc/boot/pathtrust !<path>`;** a refused file stays refused even on the same
  inode. `scripts/push.sh` handles it.
- **The phone's shell lacks `head`, `tr`, `uniq`, `diff`, `printf`, `rmdir`, `sqlite3`, `grep -A/-B`:** filter on
  the Mac (`ssh … 'cat f' > local`).
- **The Telegram API id and hash are in no file.** Ask the user; pass them on the command line only
  (`TG_API_ID=… TG_API_HASH=… scripts/build-app.sh`). Never write them into the repo, notes or memory.

**On the phone now:**
- **Tengram is installed** (`com.zsavajji.tengram.testRel_jji_tengram30796029`), logged in by QR to the
  user's real account (listed as "BlackBerry Classic"). Sandbox data:
  `/accounts/1000/appdata/com.zsavajji.tengram.testRel_jji_tengram30796029/data/` — `logs/tengramd.log` (the
  engine), `logs/ui.log` (the UI), `chats.db`, `db.key`, `db/`, `files/`, `engine.json`, `hub.json`.
- **`/root/tengram` does not exist:** the old test session, binaries and logs were deleted at the user's request
  (they end that session from Telega's Devices list). `scripts/push.sh` makes the folder again when needed.
- **The `headless-mem` addon** (`../rlimit-patch`) is installed in `/var/addons`; authman's
  `_sys_headless_xlarge` is patched to 128 MiB in memory since the 11:22 boot. Authman also still holds a
  harmless `hmem` prefix from a failed experiment, until the next reboot.
- **Installed: Tengram 0.1.0.8** (the chat list kept across engine restarts: confirmed by the user); `build/buildid.txt` is at 8. The `headless-mem` addon is installed (by the app, 0.1.0.7)
  and authman is patched since 13:40 (boot 13:34).

## 4. Build and install

Everything builds in Docker (OrbStack) from the Mac.

| What | Command | Notes |
|---|---|---|
| TDLib, OpenSSL, zlib | `scripts/build-deps.sh` | Image `tengram-build` (`docker/Dockerfile`, base `bb10-cxx20:emutls-lto`). TDLight `ccf8956e…`, patches `patches/td/` 0001–0006, `patches/openssl/`. About 12 min, stamped. |
| The engine | `engine/build.sh` | `build/tengramd` (37 MB, static), `build/dbq`. With `TG_API_ID`/`TG_API_HASH` in the environment they are compiled in (`-D`). |
| The UI | `scripts/build-ui.sh` | Fetches, `lupdate` parse, `qmake`+`make` in `yamsergey/bb10-ndk:0.6` → `build/ui/app/tengram`. Agents built in `build/ui-b` (the same `make`) to avoid clashing. |
| **The package** | `TG_API_ID=… TG_API_HASH=… scripts/build-app.sh` | Engine + UI + `build/tengram.bar`, unsigned, next build id. Prints `Package-Version`. |
| The memory-cap addon | `../rlimit-patch/build.sh` | `build/headless-mem.zip`; see that repo's README. |

**Installing a `.bar`** (ask first): HyperBerry's root route —
```sh
scp build/tengram.bar bb10rootx:/var/rootdata/tgN.bar
ssh bb10rootx 'chmod 644 /var/rootdata/tgN.bar; J=/pps/system/installer/upd/current/job.tgN.bar;
  (umask 000; touch $J; chmod 666 $J); echo "action::install" >> $J;
  echo "package_location::/var/rootdata/tgN.bar" >> $J; echo "extras::" >> $J;
  i=0; while [ $i -lt 60 ]; do grep -q "result::" $J && break; sleep 2; i=$((i+1)); done;
  grep -E "result::|actual_version::" $J; rm -f /var/rootdata/tgN.bar $J'
```
An install closes the app and its engine; the user reopens it. An addon installs with
`/var/rootdata/installer <zip>` (its log: `/var/log/installer.log`).

**Traps of this Mac:** it sleeps (start `caffeinate -i -m -s -t <s>` before long stretches); OrbStack can hang
after sleep (`orbctl stop`, 100 s, `orbctl start`); Docker may read a just-edited file at its old size (run
again); the editor's red C++ errors are noise (no SDK on the Mac). `../bb10-cxx20` is the user's; don't change it.

## 5. Test

- **The installed app** (the main way now): open it on the phone, then read its logs over SSH (paths above).
  `slog2info` has the UI's and the navigator's lines (`grep -i tengram`). The engine logs its start line
  (`start: service … ignored: [invoke://localhost]` — the navigator's argument), its memory limits
  (`rlimit DATA: soft …`), and `out of memory` before a `bad_alloc`.
- **A test engine under `/root`** (no Hub, no UI): `E=$(scripts/push.sh build/tengramd)`;
  `ssh bb10rootx "cd /root/tengram && TG_API_ID=… TG_API_HASH=… $E --data data >engine.log 2>&1 </dev/null &"`;
  `tests/engine_smoke.py`, `tests/engine_client.py <cmd> '<json>' --listen 10`. It needs its own login. **Don't
  run it beside the app on the same session.**
- **The Mac harness** (`build/harness/`, git-ignored, unreviewed): a scripted fake of TDLib's `ClientManager`
  with drives `drive2`–`drive13` (`drive12`: `read_inbox`/`limit`; `drive13`: `chat.newer`, `chat.read upto`).
  The sub-agents built it with a patched script at
  `/private/tmp/claude-501/-Users-andreafaggin-own-projects-bb10-tengram/283e6744-93c6-4197-97ed-924a3b565cb6/scratchpad/harness-build.sh`
  (a scratchpad that may be gone). Bringing it into `tests/` is still worth doing.

## 6. What happened today (2026-11-17), in commit order

All compile; **only the login, the chat list's first draw and the service start were seen on the phone.**

| Commit | What |
|---|---|
| `809fe98` | Login pages for Telegram's states (code by `codeType`, password + hint, flood wait). TDLib takes no phone number or new QR while a QR waits: from the QR, "Link with phone number" logs out first. |
| `b954fe0`, `1a2c5ea` | History paging against `chat.open`/`chat.history` (verified floor, `rowid` → message id, oldest day's header held back); the chat list does nothing out of front and patches rows that keep their place. |
| `07931ec`, `91ac018` | A gap below the newest page is bridged (up to 3 × 100) or dropped; meegram2's lessons on TDLib history (`docs/ui-fork.md` §3.6). |
| `5b41598`, `52c5ca3`, `e3e78a2` | **A chat opens at its first unread message** (the user's ask), an "Unread messages" divider; reading follows what is shown (`chat.read upto`); a *window* of history for long-unread chats (`chat.newer`), joined to the live end by paging down; search and pin jumps leave the window. Engine: `chats.read_inbox` (schema 3, migrated in place), `chat.open limit`, `chat.newer`, `chat.read upto`. |
| `dbfe2d5`, `ab3a13b` | **Packaging:** `scripts/build-app.sh`, API keys compiled in, device read from BPS. The service takes `HOME` and logs to `logs/tengramd.log`; test runs use `--data`. |
| `7a89b22` … `7c8297b` | The 30 MiB headless cap: found, raised to 128 MiB, moved to `../rlimit-patch`. |
| `facdf11`, `efe1a3d`, `08dd382`, `0a2b194` | **The in-app memory patch** (§7 item 0): engine `memMB` in the link body; a one-button dialog installs the addon through `__root` + `tg-install-patcher`; the login page asks Telegram for nothing under 128 MB; "Close Tengram" ends both processes (the navigator's per-app cache); the dialog in the user's words. |
| `566876e` | **The chat list stays across engine restarts:** `linking` (what the engine says while TDLib opens) keeps the known link state instead of swapping to the linking page's "Starting…"; the chat list's title bar shows "Connecting…" and a spinner instead. |

**Seen on the phone (0.1.0.3/0.1.0.4):** the service starts and registers the Hub account; QR login works
(scanned from Telega); the first full sync loaded 467 chats in 12.9 s once it fit in memory; the chat list
drew. Under the 30 MiB cap the engine died with `bad_alloc` at every start while TDLib reloaded the installed
sticker sets; with the patched 128 MiB it ran through. **0.1.0.7:** the in-app patch flow end to end on a clean
boot (§7 item 0). **0.1.0.8:** the chat list stays across engine restarts, with the title bar's spinner: confirmed by
the user.

## 7. Where to resume, in order

**Ask the user before each item that installs, reboots or touches the account.**

0. **The in-app memory patch: verified on the phone** (0.1.0.7, 2026-11-17 13:40, on a clean boot without the addon):
   the one-button dialog (the user's text) installed `../rlimit-patch`'s addon through AutoRoot's `__root` and
   `tg-install-patcher` (`[HMEM] … to 128 MiB`), "Close Tengram" ended both processes, and the reopened engine
   logged `rlimit DATA: soft 134217728`. The **navigator keeps an app's memory limit from its first launch until
   every process of the app has ended**, hence the close step (`MemoryPatch::closeAll`). While the dialog is up
   a logged-in engine still runs at 30 MB and may die once at the first sync; the UI restarts it. Not yet seen:
   the login page's gate (a phone that isn't logged in). The no-root dialog can't be reached: an unsigned app
   runs only on a rooted phone (the user), so it stays as a fallback and is not a test.

1. **The memory patch in the app (done 2026-11-17, never run):** `MemoryPatch` (`app/src/`), the dialog in
   `main.qml`, `app/helper/tg-install-patcher.c`, the bar bundling `headless-mem.zip`, the engine's `memMB` in
   the link body. A phone with no addon and an engine at 30 MB gets one "Install" dialog; Install runs the addon's
   installer as root through AutoRoot's `__root` (output in Documents, `tengram-memory-patch.txt`) and ends
   `tengramd`. The test list is `docs/ui-fork.md` §8b, "Memory patch (in-app)". The user's phone already has the
   addon, so the install path needs it undone first (delete `/var/addons/headless-mem`, reboot): ask.
2. **Reboot test of `headless-mem`:** after a reboot, `[HMEM]` in `/var/log/installer.log` about 4 minutes in;
   then restart the engine (or reopen the app) and check `rlimit DATA: soft 134217728`.
3. **Walk `docs/ui-fork.md` §8b on the phone with the user,** now that the UI runs: the code login path (wrong
   code, password), opening chats (first unread, divider position, a long-unread group's window), paging up and
   down, gaps, sent-message flicker, read state as you scroll, search and pin jumps, the chat list's moves.
   Each finding is a fix, then a new package (0.1.0.5, …).
4. **The chat list's moves:** the log already answered §3.8's question: a full reload costs 125–277 ms for
   200–467 chats. Patch moves too (`itemMoved`, or remove + insert at the ends), not only in-place rows.
5. **`contacts.book`:** the UI sends the phone's address book (429 contacts) at every connect and the engine
   answers `unknown_cmd`. Decided: uploading contacts to Telegram is a setting the user turns on, never a
   default. Make the UI not send it unless that setting is on; the engine command comes later.
6. **The engine's memory peak**, even with 128 MiB: measure a fresh login's first sync (the user must log in
   again), then patch 0006's 8 MB SQLite cache and the sticker-set loads TDLib does at the first sync.
7. **The `--service` list in `NOTES.md`** ("To verify on the phone once the app is packaged"): the Hub account
   (every engine start logged "new to the Hub: true, hub.json is forgotten" — check for duplicate accounts),
   navigator exit, Hub actions, network status, notifications on real traffic.
8. Then the older list: **engine pages around the chat** (`docs/ui-fork.md` §4: `contact.info`, `group.info`
   paged, `photo`, `profile.get`, `contacts.list`, `channel.info`, `chat.clear`, `chat.delete`,
   `channel.unfollow`); **reactions and poll votes** (proposed, **not yet answered**: a row per emoji with a
   count); **sending and managing** (`media.send`, `poll.create`, groups, invites, `contact.block`,
   `profile.set`, `chat.export`); **voice notes**, then **Calls**; measurements owed (a daytime hour on
   battery, the notifier against what really notified, warm SQLCipher query times).

**Known loose ends:** UI to the front while offline connects after 50 ms or 1.8 s (not understood); once an
own-status update was ignored as "our own doing"; old message rows are never pruned; previews are English
only; WhatsApp's rules still in the UI's logic (`grep -rn WhatsApp app/`, `docs/ui-fork.md` §3.9);
`chat.newer` could in theory hit TDLib's `CHECK(!have_a_gap)` if the last message is deleted in the instant
before a window's first request (commit `52c5ca3`).

## 8. Decisions on record

- Always encrypted; one key made on the phone; no toggle; no passcode.
- Notification delivery: UI open → instant; UI closed and another session active → batched (5 minutes, a
  setting); UI closed and no other session → instant. No relay server.
- The engine does all syncing; the UI never talks to Telegram.
- Address-book upload to Telegram: a setting the user turns on, never a default.
- A chat opens at its first unread message, Telegram's way (2026-11-17).
- The 30 MiB headless cap is raised by the `../rlimit-patch` addon (users are rooted); the engine still keeps
  its peak low.
- Icons: the tangram paper plane; grey Hub glyph for read items, yellow for unread (HyperBerry's convention,
  not asked for by the user: ask), white for the account.
