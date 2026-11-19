# Tengram: handoff, 2026-11-17 (evening)

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
  (`TG_API_ID=… TG_API_HASH=… scripts/build-app.sh`), or from `.tg-keys` at the repo's root (gitignored, written at the
  user's ask, 2026-11-17), which `build-app.sh` reads. Never put them in a tracked file, notes or memory.

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
- **Installed: Tengram 0.1.0.14** (evening; `build/buildid.txt` is at 14). The `headless-mem` addon is installed (by
  the app, 0.1.0.7) and authman is patched since 13:40 (boot 13:34).

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

## 6b. The evening (2026-11-17): the UI plan restarted, 0.1.0.9 to 0.1.0.14

The user tests in rounds: **batch several features into one package**, then one test round (the user's ask).

| Commit | What | State |
|---|---|---|
| `428bd38` | Engine `profile.get`/`profile.set` (name split at the first space, bio ≤ 70), `media.send` (kind by extension; caption on photos and videos only; `thumb` ignored) | phone: works |
| `59f157f`, `5ea1875` | Settings: Background delivery (auto, instant, every 5 min = `prefs.set mode`); Settings split into pages as meegram2's (Account = ProfilePage with Log out, Appearance, Chats, Notifications, Data and storage) | phone: works |
| `238b443` | Saved Messages in the chat list's menu (chat id = our user id); the engine names that row "Saved Messages" | phone: works |
| `0625348` | Engine: `contact.info`, `channel.info`, `group.info` (≤ 200 members + `count`), `contacts.list`, `chat.clear`, `chat.delete`, `channel.unfollow`, the `photo` event; the UI no longer sends `contacts.book` | phone: info pages and New chat work; clear/delete not tried |
| `ab61b78` | Sharp photo/video bubbles: `media.get {preview: true}` (photo size ≥ 400 px short side; video JPEG thumbnail), asked as rows are read, priority 8 | phone: works |
| `dc2c9df` | Group senders as Telegram's: square picture (chat list tile size) beside the newest message of a run, name on the oldest, Telegram's 7 colours from the user's accent, admin tags (engine `senders.get`, `updateAccentColors`) | phone: works; a run's picture moving to a new message untested |
| `6a0bddc`, `85e3d2d` | **First unread at the top.** `scrollToItem` in the bottom-to-top list lands by estimated heights (photos far taller). Now: the rows older than the divider are held back (`MessageModel::m_holdOlder`), the scroll is aimed 3× over 450 ms, the list is nudged 4 px off its top edge (a list exactly at an edge keeps it in view when rows are appended there), then `releaseOlder()` appends them | phone: works (0.1.0.13) |
| `e565934` | Contact page: groups in common show their pictures (from the chat list) | not seen |
| `f0e3832` | A chat opened with only its last message cached loads its history: a list too short to scroll never neared its top (`fillShort`) | not seen |
| `de536ae` | Black screen: a second tap on the chat's back arrow during the slide popped the chat list (the root) too; the arrow now pops only while its page is on top. Same fix in HyperBerry, `bad0440` (not built) | not seen |

**Not seen yet on the phone (test with 0.1.0.14):** a chat found through search loads its older messages; groups in common
show pictures; a double tap on a chat's back arrow stays on the chat list; a group sender's picture moves down to their new
message; Clear chat and Delete chat (on a throwaway chat).

## 6c. Late evening (2026-11-17): media, topics, pins, HyperBerry's ports (0.1.0.15 to 0.1.0.17: tested, all ok)

Found on the phone first: `voice.play`/`voice.start` answered `unknown_cmd`; no video ever reached mm-renderer (the
23 MB download finished after the user had left the chat, with only a spinner to show); every missed swipe to reply was
a touch **cancel** 270–720 px out (the list takes the touch once a long swipe drifts), not a short release.

| What | Where | State |
|---|---|---|
| Swipe to reply: a cancel past the 36 px mark replies too (also HyperBerry `fa4b190`) | `MessageCardActions.cpp` | builds |
| **Voice notes:** libopus 1.6.1 in `build-deps.sh`; `voice.play` (OGG → `data/cache/voice/*.wav`, marks theirs played), `voice.start` ("recording" every 4 s until send/cancel, 30 min cap), `voice.send` (WAV → OGG Opus, 20 kbps, waveform; encoded at send on the engine thread), `voice.cancel` | `engine/voice.cpp`, `opus_ogg.*` | `tests/voice_check` ok on the phone; never sent to a real chat |
| **Download progress** as meegram2's: spinner, then a ring (25 frames, `scripts/gen-ring.py`) with an × ; "1.2 MB / 23 MB"; a tap while downloading cancels (`media.progress` event ≤ 5/s, `media.cancel`, error `cancelled` without toast) | `requests.cpp`, `MediaOpener`, `MediaBubble` | builds |
| **To the newest:** a square opaque button, bottom right, when > 12 entries above the newest or in a history window | `ChatPage.qml` | builds |
| **HyperBerry ports:** Interface Normal/Compact (Appearance page), bar order Search/New chat/New group, emoji picker closes on send, menu letters R A V C E X D F, **Forward** (engine `msg.forward`), a quote's tap jumps to it, quieter strip and match tint, translations | many | builds |
| **Forum topics:** schema v4 (`messages.topic`, `chats.forum`, `topics` table; the migration empties `messages`, a cache TDLib refills), `topics.list`, `topics` event, `topic` on history/read/typing/send/voice; TopicsPage (chat-list rows, square tiles, Compact-aware, Mark all read), a topic opens ChatPage filtered to it | `engine/topics.cpp`, `TopicModel`, `TopicsPage.qml` | builds; no TDLib call exercised |
| **Pins:** `chat.pins` (TDLib's pinned filter, newest message first, no expiry); the menu asks it. The WhatsApp notice logic is gone (it never found anything in Tengram) | `requests.cpp`, `Pins`, `PinnedAction.qml` | builds |

**The user's round (2026-11-17, late):** voice, downloads (ring, cancel), gestures, forum topics, pins, the Hub's
inverted glyphs: all ok. Fixed after it: a video stretched to the screen (the viewer now takes the player's
`videoDimensions`, 0.1.0.16, ok); search in Send to / Forward to (0.1.0.16); Hub items said "Telegram / New message"
after an engine restart (the read-at-start pass ran before the chat names were in memory: now from chats.db), and
hub.json's `glyph` re-adds the account once for a new glyph (0.1.0.16); a channel's composer row has only the menu
(0.1.0.17, not yet seen).

**Later the same night (0.1.0.18 to 0.1.0.23, each installed at the user's go-ahead):** Send as file is a square
toggle ("As file") at the staged bar's right, for PNGs and the user's own JPEGs (`media.send`'s `file`; seen ok);
**Edit** in the staged bar opens the system picture editor card (`sys.pictureeditor.cardeditor`, `bb.action.EDIT`)
and restages what it saves: a copy comes back as reason `SaveDone` with a path, the original saved over as `save`
with `[{"uri": …}]` (both seen on the phone; works); **Settings → Devices** (`engine/sessions.cpp`, `Sessions`,
`DevicesPage.qml`, `QrScanPage.qml`): the sessions list and terminate work on the phone; Telegram refuses ending
sessions from a device signed in under ~24 h (406 FRESH_RESET_AUTHORISATION_FORBIDDEN, now said in words);
**Link desktop device** (camera + BarcodeDetector, `confirmQrCodeAuthentication`) is **not yet tried**.
`README.md` written (features as of 0.1.0.23).

**Tested with 0.1.0.15 (first launch migrates chats.db to v4: chats open empty for a moment and refill):** a voice note
plays, records, sends (the other side sees "recording…" throughout), a received one turns played; a big video and a
document show the ring and the size, a tap cancels, a finished video plays in the viewer; swipe to reply on long swipes;
the newest button; Normal/Compact; Forward; a forum: the topics list, order, marks, a topic's history and first unread,
send/voice/photo into a topic, another topic's message not shown, Mark all read; Pinned message in a chat and a topic.

Also in it: forwarding to a forum opens its topics and forwards into the one tapped; every string translated in the
ten languages (the split settings pages' strings moved to their own contexts, translations kept); the Hub glyphs
inverted, the plane cut out of a disc (the user; `scripts/gen-icon.py`).

**Loose ends:** Group info isn't in the TopicsPage's menu (it is inside a topic); the voice encode blocks the engine
about a second a minute of audio (unmeasured). **lupdate trap (fixed `7c95475`: the strings are marked with QT_TRANSLATE_NOOP now):** the C++ cards translate through
`Card::tr(kContext, …)`, which lupdate can't see: a plain `lupdate -pro` marks their 35 entries (MessageItem,
ChatRow, MediaBubble) obsolete and lrelease drops them. They were restored by hand; mark the strings with
`QT_TRANSLATE_NOOP` before the next lupdate.

## 7. Where to resume, in order

**First:** try Link desktop device (scan Telegram Desktop's QR; then the camera is free for other apps); see the
channel composer (0.1.0.17).

**Step 4 is written (2026-11-18, `26ff906` to `b9a76f1`): it builds, it is not packaged and nothing of it has run.** The
user batches: build one package when the round is ready, then ask to install it. Test in that package:
1. **Hub account icon:** the white glyph in the Hub too, not the old grey one (`hub_account2.png`, glyph 3; the Hub
   caches an account's icon by file name, `097d4ed`). The Hub's items go and the unread ones come back once.
2. **First start migrates chats.db to v5** (drops `reactions` and `poll_votes`): chats open as before.
3. **Reactions:** chips under messages with their counts, wrapping on the Classic (the chip widths are reckoned, not
   measured: `MessageCard::chipWidth`), ours on the primary colour; a tap adds or takes back; React from the menu,
   also in a channel. Older messages get their chips once the engine writes them again (opening the chat).
4. **Polls:** counts and our answers from Telegram, a vote and its withdrawal, a closed poll can't be voted on;
   Create poll (groups only now: Telegram refuses one in a chat with a person), also inside a topic.
5. **Managing:** block/unblock a contact; a group's add, remove, promote, demote (an admin's own group); New group;
   an invite link (t.me/+…): join, an expired one, a group that approves requests ("Request sent"); Export chat
   (`Telegram Chat with <name>.txt` in Downloads, readable by other apps: the engine chmods it 0644, untried).
6. **Group info in a forum's topics list** (its ⋮).
7. **Loose ends:** Clear chat empties the list's preview and keeps the chat's place; Saved Messages named so after an
   engine start; a sent video shows its length (read from the file's mvhd).

8. **Settings → Privacy and security** (`a3bd602`, `414fea4`): the page loads (all rows at once); a rule's DropDown
   changes and goes back when Telegram refuses; two-step verification set/change/e-mail code/off (a session under a day
   old may get "too fresh"); blocked users and unblock; auto-delete; account TTL; connected websites; Sync contacts stays
   off by default and sends the book at once when turned on.
9. **Settings → Data and storage** (`5ee3f09`, the page after it): storage and data used; Clear media, then a photo in a
   chat shows its blurred thumbnail and comes back on a tap; automatic download on Wi-Fi only (the default: a photo
   arrives downloaded), nothing on mobile data, a download stopped when Wi-Fi goes; Save to gallery puts a received
   photo in Pictures → Tengram; the media cap prunes 2 minutes after start (tengramd.log).

10. **Secret chats** (`618cabd` and the UI after it; chats.db migrates to v6 at the first start): Start secret chat from a
    person's page (the other side on the user's Android phone: Telegram Desktop has none); the lock and green; "Waiting
    for … to get online" until it is accepted; send and receive; the self-destruct timer, a message's countdown, the
    message going; a timed photo's countdown starting when it is opened; Delete chat ends it on both sides; the Hub says
    "New message" only; nothing of it in Pictures → Tengram. Requests that came while secret chats were off are lost.

**After 0.1.0.25's round (2026-11-18, written, built, not packaged):**
11. **Hub items' icons:** the inverted plane on the items too (`hub_read2.png`, `hub_unread2.png`, glyph 4: the Hub
    caches them by file name as it did the account's); the account is re-added once at the first start.
12. **Reactions in a group:** the chips start under the bubble, not under the sender's tile (`fillChips` puts the
    tile's width in the area's left padding: a top-to-bottom stack ignores a child's left margin).
13. **Telegram links open in the app** (`link.open`, `group.join`): a public chat we're in opens; one we're not in asks
    Join/Follow, then opens; a user or bot opens their page; a message link opens the chat at the message (a forum:
    only its topics); invites as before; a sticker set or proxy goes to the browser; an unknown username says "This
    link doesn't lead anywhere". Links from outside the app (browser, e-mail) are not caught.
14. **Telegram's look, Cascades kept** (2026-11-18, in 0.1.0.27, not committed; colours on `nav` in `main.qml`): our bubbles
    `#2B5278` dark / `#EFFDDE` bright, theirs `#182533` / white, the time and ticks on ours in `outMeta` (read = two
    ticks, no colour change); the doodle wallpaper behind the chat (`scripts/gen-wall.py`, 512 px tiles: Cascades
    repeats only powers of two); dates and service lines as translucent pills with white text; a group's "Name:" in
    the list in `#24A1DE` (rich text span: untried); tiles in Telegram's seven peer colours; the chat list's and the
    chat's title bars in `headerColor` (a FreeForm container's background: does the bar's chrome show around it?);
    **Seen in 0.1.0.27 (the user):** all fine but the wallpaper, which the user will redo later. **Taken off the chat
    page** (the user, 2026-11-18, after 0.1.0.27): the PNGs deleted; `scripts/gen-wall.py` can draw them again. The Settings probe
    (`TitleBarAppearance.Branded`) drew the bar as always: on 10.3 it doesn't take the primary colour, so the probe is
    gone and a page's bar is coloured only by a FreeForm container's background.
    Check both themes, switching live (the wallpaper's `imageSource` binding), and contrast of grey text, quotes,
    link previews, polls and voice notes on our blue bubble (their own bubbles weren't recoloured).
15. **Download in a file's menu** (2026-11-18, in 0.1.0.27, not committed; `media.get` with `save`, engine `downloads_copy`):
    long-press a document or audio file → Download (key S) → downloaded if needed (the bubble's ring), copied to
    `/accounts/1000/shared/downloads` under its own name (`name (2).ext` beside a different one; the same size is
    reused), toast "Saved to Downloads"; File Manager shows it. Ours too (Saved Messages from another device). Greyed
    in a secret chat; a protected or self-destructing one says "Couldn't save to Downloads". A tap still only opens.
    tengramd.log says `downloads: <path>`.
16. **Sending without the blink** (2026-11-18, builds, not packaged): the user saw a sent message drawn twice and a
    short delay. The engine swaps the sending row (temporary id) for the final one in one `msgs` event; the model
    took that as remove + add, so the card was destroyed and rebuilt. `MessageModel::merge` now hands the sending
    row's entry to the final id (paired in order) and the card is updated in place. Our own new message also flushes
    at once instead of after the 50 ms coalescing (`mark_dirty(true)` on `updateNewMessage` with a sending state).
    Check: one bubble, no blink when the tick turns; several quick sends; a send that fails (airplane mode) stays.
17. **About → Open source libraries** (2026-11-18, builds, not packaged): ten rows (name, version, licence), each
    opening the project's page in the browser; the header in the ten languages. Update the versions when
    `build-deps.sh` or a `fetch-*.sh` pins a new one.
18. **Several files at once** (2026-11-18, builds, not packaged): Attach picks several (PickerMultiple); two or more
    open **Send files** (`SendFilesPage.qml`): a carousel, one file large at a time with the next peeking (snaps per
    file; "2 / 5" in a corner), a square cross drops a file, a tap on a picture opens the system editor and the edited
    file takes its place; two square toggles on one row, Album (on) and As files (off); the caption, from the composer.
    (0.1.0.30 had a 3-column grid: small pictures, and the checkboxes under it didn't fit the Classic.) Grouped: photos and videos in albums of up to 10 (engine `media.album`, `sendMessageAlbum`), documents and
    audio each with their own kind, a lone file as `media.send`; the caption on the first photo or video, else a text
    after. Also several files from another app's Share menu (`filelist/*`, `OpenRequests::sharedFiles`; **the data's
    format is a guess**: tengramd's UI log prints `shared:` with it), into a forum's topic too. Check: 2 photos, 12
    photos (two albums), photos + a PDF, Send as files, a reply and a topic, edit then send, cross down to none.
19. **Albums are Telegram's** (2026-11-18, 0.1.0.34): the chat grouped by HyperBerry's WhatsApp rule (4+ photos within
    a minute, no caption), so a sent album of 3 showed as 3 photos. The engine keeps `extra.album` (media_album_id) and
    `Albums::find` groups 2+ consecutive photos/videos of one album id (`tests/albums_check.cpp`); a caption on any
    member shows under the tiles; 2 tiles side by side, 3 as two over one wide. Old rows group once the engine writes
    them again (opening the chat). Check: a received album of 2 and 3, one with a caption, 4 separate photos stay apart.

20. **Stall detector** (2026-11-19, not built): the user saw the UI freeze ~5 s at 21:55, just after opening a group's
    chat (the engine answered at once; the UI went silent 3.5 s, then left the front). `EngineLink::beat()` ticks every
    500 ms while in front and writes `ui: stalled N ms` to ui.log when a tick is over 1 s late. After a freeze, read the
    lines before it. Remove once the cause is found. Also seen: opening a chat at 20:22:37 took 1971 ms (`chat: first
    page`), likely photo previews made on the UI thread (`MediaData.cpp` `preview()`).

**Saved for later (the user, 2026-11-18): video streaming.** Possible, not built. bbtube plays a local MP4 while it
fills (moov written first, the file at its final size). For Tengram the planned route is a small HTTP server in the
engine on 127.0.0.1 serving a video by byte ranges: a range not downloaded yet is asked of TDLib (downloadFile with
offset/limit) and the request waits for it, so mm-renderer waits instead of ending and seeking works. The video viewer
then opens that URL at once; a "Stream videos" toggle on Data and storage, as Android's. The simpler route, mm-renderer on
TDLib's partial file, risks the player stopping at the file's end when it catches up, and can't seek ahead.

**Left of step 4:** topics' create/rename/close and custom-emoji icons (not asked yet); voice encoding as the WAV grows
(only if long notes stall the engine at send). No voter names or who-reacted any more: Telegram gives counts here.

**The older list follows (items 0 to 8 below; 3's walk of §8b is mostly done by the evening's tests).**

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
   `profile.set`, `chat.export`); voice notes (done, 0.1.0.15), then **Calls**; measurements owed (a daytime hour on
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
- A chat opens with its first unread message at the **top** of the screen (user, 2026-11-17 evening); a short unread run
  ends at the newest, as Telegram's.
- Group senders' pictures are **square, the chat list's tile size** (user); colours are Telegram's 7 accent colours.
- Reactions: a chip per emoji with its count; stored as JSON in the message row (user).
- Settings are pages grouped as meegram2's (user).
- Icons: the tangram paper plane; grey Hub glyph for read items, yellow for unread (HyperBerry's convention,
  not asked for by the user: ask), white for the account.
