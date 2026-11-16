# Tengram: the UI ↔ engine protocol

The contract between the Cascades UI (`tengram`) and the engine (`tengramd`). It is HyperBerry's
(`../hyperberry/docs/protocol.md`) with Telegram's meanings, so the forked UI keeps its code.
Where this file is silent about a command, the engine does not have it yet (`unknown_cmd`).
When the code changes, this file changes with it.

## Files in the data folder

| file | what |
|---|---|
| `db/`, `files/` | TDLib's own database and files. Only the engine touches them. |
| `chats.db` | What the UI shows, written by the engine, read by the UI. SQLCipher 4, WAL. A cache: the engine deletes and rebuilds it when it can't be opened or its `user_version` isn't the engine's. |
| `chats.key` | The key of `chats.db`: 64 hex characters (32 random bytes), mode 0600, made by the engine when missing. Open with `PRAGMA key = "x'<the 64 characters>'"` (a raw key: no key derivation). |
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
| `link.qr` | — | — (the code comes as a `link` event, state `qr`) | `linked`, `bad_state` |
| `link.phone` | `phone` (international, non-digits ignored) | — (then state `code`) | `linked`, `bad_state`, `bad_phone` |
| `link.code` | `code` | — (then `password` or `ready`) | `bad_state`, `bad_code` |
| `link.password` | `password` | — (then `ready`) | `bad_state`, `bad_password` |
| `link.logout` | — | — | — |
| `ui.state` | `fullscreen` (bool), `openChat` (key or "") | — | — |
| `chat.open` | `chat` | `{oldest, count, more}` | |
| `chat.history` | `chat`, `before` (a message id), `limit` (1 to 100, default 50) | `{oldest, count, more}` | |
| `chat.close` | `chat` | — | |
| `chat.read` | `chat` | — | |
| `chat.typing` | `chat`, `on` (bool) | — | |
| `chat.set` | `chat`; optional `pin` (bool), `mute` (unix seconds until; 0 off; -1 always), `archive` (bool), `unread` (bool); at least one | — | |
| `msg.send` | `chat`, `text`, `quote` (a message id, optional) | `{id}` | |
| `prefs.set` | `mode` (`auto`, `instant` or `batched`), `batchSecs` (60 to 3600); each optional, at least one | — | |

**`link.*`:** the login. `link.qr` asks Telegram for a login link to show as a QR code
(`requestQrCodeAuthentication`); `link.phone`, `link.code` and `link.password` are the phone
number, the code Telegram sends and the two-step password. Each answers when TDLib took it;
the new state comes as a `link` event. `bad_state` is a command the current state doesn't expect.
`link.logout` logs out on Telegram's side, empties `chats.db` and starts a fresh TDLib client.

**`ui.state`:** `fullscreen: true` sets TDLib's `online` option, `false` clears it. It also
drives the delivery mode (below). A connection that closes counts as `fullscreen: false`.

**`chat.open`:** the UI paints the chat from `chats.db` first and sends this at the same time.
The engine opens the chat in TDLib and makes the newest 50 messages in `chats.db` exactly
TDLib's (rows added, changed and removed, each with `msgs` events). The reply comes after that
commit: `oldest` is the oldest id of that verified page ("" for an empty chat), `count` how many
it holds, `more` whether older messages exist. Rows older than `oldest` may be stale or have
gaps: the UI shows them only after a `chat.history` covered them.

**`chat.history`:** the same for the page of `limit` messages older than `before`. Served from
TDLib's local database when it has them, from Telegram otherwise.

**`chat.read`:** marks the chat read up to its newest message. `unread` changes with the
`chats` event that follows.

**`chat.set`:** `pin`, `archive`, `unread` and `mute` as HyperBerry's. The row changes when
Telegram's echo arrives (a `chats` event), not before.

**`msg.send`:** a text. The row is in `chats.db` as *sending* (state 0) under a temporary id
before the reply comes, and `{id}` is that id. When Telegram takes the message the row is
replaced by one with the final id; a `msgs` event names both ids. On failure the row stays,
state 4.

**`prefs.set`:** kept in `prefs.json` in the data folder. `mode` and `batchSecs` are the
delivery mode's settings (below). Defaults: `auto`, 300.

## Events

| ev | fields | meaning |
|---|---|---|
| `link` | the link body | the login state changed |
| `conn` | `state`: `offline`, `connecting`, `ready` | the connection to Telegram changed |
| `chats` | `jids`: [chat keys] | these rows of `chats` changed or disappeared: re-read them |
| `msgs` | `chat`, `ids`: [message ids] | these rows of `messages` changed or disappeared: re-read them |

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

## Delivery mode

How quickly messages reach the phone while the UI is closed, against the battery:

- **instant:** the connection to Telegram stays open.
- **batched:** `setNetworkType(None)` between syncs. Every `batchSecs` the engine connects,
  waits until TDLib is `ready` and no update came for 2 s (60 s at most), and disconnects.
- **auto** (the default): instant while a UI connection says `fullscreen: true`. With the UI
  closed: batched while our own account is online on another device (`updateUserStatus` for
  our user says online, which can only be another session since ours is `online=false`),
  instant otherwise. A sync that finds us offline elsewhere goes back to instant.

## chats.db, as the UI reads it

HyperBerry's tables and columns (`../hyperberry/docs/protocol.md`, *chats.db, as the UI reads
it*), with these meanings for Telegram. `user_version` is 1.

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
| `extra` | JSON, NULL when empty: `gif: true`; a sticker's `animated: true` and `emoji`; a location's `lat`, `lon`; a contact's `contacts: [{name, tel: [number]}]`; a poll's `options`, `max` (1, or 0 for several); a text's link preview `title`, `desc`, `url`; a document's `caption`; an audio file's `title`, `performer`; a call's `secs` |

A notice's `text` is `call`, `video_call`, `missed_call` or `missed_video_call`.
`deleted`, `media` and `starred` are unused (0 or NULL). `reactions` and `poll_votes` exist and
are empty for now.
