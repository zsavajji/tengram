# Forking HyperBerry's UI: the survey, the pitfalls, the migration map

Read from `../hyperberry/app` and `../hyperberry/docs/plan/pitfalls.md` on 2026-11-17 (13,300
lines of C++, 9,900 of QML). The aim, the user's: carry over as much as possible and change only
what Telegram or the new engine forces. Decided for the first build: the same UI, accent
`#24A1DE`, no tabs (Calls returns later), packaging later.

**Rule zero, HyperBerry's and ours:** never remove a workaround the phone proved, on the theory
that it is no longer needed. Every odd-looking line in that app has a date and "Q10" beside it.
A change that touches one says so and waits for a phone test.

## 1. What the app is

- **A plain Cascades executable** (`hyperberry.pro`: `QT += declarative network sql gui`, the
  Cascades and BB10 libraries), built with qcc (GCC 4.6.3, C++98) in the Native SDK image by
  `scripts/build-app.sh`, which also packages the engine into the bar.
- **`main.cpp`** routes Qt's log to slog2 and `data/logs/ui.log`, installs the translator, makes
  one `EngineLink` and starts it first, then the models (`ChatListModel` ×3: chats, archived,
  channels; `CallsModel`; `StoriesModel`), the helpers (`ContactBook`, `EmojiModel`, `QrImage`,
  `Settings`, `OpenRequests`, `Invites`, `CallAudio`, `CallVideo`, `Proximity`), registers the QML
  types under the module name `hyperberry` (`ChatController`, `ChatRows`, `MessageCards`,
  `ContactInfo`, `ContactPicker`, `MapChooser`, `MediaOpener`, `Profile`, and `QTimer` as
  `Timer`), sets its context properties and loads `main.qml`. Twenty QML files import the module.
- **`main.qml`** is a `TabbedPane` (Chats, Calls, Stories) with the application menu (Settings,
  Profile, About). The Chats tab holds the `NavigationPane` `nav` that every page reaches by
  that id: it shows `LinkPage` while not linked and `ChatsPage` once linked (`nav.linked =
  engine.linked && !engine.link.syncing`), and carries what list items cannot reach otherwise:
  the one emoji text style, the text sizes, the press colour, `showPhoto`, `showVideo`,
  `showAlbum`, `openRequested`, `shareTo`, `backToList`.
- **Message cards are C++** (`MessageCard*.cpp`, the `*Bubble.cpp` classes, `CardStyle.h`), fed
  by `MessageModel` and `MessageRows`, which read `chats.db` directly, read-only.
- **Not in git, fetched by scripts before a build:** the emoji font and pictures
  (`fetch-emoji.sh`, 7.7 MB + 1.1 MB), `qrcodegen`, the OpenMAX headers (video playback),
  `libwebp` (stickers).

## 2. What carries over unchanged

| Part | Why it fits as it is |
|---|---|
| `EngineLink` | Same socket, `engine.json` in `QDir::homePath()`, `hello` with `proto: 1`, token. Replies are matched by `"@"` in a hash of pending ids (`EngineLink.cpp`, `m_pending.take(id)`), so tengramd's replies in any order are fine. Same `link`, `conn`, `chats`, `msgs` events. It already writes whole numbers as integers (`wholeNumbers`), which tengramd's typed fields need too. |
| Chat list (`ChatListModel`, `ChatsPage`, `ChatList`, search, pin, mute, archive, mark unread, the Active Frame) | Reads `jid, kind, name, last_ts, last_preview, last_out, last_state, unread, muted_until, pinned, photo`; all there with the same meanings. `chat.set` exists. |
| Chat page (`ChatPage`, `ChatController`, `Composer`, cards, reply, edit, delete with undo, copy, search in chat) | `chat.open`, `chat.close`, `chat.read`, `chat.typing`, `msg.send`, `msg.edit`, `msg.delete`; the `messages` columns are HyperBerry's. |
| Received photos, videos, documents (`MediaOpener`, `PhotoViewer`, `VideoViewer`, `VideoDecoder`, `VideoWindow`) | `media.get` answers with a path. |
| Settings, About, cover, emoji picker, text sizes, themes, hints | UI only. |
| QR linking (`LinkPage`, `QrImage`) | The engine's `qr` state carries the string to draw, as HyperBerry's does. |
| Phone entry (`LinkNumberPage`, `CountryPage`, `countries.js`, `phone.js`) | Reused for `link.phone`. |
| Hub taps and the Share target (`OpenRequests`, `SharePage`) | Same invocations; only the item id's form changes. |

## 3. The changes, one by one, with their traps

### 3.1 Copy and identity

**What:** `app/` (without the Calls and Stories files, §3.3), the build and fetch scripts,
`lib/` fetched afresh. `com.zsavajji.hyperberry` → `com.zsavajji.tengram` in the bar descriptor
(app id, both invoke targets, the MIME type `application/vnd.zsavajji.tengram.chat`, the Hub
actions), in `EngineLink.cpp` (`startEngine`: target `…tengram.service`, action
`…tengram.START`), the slog2 buffer name and the error-log file name in `main.cpp`, the
translation file names (`main.cpp` loads `"hyperberry_" + language`).

**Traps:**
- **The QML module name.** Every page that uses a C++ type says `import hyperberry 1.0`. A
  rename that misses one page breaks that page's load, and for a page in the start path that
  is a black screen with nothing in the log but `hyperberry-qml-errors.log`. Rename in one
  pass and check with `grep -L`; or keep the module name. Decided: rename, then grep.
- **The emoji font.** `main.qml` names the family `HBEmoji` and the file
  `fonts/HyperBerryEmoji.ttf`, which `build-emoji-font.py` writes. Rename both ends or neither.
  A `FontFaceRule` reaches only styles built on it and each rule loads the font again: keep the
  one shared style in `main.qml` (pitfall "Text, fonts").
- **Translations are keyed by the source text.** Changing "WhatsApp" to "Telegram" in a QML
  string orphans its ten translations unless the same replacement is made in every `.ts`
  `<source>` and `<translation>`. Do the replacement across QML, C++ and `.ts` in one pass.
- **`settings.ini`'s `linked` flag** makes a cold start show the chat list before the engine
  answers. It lives in the app's data folder: a new app id starts with none, which is right.
- **The installer keeps the old app when Package-Version is unchanged,** and a new id is a
  new app beside the old one. Not a concern until packaging.

### 3.2 Accent

**What:** `#1BD862` → `#24A1DE`, and its darker base `#14A249` → `#1B79A7` (the same ratio).
**Where:** `bar-descriptor.xml:145` (`CASCADES_THEME`, both colours), `Settings.cpp:69-70`
(`setVisualStyleAndPrimaryColor`, both colours), `cover.qml:36` and `:77`, `MessageCard.cpp:294`
(the search match, `#401bd862`), `CallSlider.qml:43` (not copied). The app icon, the splash
screens and the Hub glyphs are Tengram's own (`scripts/gen-icon.py`).

**Traps:**
- **`CASCADES_THEME` applies before any code runs** and `Settings::applyTheme` repeats it when
  the user switches theme: the two must hold the same pair, or a theme switch changes the
  accent.
- **HyperBerry's `gen-icons.py` also draws the app icon and splash** into `assets/images`. Run
  for Tengram it would overwrite ours with the green tile: take its marks (ticks, clock, pin,
  action icons), not its `app_icon` part.
- **The Hub account's one icon serves the lock screen and the instant preview;** they cannot
  differ, and item icons are swapped by the engine, not tinted. A white item glyph vanished on
  the light Hub (HyperBerry 5.7): items are grey and yellow, only the account's is white.

### 3.3 No tabs

**What:** `main.qml`'s root becomes the `NavigationPane` (`nav`) itself. Calls and Stories go:
`callsTab`, `storiesTab`, `fillTab`, `showStory`, `callSheetDef`, `liveCall`, `callFailed`; in
`main.cpp` `CallsModel`, `StoriesModel`, `CallAudio`, `CallVideo` and their context
properties; from the `.pro` their sources plus `CameraEncoder`, `Nv12Rotate`. The video player
(`VideoDecoder`, `VideoFormat`, `VideoOutput`, `VideoWindow`) stays: received videos use it.

**Traps:**
- **The application menu must stay.** With no `Menu.definition` the swipe down slides in an
  empty drawer (pitfall "Starting up"). It moves from the `TabbedPane` to the `NavigationPane`.
- **Everything that says `root.`** (`root.activeTab = chatsTab` in `openPending`, `openRow`,
  `shareTo`; `root.showOnChats`) must be rewritten, not deleted: those functions also do
  `backToList()` and the push.
- **Pages outside the call and story sets use what goes.** `ChatPage.qml` and
  `VoicePlayer.qml` name the call or story objects (see below for `proximity`), and
  `ChatTitleBar.qml`, `ChatList.qml` and `ContactInfoPage.qml` carry call actions
  (`call.start`). A binding to an undefined name is a ReferenceError at run time; an
  `ActionItem` has no `visible`, so a call action is removed from the list, not hidden.
- **`Proximity` is not only for calls:** `VoicePlayer.qml` uses it (a note held to the ear moves
  to the handset, and `settings.earSensor` turns that off). It stays, with its context property.
- **`engine.call`, the `call` event and `callChanged`** stay in `EngineLink` (harmless, and
  Calls returns); the `stories*` signals go with their model.
- **`CallsModel` and `StoriesModel` each open `chats.db`** under their own connection name: not
  copying them also removes two of the four database opens (§3.4).

### 3.4 The encrypted database

**What:** `chats.db` is SQLCipher. `ChatListModel.cpp:77` and `MessageRows.cpp:192` open it with
`QSqlDatabase::addDatabase("QSQLITE", name)`; `Polls.cpp:27` and `Pins.cpp:61` reuse the
messages connection by name. One helper replaces the two opens: it registers a
`QSQLCipherDriver` (`ui/sqlcipher/`: Qt 4.8.6's own SQLite driver on TDLib's SQLCipher
amalgamation), opens read-only, and runs `PRAGMA key = "x'<db.key>'"`.

**Traps:**
- **The class must not be called `QSQLiteDriver`:** the phone's QtSql already has one, and the
  spike renames it. `addDatabase(QSqlDriver *)` takes one driver object per connection.
- **`PRAGMA key` is the first statement of every connection,** and a wrong or missing key
  raises no error there: the first real query fails with "file is not a database". The helper
  proves the key with one `SELECT` before it hands the connection out.
- **A raw key, not a passphrase:** `x'<64 hex>'` skips SQLCipher's 256,000 PBKDF2 rounds, which
  would cost about a second at every open on this phone.
- **The file may not be there, or not readable yet.** At the very first launch the UI starts
  before the engine has made `db.key` and `chats.db`. HyperBerry's models already retry at the
  next event ("Before the first link the engine hasn't made chats.db"); the helper must not
  leave a half-registered connection behind a failed try.
- **New with tengramd: the engine can delete and rebuild `chats.db`** (another schema version, a
  new key). A UI that keeps its read-only connection would read the deleted file for ever.
  Reopen the connections whenever `EngineLink` reconnects (`connectedChanged`).
- **Read-only and WAL:** the reader needs the `-shm` file, which the engine creates. Proven
  for HyperBerry, and for the encrypted file by the spike.
- **Building it with qcc:** the amalgamation is one 8 MB C file (compile it with `-w`, and
  without `SQLITE_OMIT_DECLTYPE`, which the driver needs); our `libcrypto.a` is built with clang
  and needs the `__aeabi_mem*` helpers (`ui/sqlcipher/aeabi_mem.c`) and `-lsocket`; qcc is C++98,
  so the helper has no `auto`, lambdas or `nullptr`.
- **A static `libcrypto.a` in an executable exports its symbols** (the first build had 2,862
  dynamic symbols, OpenSSL 3.5's `EVP_*` among them). A system library that uses the phone's own
  OpenSSL 1.0 would bind to ours and crash. `tengram.pro` links with
  `-Wl,--exclude-libs,ALL`; checked with `readelf --dyn-syms`: none left.
- **`sqlite3.c` does not compile under `-std=c99`** (`open`, `fcntl` undeclared in strict ISO
  mode): the project's C flag is `gnu99`, which qrcodegen and libwebp accept too.
- **Cost, measured on the phone in a cold process:** open and key 5 ms, the chat list's first
  30 rows 52 ms, a chat's newest 20 messages 38 ms. The warm figures are still to measure.

### 3.5 Login

**What:** tengramd's states are `linking`, `unlinked`, `qr` (`qr`: the `tg://login?token=…`
link), `code` (`phone`, `codeType`, `length`), `password` (`hint`), `ready` (`me`, `name`).
`LinkPage` keeps its QR part, with Telegram's wording (Settings → Devices → Link Desktop
Device). `LinkNumberPage` sends `link.phone`. New: a code field (`link.code`) and a password
field (`link.password`), shown by state.

**Traps:**
- **HyperBerry's `code` state means the opposite:** there the app shows a code to type on the
  phone; here the user types a code Telegram sent. The old block of `LinkPage` that prints
  `engine.link.code` must go, or it shows an empty big label.
- **Say where the code went.** `codeType` `app` means Telegram sent it to the account's other
  devices, not by SMS. On 2026-11-16 that looked like "no code arrived" for an hour. The page
  words each `codeType`, and offers the QR instead.
- **Telegram rate-limits login tries** (a `td` error 429, "Too Many Requests: retry after N"):
  show the wait; never retry on a timer.
- **`link.qr` answers `bad_state` while a phone step is being checked,** and the QR link expires
  and is replaced by a new one in a later `link` event: the page redraws on every event.
- **`nav.linked` waits for `engine.link.syncing`,** which tengramd does not send yet. Without
  it the chat list opens empty after a first login and fills over 5 to 9 s. The engine gains
  `syncing` (true until the first full chat list load is in).
- **New fields are text fields on a Q10:** setting `text` from code fires `onTextChanging`; the
  Enter that submitted comes back to a `KeyListener` (ignore key 13 for a second); a field
  refocuses when the app returns to full screen (pitfall "Focus, text fields").
- **`bb.system` dialogs and toasts are not Cascades controls** (no `onCreationCompleted`, need
  `import bb.system`); either mistake fails the whole page.

### 3.6 Message order and history paging

**What:** HyperBerry holds the whole history in `chats.db` and orders by `ts DESC, rowid DESC`
(`MessageRows.cpp:221`, `:246`); `rowid` is also the tie-breaker of the page conditions
(`MessageModel.cpp:143`, `:159`, `:191`, `:236`, `:270`) and of the sort (`:32`). In Tengram
older pages are written later, so `rowid` is wrong: the tie-breaker becomes the message id as a
number. The least change: select `CAST(id AS INTEGER)` where `rowid` is selected (`kColumns`),
keep `Msg::rowid` (a `qint64`), and replace `rowid` in the six SQL fragments.

And the history is not all there: `chat.open` verifies the newest 50 and answers `{oldest,
newest, count, more}`; `chat.history {chat, before, limit}` verifies the next older page. Rows older
than the oldest verified id may have gaps and are not shown.

**Traps:**
- **Rows may only arrive at an end of the model.** "An `itemsChanged(AddRemove)` inserting rows
  above the screen threw the list to the bottom; `itemAdded` at either end never moves the rows
  on screen" (pitfall "Lists"). HyperBerry guarantees it by loading whole days: a day's header
  sits above its rows, so a page must never land inside a loaded day. Tengram cannot load
  whole days (a busy group has more than a page a day, and each page is a network round trip).
  The rule that keeps insertions at the end: **the oldest loaded day has no header until an
  older day's message, or the start of the history, is loaded.** Older rows of the same day
  then append at the model's end, and the header appends after them when the day is complete.
- **A row is dear to read,** about 3 ms with its JSON and pictures (123 rows took 416 ms on the
  Q10). Keep pages at 50 and ask for the next when the first visible entry is within 25 of the
  top, as now; ask the engine one page ahead of the database running out, so the list never
  waits at the top for the network.
- **The first paint comes from the cache, before `chat.open` answers.** When the answer says
  `oldest`, loaded rows older than it are dropped (they are at the model's end, so that is
  safe) and come back verified with the next page.
- **A sent message changes id** (temporary, then Telegram's): the `msgs` event names both, the
  old row no longer reads back and a new one appears. In HyperBerry only `ts` moved. To check
  on the phone: whether the card flickers. If it does, the model renames the entry in place
  when one of our own rows goes and one comes in the same event.
- **Ids exceed 2^53.** They are strings across the socket and in QML, always (`before` in
  `chat.history` included); only SQL and C++ `qint64` treat them as numbers.
- **What meegram2 (`../../meegram2`, the N9's TDLib client) learned about history, and where
  Tengram stands** (read 2026-11-17: `docs/features.md`, `docs/troubleshooting.md`,
  `docs/profiling.md`, `src/MessageModel.cpp`):
  - *An empty or short `getChatHistory` answer is not the start of the history.* Short ones:
    the engine already asks again from the oldest message it got. Empty ones: meegram2 retried
    them on a 400 ms timer; TDLib's `get_dialog_history` already tries the database and the
    server itself (`left_tries`) before it answers empty, so the engine takes empty as the start
    and logs `history: nothing older than …`. If a chat stops paging early, that line says where.
  - *`from_message_id` with offset 0 returns messages strictly older than it; 0 means the newest,
    inclusive.* The engine uses exactly that. Never offset -1 anchored on the last message: it
    reaches `CHECK(!have_a_gap)` in `OrderedMessages::get_history` and aborts TDLib after a
    `closeChat` unloaded the chat.
  - *`updateDeleteMessages` with `from_cache` (not permanent) is TDLib unloading, not deleting;*
    it follows every `closeChat`, which our minimise sends. The engine already ignores it.
  - *A sent message comes twice* (temporary id, then the server's); the engine swaps the row
    and names both ids. The card flicker is still to check (above).
  - *Rows inserted in the middle of a list* showed one message twice in QML1's ListView;
    meegram2 resets the model then. Tengram inserts in the middle only when a gap is bridged
    (`itemAdded` per row): to check on the Q10.
  - *Ids cross into QML as strings* (QtScript turns large numbers into doubles): Tengram does.
  - *Opening position:* meegram2, as Telegram, opens at the first unread message and reports
    read up to the bottom-most visible one; Tengram (HyperBerry's way) opens at the newest and
    marked the chat read once its bottom was on screen. The user chose Telegram's way
    (2026-11-17, §8b). meegram2's other lesson applies to it: position on a bounded retry, never
    from a height-change handler (that never converged and froze its app); Tengram scrolls once
    and settles 300 ms later.
- **`scrollToPosition` right after the model is set stops short;** the chat is a bottom-to-top
  list that starts at its newest row with no scroll call. Nothing in the paging may add one.

### 3.7 Chat keys and Hub ids

**What:** WhatsApp's suffixes are tested in `ChatListModel.cpp:38` (a placeholder name from the
number), `MessageRows.cpp:53` (`readOnly` for `@newsletter`), and in the two models that are
not copied. Telegram's keys are bare numbers: a channel is `kind = 3` (the chat row has it),
and every chat has a title, so the placeholder is only "Unknown".

**Traps:**
- **`OpenRequests.cpp` finds a Hub item with `(?:chat|call)_([0-9a-f]+)` and decodes hex.**
  tengramd writes `chat_<decimal id>`, `n` for the minus sign. The old pattern still matches a
  decimal id and decodes it into garbage: change it, do not just test it.
- **Hub item ids take only letters, digits and `_`** (hence the `n`).
- **`pickColor` hashes the key with FNV-1a, not `qHash`,** because Qt 4's hash keeps the last
  character in its low bits and gave every WhatsApp key one colour. Numeric keys end in ten
  digits only: keep the FNV-1a.
- **`readOnly` (no reply, reaction, delete, vote)** was "is a newsletter". In Telegram it is
  "a channel where we cannot post", which the engine must tell; until it does, `kind = 3`.

### 3.7b Channels in the main list (user, 2026-11-17)

**What:** HyperBerry keeps channels on their own page, as WhatsApp does: `ChatListModel` has a
`Channels` mode (`kind = 3`) and filters them out of the other two (`kind != 3`), `main.cpp`
makes a third model `channelChats`, `ChatsPage` has a "Channels" action that opens
`ChannelsPage.qml`, which sends `channels.sync`. Telegram shows everything in one list: the
filters, the mode, the third model and the page go. `ChannelInfoPage`, `channel.info` and
`channel.unfollow` stay.

**Traps:**
- **The Active Frame's unread query also excludes `kind = 3`** (`ChatListModel.cpp`, the query
  on `muted_until <= ? AND unread != 0`): drop that too, or unmuted channels never show there.
- **More rows in the main list:** this account has 133 channels among 468 chats, which makes
  §3.8 matter more.

### 3.8 The chat list's cost under Telegram's traffic

**What:** `ChatListModel` re-reads every chat on each `chats` event (`reloadSoon` then
`reload`, marked `ponytail:` "a few ms for hundreds of chats; read only the changed jids if
accounts with thousands of chats make it show"), three times over (chats, archived, channels),
in the background too.

**Traps:**
- **That ceiling is reached on day one.** This account has 468 listed chats and several events
  a minute, and each row now costs a decryption. The event names the changed jids: read only
  those (the model's `chat(jid)` query exists) and move the rows.
- **Nothing should run with the screen off.** The battery target is 0 CPU for the UI in the
  background: while not fullscreen, remember that the list is stale and reload on
  `fullscreenChanged`; only the Active Frame's numbers need the event then.
- **`fullscreen()`, `thumbnail()` and `invisible()` can fire twice in a row;** the handlers are
  idempotent today and must stay so.

### 3.9 Wording

Every text the user reads says Telegram and Tengram: the pages, the sources' `tr()` strings and
the ten `.ts` files, replaced in one pass so sources and translations still match (§3.1).
`Label` text defaults to auto-detected format: any text a user wrote keeps
`textFormat: TextFormat.Plain`.

**Traps:**
- **Comments are not wording.** The first pass also turned "WhatsApp's 15 minutes" into
  "Telegram's 15 minutes" in comments. Where a comment states a WhatsApp rule that the code still
  applies, it says WhatsApp, so the list below can be found with `grep -rn WhatsApp app/`.
- **WhatsApp's rules still in the UI's logic,** each to be replaced by Telegram's or by what
  the engine says:
  - delete for everyone within about 2.5 days, edit within 15 minutes (`MessageRows.cpp:107`,
    `:112`); Telegram allows edits for 48 hours and deletes without a limit in most chats;
  - text styles typed as `*bold* _italic_ ~strike~` and "`mono`" (`Markup.h`, `MessageKinds.h`,
    `ChatRows.cpp:255`); Telegram's formatting comes as `extra.ent`;
  - typing: cleared after 25 s, re-sent every 5 s (`ChatController.cpp:38`, `:42`);
  - a poll's 12 options (`PollPage.qml`); pins holding 7 days (`Pins.h`); "forwarded many
    times" at a score of 5 (`MessageCard.cpp:534`); the 1600 px shrink of a sent picture
    (`MediaOpener.cpp:199`); the album rule (`Albums.h`), which happens to suit Telegram's albums;
  - group member errors 403 and 409 (`GroupInfoPage.qml:77`).
- **Invite links:** `Invites.cpp` now recognises `t.me/+…` and `t.me/joinchat/…`, but the engine
  has no `invite.info` yet: a tap shows the "couldn't open this invite" toast.

## 4. The engine, feature by feature

What HyperBerry's UI sends and listens for, where tengramd stands, and what to mind.

| UI feature | Commands, events, columns | In tengramd | Mind |
|---|---|---|---|
| Connect, state | `hello`, `ui.state`, `prefs.set`; `link`, `conn` | yes | `prefs.set` knows `mode`, `batchSecs`, `notifyPreview`. Settings also sends `mediaOnMobile`, `language`, `cacheMB` alone: today that answers `bad_request` and the page shows a failure. |
| Login | see §3.5 | yes, but no `syncing` | |
| Chat list | `chat.set`; `chats` | yes | |
| Chat page | `chat.open`, `chat.close`, `chat.read`, `chat.typing`, `msg.send` with `mentions`, `msg.edit`, `msg.delete`; `msgs`, `typing`, `presence` | all but `mentions`, `typing`, `presence` | Send `typing` only for the chat that is open: Telegram's groups produce these constantly. `presence` needs the person's status, which Telegram hides by their privacy settings. |
| Media received | `media.get`; `Sticker.cpp` asks for a sticker's file as its row is drawn | yes | Telegram's animated stickers are TGS and WebM, which `libwebp` cannot draw: the engine hands the static thumbnail for those. Documents go to TDLib's files folder; HyperBerry saved them to the shared Downloads (`access_shared`). |
| Media sent | `media.send` (path, thumb, caption) | no | The UI makes the thumbnail (`QImage`); TDLib wants the file and makes its own. |
| Voice notes | `voice.start`, `voice.send`, `voice.cancel`, `voice.play`; `waveform`, `duration`, `played` | columns yes, commands no | Needs Opus in the engine both ways: HyperBerry encodes the recorder's WAV as it grows and decodes notes to WAV for the player. libopus on QNX builds only with `--disable-rtcd` and `-mfpu=neon`. |
| Reactions | `msg.react`; the `reactions` table, one row per reactor | no | Telegram gives counts per emoji and a few recent reactors, not a row per person; the table's meaning has to bend (a row per emoji with a count) or the bubble's query changes. |
| Polls | `poll.vote`, `poll.create`; `poll_votes`, one row per voter | options only | Most Telegram polls are anonymous: counts, no voters. Same bend as reactions. |
| Clear, delete, export | `chat.clear`, `chat.delete`, `chat.export` | no | Deleting a group in Telegram means leaving it; say so in the dialog. |
| Group page | `group.info`, `group.participants`, `group.create`; `photo` | no | A supergroup can have 200,000 members: `group.info` cannot answer with all of them in one line (the socket's limit is 1 MiB). Page them. |
| Contact page | `contact.info`, `contact.block` | no | |
| New chat | `contacts.list`, `contacts.book` | no | `contacts.book` hands the phone's address book to the engine. For Telegram that means uploading it to Telegram's servers: a setting the user turns on, never a default. |
| Invites | `invite.info`, `invite.accept`, `group.join` | no | |
| Profile | `profile.get`, `profile.set` | no | |
| Channel info | `channel.info`, `channel.unfollow` | no | The separate Channels page and `channels.sync` are gone (§3.7b). |
| Starred messages | `msg.star`; `starred` | Telegram has none | The page and the card's Star action go. |
| Pins, forwards, albums, spoilers | notice rows with `extra.msg`/`until`, `extra.forwarded`, album grouping, `extra.spoiler` | not written yet | `Albums.h` has the grouping rule the rows must satisfy. |
| Mentions, links, formatting | `extra.mentions` | `extra.ent` (UTF-16 offsets) | The cards read `mentions` today (`MessageKinds`, `Markup.h`); they learn `ent`, which also carries bold, links and code. |
| Calls | `call.*`; `call` | later | |
| Stories | `story.*` | out | |

## 5. Packaging, for the day it comes

- **The service entry point gets no arguments we choose.** tengramd wants `<data-dir>
  --service`: started with none, it must take the data folder from `HOME` (the sandbox's
  `data`, which is `QDir::homePath()` for the UI) and run as the service.
- **The API id and hash come from the environment today.** The launcher sets none: they are
  compiled in at build time, from the build's environment, never from a file in the repository.
- **A headless entry point is slain if it has not connected to the navigator 6 s after
  launch,** and gets a 3 MB heap without `_sys_headless_xlarge`. Both show only in the
  navigator's log. The BPS thread starts before TDLib.
- **The Hub needs `_sys_access_pim_unified`,** never prompted; a permission missing from the
  descriptor fails silently. The UDS library is loaded with `dlopen` and pulls in QtCore and
  QtGui: `LD_LIBRARY_PATH` in the descriptor must reach `/usr/lib/qt4/lib` (HyperBerry's does).
- **Hub icons are public assets** (`public="true"`, read from `/apps/<pkg>/public/`).
- **Root cannot execute an installed app's binaries:** an installed build is tested through
  the navigator. The user installs every `.bar`.
- **A background app cannot invoke.** `EngineLink` backs off (doubling to 60 s) when START is
  refused, after a retry every 500 ms kept the CPU busy for 18 hours. Keep the back-off.
- **The boot filter also fires right after an install.**

## 6. How each change is checked before the phone sees it

- **Compile without packaging:** `qmake` and `make` in the SDK image, in `build/ui`, about 30 s
  when little changed. Two builds at once need two folders.
- **Parse every QML and JS file with the SDK's `lupdate`** (Qt 4.8's parser), as
  `build-app.sh` does: the Qt 6 lint let through a `var` inside a binding block that failed a
  whole page on the phone. QML's JavaScript is ES5; there are no QtQuick visual items, no
  `Component.onCompleted`, no `Connections` in Cascades' QML (connect in `onCreationCompleted`).
- **The SDK lint against Cascades' type info** (`cascades-qmllint.sh` in HyperBerry's notes;
  the `bb10-qmllint` image exists, the script was not found under `../`: to locate).
- **The editor's red C++ errors are noise** (no BB10 SDK on the Mac). The SDK's compile is the
  check.
- **Docker under OrbStack may read a file just edited at its old size:** run again, and after
  a package check that the bar holds the change. And the Mac must be kept awake for long runs.

## 7. Order of work

1. **The fork that builds:** §3.1 to §3.4, §3.7, §3.7b, §3.9. Calls and Stories sources are not
   copied; they stay in HyperBerry for the day Calls returns.
2. **Login and history:** §3.5, §3.6, then §3.8.
3. **Engine, what the chat page shows live:** `syncing`, the three `prefs.set` fields, `typing`
   and `presence`, `mentions`, pins, forwards, albums and spoilers in `extra`.
4. **Engine, the pages around the chat:** `contact.info`, `group.info` and `photo`,
   `profile.get`, `contacts.list`, `channel.info`, `chat.clear`, `chat.delete`,
   `channel.unfollow`, reactions, `poll.vote`.
5. **Engine, sending and managing:** `media.send`, `poll.create`, `group.participants`,
   `group.create`, invites, `contact.block`, `profile.set`, `chat.export`.
6. **Voice notes,** then **Calls.**

## 8. State after step 1 (2026-11-17), and what to test first on the phone

`scripts/build-ui.sh` builds `build/ui/app/tengram` (5.2 MB) and the SDK's `lupdate` parses every
QML and JS file. Nothing of it has run. In order of doubt:

1. **`ChatsDb`** (`app/src/ChatsDb.cpp`) has never opened a real `chats.db`: the spike proved the
   driver and the key, not this helper, its retry when the file is missing, or its reopen after
   the engine reconnects.
2. **The application menu on the `NavigationPane`** (it sat on the `TabbedPane`).
3. **A tap on a Hub item** (`chat_<id>`), which needs the packaged engine too.
4. **Leftovers that only log:** `MessageCardActions.cpp` still calls `openStory` for a quote
   flagged as a story, which Telegram's rows never are; `ChatList.channelList` and its Unfollow
   row menu have no page that sets them; a starred mark is still drawn for `starred = 1`, which
   the engine never writes.
5. **`LinkPage.qml`'s code block** still carries HyperBerry's pairing-code wording: step 2 (§3.5).
6. **`Nv12Rotate.h` is back** (the video window includes it); twenty imports of the module were
   ten once the call and story pages were gone.

## 8b. State after step 2 (2026-11-17), and what to test on the phone

Three commits (`809fe98`, `b954fe0`, `1a2c5ea`); all compile in the SDK image, none has run.

- **Login (§3.5).** Found while doing it: TDLib refuses both `setAuthenticationPhoneNumber`
  and `requestQrCodeAuthentication` in WaitOtherDeviceConfirmation (our `qr`) and takes both in
  WaitCode and WaitPassword (`AuthManager.cpp`). The engine's checks were the other way round;
  they follow TDLib now. From the QR, "Link with phone number" sends `link.logout` first (no
  session exists yet) and the number page waits for `unlinked`. A failed step now answers before
  the state goes back, so a refused number asked from `code` shows its error.
  To test: a phone login with a real code (the code goes to the other devices: `codeType`
  `app`), a wrong code, the password step, QR → phone → QR. Telegram rate-limits tries.
- **History (§3.6).** The floor from `chat.open`, `chat.history` one page ahead, the oldest day's
  header held back. A later `chat.open` that does not reach the verified run asks up to 3 pages
  of 100 back to bridge it; past that the run restarts at the bridge and older rows go. To test: a
  busy group scrolled back several pages (no jump, no gap, headers appearing above their day), a
  chat opened for the first time (empty cache), a sent message (the id changes: does the card
  flicker?), the pinned-message jump, a gap after a long time minimised in a busy group.
- **Opening at the first unread (user, 2026-11-17: Telegram's way).** `chats.read_inbox` (schema 3,
  migrated in place from 2; a migrated row stays 0, which opens at the newest, until the engine
  writes it again); the model loads back to the oldest incoming message after it when the cache
  holds all `unread` of them within 150 rows, puts an "Unread messages" header above it, and
  `chat.open` verifies that far (`limit`, back to 50 after the first reply). The chat is no longer
  read on opening: a settle timer 300 ms after positioning reports whether the list's bottom shows.
  When the cache lacks some of the unread rows, or more than 150 unread, the model opens in a
  *window* (the `window` property of `MessageModel`): the page keeps its spinner while
  `chat.newer {after: read_inbox}` verifies one page after the last read message, the model shows
  that page with the divider above its first incoming row, and older pages come as always
  (`chat.history`). Newer pages come as the list nears its bottom: the rows `chat.newer` verified are
  loaded (`loadNewer`), the next page is asked a page ahead (`askNewer`), and when the verified run
  reaches Telegram's newest message `joinLive` leaves window mode and loads the live rows that were
  skipped meanwhile. While in the window the list's stick-to-edge policy is off (a newer page lands
  at index 0 and must not drag the view), a live row beyond the loaded newest is dropped (it comes
  with the window), `chat.open` replies are ignored, and the list's bottom (the newest loaded row) is not the chat's.
  Reading, in a window or not, follows what is shown, as Telegram's: once the page has positioned
  the list, each scroll reports `chat.read {upto}` the bottom-most message on screen, at most once a
  second (`ChatController::nearTop`, `sendReadUpto`); at the chat's real bottom `chat.read` reads all. Sending from the window (a text, a voice
  note, a file) drops it first (`toLiveEnd`, the controller sends `chat.open` again): the chat
  jumps to the newest, as Telegram's does. A window that gets no answer in 15 s, or an empty one,
  does the same and opens at the newest. A search match or a pinned message the window doesn't hold
  leaves it too (user, 2026-11-17), but keeps the spinner until `chat.open` sets the floor, since a
  jump back to a message needs it; the page's pending jump then runs (a give-up after 15 s ends the
  spinner without the jump). Needs the engine's `chat.newer` and `chat.read upto`.
  To test: where `scrollToItem` leaves the divider (top or bottom of the screen); a chat whose
  unread fit on one screen is read at once; one scrolled to the bottom is read then, not before;
  a group with hundreds unread (opens at the divider after the spinner; scrolling down pages newer
  and joins the newest; a message sent from the window jumps to the newest; the unread count drops
  as you read down; the view does not jump when a newer page lands at the bottom); a chat whose
  cache lacks unread rows (a few unread, an empty cache) opens in a window of one page that joins
  the newest at once; a search match or a pinned message outside a window leaves it, then lands,
  and one inside it lands without leaving; the log's `chat: window of N messages` and `chat: no window after` lines say
  which way a chat went.
- **Engine restarts keep the chat list (user, 2026-11-17, 0.1.0.8).** The engine reports `linking` at every start
  while TDLib opens; `EngineLink::setLink` now keeps the known link state through it, so the chat list stays and
  its title bar shows "Connecting…" with a spinner instead of the linking page's "Starting…". To test: reopen
  the app, kill the engine (it comes back), Install/Close of the memory patch, a phone restart.
- **Chat list (§3.8).** Nothing read while not fullscreen; rows that keep their place are patched.
  Moves still reload: look for `chat list: reload, … ms` in the log to decide whether moves need
  a patch too. `fullscreen()` firing twice is harmless (the second finds nothing stale).

- **Memory patch (in-app) (2026-11-17, not run).** The engine runs under 30 MiB unless `../rlimit-patch`'s
  `headless-mem` addon has patched authman (`docs/headless-cap.md`). The engine reports its `RLIMIT_DATA` as
  `memMB` in every link body; `main.qml`'s `checkMemory()` (on `engine.linkChanged` and at start) shows a
  `SystemDialog`, once per run, with one button, Install, when it is connected, `0 < memMB < 128`, the addon is
  not in `/var/addons/headless-mem`, and no install is running. Install calls `MemoryPatch::install()`: the
  UI runs the root installer through AutoRoot's `__root`, started as a new application by the bundled
  `tg-install-patcher` (the way VoLTE Manager's `AddonSetup` does), then `slay -f tengramd`, so that the UI's
  START brings the engine back under the new limit. A toast says the result; the output is saved as
  `tengram-memory-patch.txt` in Documents. Without root a dialog says so, once per run.
  To test, on a phone **without the addon** (the user's has it: delete `/var/addons/headless-mem`, reboot, and
  wait the 4 minutes for root before opening Tengram, or the `installed` check is what is under test, not the
  dialog): the dialog shows at first launch, before login, with no Cancel; Back or a tap outside shows it
  again at the next link event; Install, then `rlimit DATA: soft 134217728` in `logs/tengramd.log` for the
  restarted engine and `memMB` 128 in the link body (`engine: connected` in `ui.log`); closing Tengram right after
  Install does not stop the install (`[HMEM]` lines in `/var/log/installer.log`, the engine still gets restarted);
  a failed install (take the zip away: it says so) toasts and brings the dialog back; a phone **with** the addon
  shows nothing, also right after a reboot, when the engine reports 30 for the first 4 minutes. The no-root dialog
  (no `/base/bin/__root` or `u_root`) is a fallback only: an unsigned app runs only on a rooted phone (the
  user), so no phone can show it.

## 9. Open points

- **The address book** (§4, New chat).
- **Reactions and poll votes:** bend the tables or change the bubbles' queries (§4).
- **The QML lint script** is to be found (§6).
