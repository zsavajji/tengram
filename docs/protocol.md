# Tengram: the UI ↔ engine protocol

The contract between the Cascades UI (`tengram`) and the engine (`tengramd`). It is HyperBerry's
(`../hyperberry/docs/protocol.md`) with Telegram's meanings, so the forked UI keeps its code.
Where this file is silent about a command, the engine does not have it yet (`unknown_cmd`).
When the code changes, this file changes with it.

## Files in the data folder

| file | what |
|---|---|
| `db/`, `files/` | TDLib's own database and files. Only the engine touches them. |
| `chats.db` | What the UI shows, written by the engine, read by the UI. SQLCipher 4, WAL. A cache: the engine deletes and rebuilds it when it can't be opened, its `user_version` isn't the engine's, or `db.key` is new. |
| `db.key` | The key of both databases: 64 hex characters (32 bytes read from `/dev/random` at the first launch), mode 0600. The engine hands it to TDLib as its database key; the UI opens `chats.db` with `PRAGMA key = "x'<the 64 characters>'"` (a raw key: no key derivation). There is no setting to turn encryption off and no passcode. |
| `engine.json` | `{"port": 41234, "token": "<32 hex characters>"}`, mode 0600, written as a temporary file and renamed, new at every start, removed at a clean stop. |

## Connecting and framing

As HyperBerry's: TCP on `127.0.0.1`, the port and token from `engine.json`; the first line must
be `hello` with the token (`auth` error and close otherwise; a wrong `proto` gets `proto` and
closes). One JSON object per line, UTF-8, `\n` at the end, at most 1 MiB a line.

- **Requests:** `"@"` (an id the UI chooses, a string), `"cmd"`, and the command's fields.
- **Replies:** `{"@":"7","ok":true}` with `"result":{…}` when there is one, or
  `{"@":"7","ok":false,"error":{"code":"…","message":"…"}}`. **Unlike HyperBerry's, replies can
  come in any order**: match on `"@"`.
- **Events:** `"ev"` and no `"@"`. Every authenticated connection gets every event.
- A connection with more than 1 MiB waiting to be sent is closed. Reconnect and re-read
  `chats.db`: events only say which rows to re-read.
- Chat keys and message ids are strings: TDLib's numbers in decimal (`"-1001234567890"`,
  `"123456789"`). Never parse them as numbers in the UI (they exceed 2^53 in QML).

Error codes every command can give: `bad_request` (a field is missing or of the wrong type),
`unknown_cmd`, `unlinked` (not logged in), `bad_chat` (no such chat), `td` (TDLib refused:
`message` is `<code> <TDLib's message>`).

## Requests

| cmd | fields | result | own errors |
|---|---|---|---|
| `hello` | `token`, `proto: 1` | the link body | `auth`, `proto` |
| `link.qr` | — (states `unlinked`, `code`, `password`) | — (the code comes as a `link` event, state `qr`) | `linked`, `bad_state` |
| `link.phone` | `phone` (international, non-digits ignored; states `unlinked`, `code`, `password`) | — (then state `code`) | `linked`, `bad_state`, `bad_phone` |
| `link.code` | `code` | — (then `password` or `ready`) | `bad_state`, `bad_code` |
| `link.password` | `password` | — (then `ready`) | `bad_state`, `bad_password` |
| `link.logout` | — | — | — |
| `ui.state` | `fullscreen` (bool), `openChat` (key or "") | — | — |
| `chat.open` | `chat` | `{oldest, newest, count, more}` | |
| `chat.history` | `chat`, `before` (a message id), `limit` (1 to 100, default 50) | `{oldest, newest, count, more}` | |
| `chat.close` | `chat` | — | |
| `chat.read` | `chat` | — | |
| `chat.typing` | `chat`, `on` (bool) | — | |
| `chat.set` | `chat`; optional `pin` (bool), `mute` (unix seconds until; 0 off; -1 always), `archive` (bool), `unread` (bool); at least one | — | |
| `msg.send` | `chat`, `text`, `quote` (a message id, optional), `mentions` ([{`jid`, `name`}], optional) | `{id}` | |
| `msg.delete` | `chat`, `ids` [message ids], `everyone` (bool) | — | |
| `msg.edit` | `chat`, `id`, `text` (non-empty: the new text, or the new caption of a photo or video) | — | `not_found` |
| `media.get` | `chat`, `id` (a message with a file) | `{path}`, the absolute path of the file | `not_found`, `download_failed` |
| `prefs.set` | `mode` (`auto`, `instant` or `batched`), `batchSecs` (60 to 3600), `notifyPreview` (bool), `mediaOnMobile` (bool), `language` (string), `cacheMB` (64 to 512); each optional, at least one | — | |

**`link.*`:** the login. `link.qr` asks Telegram for a login link to show as a QR code
(`requestQrCodeAuthentication`); `link.phone`, `link.code` and `link.password` are the phone
number, the code Telegram sends and the two-step password. Each answers when TDLib took it;
the new state comes as a `link` event. `bad_state` is a command the current state doesn't expect.
`link.qr` and `link.phone` are accepted only in `unlinked`, `code` and `password` (TDLib refuses
both while a QR code waits, state `qr`, with "unexpected"); any other state answers `bad_state`.
From `qr` the way to a phone login is `link.logout`, which ends in `unlinked` (nothing is lost:
no session exists yet), then `link.phone`.
`link.logout` logs out on Telegram's side, empties `chats.db` (a `chats` event names the rows
that went) and starts a fresh TDLib client.

**`ui.state`:** `fullscreen: true` sets TDLib's `online` option, `false` clears it. It also
drives the delivery mode (below). A connection that closes counts as `fullscreen: false`.

**`chat.open`:** the UI paints the chat from `chats.db` first and sends this at the same time.
The engine opens the chat in TDLib and makes the newest 50 messages in `chats.db` exactly
TDLib's (rows added, changed and removed, each with `msgs` events). The reply comes after that
commit: `oldest` is the oldest id of that verified page ("" for an empty chat), `newest` its
newest id ("" for an empty chat; the page is exactly Telegram's from `oldest` to `newest`, which
lets the UI tell whether a later page reaches the rows it verified before), `count` how many
it holds, `more` whether older messages exist. Rows older than `oldest` may be stale or have
gaps: the UI shows them only after a `chat.history` covered them.

**`chat.history`:** the same (`newest` included) for the page of `limit` messages older than
`before`. Served from TDLib's local database when it has them, from Telegram otherwise.

**`chat.read`:** marks the chat read up to its newest message. `unread` changes with the
`chats` event that follows.

**`chat.set`:** `pin`, `archive`, `unread` and `mute` as HyperBerry's. The row changes when
Telegram's echo arrives (a `chats` event), not before.

**`msg.send`:** a text. `mentions` are the people the text's "@name"s point at: `jid` the
user id, `name` what the text reads after the "@". Each "@name" found in the text becomes a
mention of that user; one that is not in the text, or whose `jid` is not a user id, is skipped. The row is in `chats.db` as *sending* (state 0) under a temporary id
before the reply comes, and `{id}` is that id. When Telegram takes the message the row is
replaced by one with the final id; a `msgs` event names both ids. On failure the row stays,
state 4.

**`msg.delete`:** `everyone: true` deletes for all sides where Telegram allows it, `false` only
here. The rows go when Telegram confirms (a `msgs` event names them: they no longer read back).

**`msg.edit`:** the row's `kind` decides: a text is edited as text, a photo or video gets a new
caption; any other kind is `bad_request`, a message `chats.db` doesn't hold `not_found`. The row
changes when Telegram's echo arrives.

**`media.get`:** downloads the message's file (a photo's largest size) into TDLib's files folder
and answers when it is complete, after the row has `media_state` 2 and `path` (a `msgs` event).
A file that is already here answers at once. Nothing downloads by itself: the UI asks for what it
shows. `not_found` is a message TDLib doesn't have or one without a file.

**`prefs.set`:** the fields given are merged into the stored ones, kept in `prefs.json` in the
data folder. `mode` and `batchSecs` are the delivery mode's settings (below), `notifyPreview`
whether a notification shows the message's text. `mediaOnMobile`, `language` and `cacheMB` are
HyperBerry's (the UI sends them on every connect): the engine keeps them and does nothing with
them yet (nothing downloads by itself, previews are worded in English, TDLib's file cache is
not pruned). Defaults: `auto`, 300, true, true, "", 64.

## Events

| ev | fields | meaning |
|---|---|---|
| `link` | the link body | the login state changed |
| `conn` | `state`: `offline`, `connecting`, `ready` | the connection to Telegram changed |
| `chats` | `jids`: [chat keys] | these rows of `chats` changed or disappeared: re-read them |
| `msgs` | `chat`, `ids`: [message ids] | these rows of `messages` changed or disappeared: re-read them |
| `typing` | `chat`, `who` (first name in a group, "" in a 1:1 chat), `on`, `voice` (recording a voice note) | someone started or stopped typing; only for chats between their `chat.open` and `chat.close` |
| `presence` | `chat`, `online`, `lastSeen` (unix seconds, 0 when hidden) | the other person's online status in a 1:1 chat, once after `chat.open` and at every change until `chat.close` |

`chats` and `msgs` come after the engine commits, at most every 50 ms.

**The link body** (the `hello` result and every `link` event): `state` and `conn`, plus:

| `state` | TDLib's state | also |
|---|---|---|
| `linking` | starting, or a step is being checked | |
| `unlinked` | waiting for a phone number | |
| `qr` | waiting for another device to scan | `qr`: the `tg://login?token=…` link to draw |
| `code` | waiting for the code | `phone`, `codeType` (`app`, `sms`, `call`, `other`), `length` (0 unknown) |
| `password` | waiting for the two-step password | `hint` |
| `ready` | logged in | `me` (our user id), `name` |

`syncing: true` goes with `ready` from a login until the whole chat list is in `chats.db` for
the first time (a `link` event without it follows). The UI waits on the linking page meanwhile.

## Delivery mode

How quickly messages reach the phone while the UI is closed, against the battery:

- **instant:** the connection to Telegram stays open.
- **batched:** `setNetworkType(None)` between syncs. Every `batchSecs` the engine connects,
  waits until TDLib is `ready` and no update came for 2 s (60 s at most), and disconnects.
- **auto** (the default): instant while a UI connection says `fullscreen: true`. With the UI
  closed: batched while our own account is online on another device (`updateUserStatus` for
  our user says online, which can only be another session since ours is `online=false`),
  instant otherwise. A sync that finds us offline elsewhere goes back to instant.

## Notifications: the Hub account

No request of its own. The engine keeps a "Tengram" account in the BlackBerry Hub while logged
in, with one item per chat that got a notification. What deserves one is TDLib's decision
(its notification groups): mutes, mentions in muted chats, silent messages, messages already
read on another device, and the delay while another device is in use are all Telegram's rules.
- An item's unread count is the number of notifications TDLib holds for the chat; when that
  drops to 0 (read here or anywhere) the item shows as read.
- It alerts (sound, LED, instant preview) unless Telegram says the notification is silent, or
  the chat is the `openChat` of a UI that is in front.
- The text is the chat list's preview of the message, or "New message" when `notifyPreview` is
  false (default true) or Telegram hides the preview.
- `hub.json` in the data folder records the account and the chats that have items.
- Calls are not notified yet.

## chats.db, as the UI reads it

HyperBerry's tables and columns (`../hyperberry/docs/protocol.md`, *chats.db, as the UI reads
it*), with these meanings for Telegram. `user_version` is 2.

**`chats`:** one row per chat in Telegram's main or archived chat list, and no others.

| column | meaning |
|---|---|
| `jid` | the chat id |
| `kind` | 0 a person (private and secret chats), 1 a group (basic groups and supergroups), 3 a channel |
| `name` | the title |
| `photo` | the absolute path of the small chat photo once TDLib has downloaded it, NULL before |
| `last_ts`, `last_preview`, `last_out`, `last_state` | the newest message: unix seconds (0 when the chat is empty), the one-line preview ("You: hi", "Anna: Photo"; no name in a 1:1 chat or a channel), ours or not, its state |
| `unread` | the unread count; -1 for a chat marked unread with none |
| `muted_until` | unix seconds; 0 not muted; 2^40 muted forever |
| `pinned` | 1 when pinned in the main list |
| `archived` | 1 when in the archive |

**`messages`**, primary key `(chat, id)`. Order by `ts`, then `CAST(id AS INTEGER)` (not
`rowid`: older pages are inserted later).

| column | meaning |
|---|---|
| `chat`, `id` | the chat id and the message id |
| `ts` | unix seconds |
| `out` | 1 if ours |
| `state` | ours only: 0 sending, 1 sent, 3 read, 4 failed (Telegram has no "delivered") |
| `sender`, `sender_name` | groups only: the sender's user or chat id, and their name at the time |
| `kind` | HyperBerry's numbers: 0 text, 1 photo, 2 video (also GIFs and round videos), 3 voice note, 4 document, 5 sticker, 6 service, 7 other, 8 audio file, 9 location, 10 contact, 11 poll, 13 notice |
| `text` | the text, a photo's or video's caption, a document's file name, a location's title and address, a contact's name, a poll's question, a service message's English wording, a notice's code, "Message" for kind 7 |
| `quote_id`, `quote_preview` | the message this one replies to (same chat only) and its one-line preview when `chats.db` holds it |
| `edited` | 1 when edited |
| `duration` | voice notes, videos, audio files: seconds |
| `waveform` | voice notes: 64 values, 0 to 100 |
| `media_state` | 0 none, 1 on the server, 2 here at `path` |
| `thumb` | photos, videos, GIFs, documents: Telegram's mini thumbnail, a small JPEG; NULL when none |
| `width`, `height`, `size` | as HyperBerry's |
| `path` | the downloaded file's absolute path with `media_state` 2 |
| `played` | voice notes and round videos: 1 once listened to |
| `sig` | the engine's own: a hash of the row's other values, so an unchanged message is not written again. Not for the UI. |
| `extra` | JSON, NULL when empty: `gif: true`; a sticker's `animated: true` and `emoji`; a location's `lat`, `lon`; a contact's `contacts: [{name, tel: [number]}]`; a poll's `options`, `max` (1, or 0 for several); a text's link preview `title`, `desc`, `url`; a document's `caption`; an audio file's `title`, `performer`; a call's `secs` |

Any kind's `extra` can also carry `forwarded: true` (the message is a forward) and, on a photo or
video, `spoiler: true` (Telegram covers it until tapped).

Any kind's `extra` can also carry `ent`, the formatting of `text` (or of a document's
`extra.caption`): a list of `[offset, length, type]` or `[offset, length, type, argument]`, in
Telegram's order. Offsets and lengths are in UTF-16 code units, which is what a `QString` counts.
Types: `b` bold, `i` italic, `u` underline, `s` strikethrough, `sp` spoiler, `c` code, `p`
preformatted (argument: the language, when given), `q` block quote, `a` a link (argument: the
URL; absent when the text itself is the URL), `m` a mention (argument: the user id, when the
mention is by name), `h` hashtag, `cmd` bot command, `e` e-mail address, `t` phone number.
Other entity types are left out.

A notice's `text` is `call`, `video_call`, `missed_call` or `missed_video_call`.
`deleted`, `media` and `starred` are unused (0 or NULL). `reactions` and `poll_votes` exist and
are empty for now.
