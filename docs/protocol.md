# Tengram: the UI ↔ engine protocol

The contract between the Cascades UI (`tengram`) and the engine (`tengramd`). It is HyperBerry's
(`../hyperberry/docs/protocol.md`) with Telegram's meanings, so the forked UI keeps its code.
Where this file is silent about a command, the engine does not have it yet (`unknown_cmd`).
When the code changes, this file changes with it.

## Files in the data folder

| file | what |
|---|---|
| `db/`, `files/` | TDLib's own database and files. Only the engine touches them. |
| `chats.db` | What the UI shows, written by the engine, read by the UI. SQLCipher 4, WAL. A cache: the engine deletes and rebuilds it when it can't be opened, its `user_version` isn't the engine's (versions 2, 3 and 4 are migrated in place to 5; the step to 4 empties `messages` and the step to 5 drops the unused `reactions` and `poll_votes` tables, see *chats.db*), or `db.key` is new. |
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
| `chat.open` | `chat`, `limit` (1 to 200, default 50); optional `topic` (a forum topic's id) | `{oldest, newest, count, more}` | |
| `chat.history` | `chat`, `before` (a message id), `limit` (1 to 100, default 50); optional `topic` | `{oldest, newest, count, more}` | |
| `chat.newer` | `chat`, `after` (a message id), `limit` (1 to 100, default 50); optional `topic` | `{oldest, newest, count, more}` | |
| `chat.pins` | `chat`; optional `topic` | `{ids: [message ids]}`, newest message first | |
| `chat.close` | `chat` | — | |
| `topics.list` | `chat` (a forum: `chats.forum` is 1) | `{count}` | `bad_chat` |
| `chat.read` | `chat`; optional `upto` (a message id), `topic` | — | |
| `chat.typing` | `chat`, `on` (bool); optional `topic` | — | |
| `chat.set` | `chat`; optional `pin` (bool), `mute` (unix seconds until; 0 off; -1 always), `archive` (bool), `unread` (bool); at least one | — | |
| `msg.send` | `chat`, `text`, `quote` (a message id, optional), `mentions` ([{`jid`, `name`}], optional), `topic` (optional) | `{id}` | |
| `media.send` | `chat`, `path` (absolute path of a file), `quote` (a message id), `caption`, `width`, `height` (whole numbers), `thumb` (ignored), `topic`, `file` (true: a document whatever the extension, e.g. a JPEG as it is); all but `chat` and `path` optional | `{id}` | |
| `msg.delete` | `chat`, `ids` [message ids], `everyone` (bool) | — | |
| `msg.edit` | `chat`, `id`, `text` (non-empty: the new text, or the new caption of a photo or video) | — | `not_found` |
| `msg.forward` | `chat` (the message's chat), `id` (the message's id), `to` (the chat it goes to); optional `topic` (a topic of `to`) | `{id}`, the new message's id | |
| `msg.react` | `chat`, `id`, `emoji` (a non-empty string); optional `remove` (bool, default false) | — | |
| `poll.vote` | `chat`, `id` (a poll's message id), `options` (an array of option numbers, 0-based as `extra.options`; missing, null or `[]` takes our vote back) | — | |
| `media.get` | `chat`, `id` (a message with a file); optional `preview` (bool) | `{path}`, the absolute path of the file (of the preview picture with `preview`) | `not_found`, `download_failed`, `cancelled` |
| `media.cancel` | `chat`, `id` (a message with a file) | — | `not_found` |
| `voice.start` | `chat`, `path` (the `.wav` the recorder is writing, inside the data folder); optional `topic` | — | |
| `voice.send` | `chat`, `path` (as `voice.start`), `quote` (a message id, optional), `topic` (optional) | `{id}`, the message's id | |
| `voice.cancel` | `path` (as `voice.start`) | — | |
| `voice.play` | `chat`, `id` (a voice note's message id) | `{path}`, the absolute path of a WAV (48 kHz, mono) | `not_found`, `download_failed` |
| `profile.get` | — | `{name, about, phone, picture}` | |
| `profile.set` | `name`, `about`, `picture` (strings); each optional, at least one | — | |
| `sessions.list` | — | `{sessions: [{id, current, app, version, official, device, platform, system, ip, location, active, login, unconfirmed, passwordPending}]}` | |
| `sessions.terminate` | `id` (a session's id) | — | |
| `sessions.terminateOthers` | — | — | |
| `sessions.confirmQr` | `link` (the `tg://login?token=` link of a QR code) | the new session, as a `sessions.list` entry | |
| `contact.info` | `chat` (a person: a private or a secret chat) | `{phone, about, groups: [{jid, name}], blocked, picture}` | `bad_chat` |
| `channel.info` | `chat` (a channel) | `{followers, description, picture}` | `bad_chat` |
| `group.info` | `chat` (a basic group, or a supergroup that is not a channel) | `{subject, description, participants: [{jid, name, admin, me, photo}], count, picture, membersAdd}`; `photo` is absent when not downloaded yet | `bad_chat` |
| `contacts.list` | — | `[{jid, name, photo}]`, an array: the account's contacts by name; `photo` is absent when not downloaded yet | |
| `senders.get` | `chat` (a basic group, or a supergroup that is not a channel), `users` [user ids, 1 to 100] | `{"<user id>": {color, tag, photo}, …}`, `photo` is absent when not downloaded yet; a user the engine does not know is left out | `bad_chat` |
| `chat.clear` | `chat` | — | |
| `chat.delete` | `chat` | — | |
| `channel.unfollow` | `chat` (a channel) | — | `bad_chat` |
| `contact.block` | `chat` (a person: a private or a secret chat), `block` (bool) | — | `bad_chat` |
| `group.participants` | `chat` (a basic group, or a supergroup that is not a channel), `jid` (a user id), `action` (`add`, `remove`, `promote` or `demote`) | — | `bad_chat`, `refused` |
| `group.create` | `name`, `participants` [user ids] | `{jid, refused}`, the new group's chat key and how many of the people could not be added | `name_too_long` |
| `poll.create` | `chat`, `question`, `options` [2 to 12 strings], `multiple` (bool); optional `topic` | `{id}`, the new message's id | `bad_chat` |
| `invite.info` | `url` (a `t.me/+…` or `t.me/joinchat/…` link) | `{member: true, jid}`, or `{member: false, kind, code, jid, name, count}` | `invite_invalid` |
| `invite.accept` | `code` (the link as `invite.info` gave it); `kind`, `jid` and `name` of that answer may come along and are not read | `{jid}`, the chat's key | `invite_invalid`, `invite_not_allowed`, `request_sent` |
| `chat.export` | `chat` | `{path}`, the absolute path of the text file | `bad_chat`, `write_failed` |
| `privacy.get` | — | `{rules: {phone, phoneFind, status, photo, forwards, calls, birthday, gifts, bio, music, invites}, exceptions: {"<key>": true}, password: {set, hint, recovery, pendingEmail, resetDate}, autoDelete, blocked, ttlDays, topChats, imported}`; a field whose call failed is absent | |
| `privacy.set` | `key` (a key of `rules`), `value` (`everybody`, `contacts` or `nobody`; `phoneFind` has no `nobody`) | — | |
| `password.set` | `old` (the current password, "" when none is set), `new` (not empty); optional `hint`, `email` ("" none) | the password object | `bad_password`, `bad_email`, `too_fresh` |
| `password.email` | `password`, `email` (not empty) | the password object | `bad_password`, `bad_email`, `too_fresh` |
| `password.code` | `code` (the e-mail confirmation code) | the password object | `bad_code` |
| `password.resend` | — | the password object | |
| `password.off` | `old` (the current password) | the password object | `bad_password`, `too_fresh` |
| `autodelete.set` | `secs` (0, 86400, 604800 or 2678400) | — | |
| `blocked.list` | optional `offset` (default 0) | `{total, people: [{jid, name, kind, photo}]}`, up to 50 a call; `photo` is absent when not downloaded yet | |
| `blocked.set` | `jid` (an id), `block` (bool); optional `kind` (`user`, the default, or `chat`) | — | |
| `account.ttl` | `days` (30, 90, 180, 365, 548 or 730) | — | |
| `websites.list` | — | `{websites: [{id, domain, bot, browser, platform, ip, location, login, active}]}` | |
| `websites.disconnect` | `id` (a website's id), or `all: true` | — | |
| `payments.clear` | — | — | |
| `contacts.clearImported` | — | — | |
| `contacts.book` | `entries` [{`number`, `name`}] | `{imported}`, how many of the numbers are Telegram users | |
| `topchats.set` | `on` (bool) | — | |
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
The engine opens the chat in TDLib and makes the newest `limit` messages (default 50) in `chats.db` exactly
TDLib's (rows added, changed and removed, each with `msgs` events). The UI asks for more when the
chat has more unread messages than 50, so the verified page reaches the first unread (a `limit` out
of range is `bad_request`). The reply comes after that
commit: `oldest` is the oldest id of that verified page ("" for an empty chat), `newest` its
newest id ("" for an empty chat; the page is exactly Telegram's from `oldest` to `newest`, which
lets the UI tell whether a later page reaches the rows it verified before), `count` how many
it holds, `more` whether older messages exist. Rows older than `oldest` may be stale or have
gaps: the UI shows them only after a `chat.history` covered them.

**`chat.history`:** the same (`newest` included) for the page of `limit` messages older than
`before`. Served from TDLib's local database when it has them, from Telegram otherwise.

**`chat.newer`:** the page of up to `limit` messages newer than `after` (exclusive; `chats.db`
need not hold it), verified as `chat.history` verifies older ones: the rows from the page's
`oldest` up to and including its `newest` are made exactly TDLib's (each change with `msgs`
events), and the reply comes after that commit. `more` is whether newer messages exist beyond
`newest`, that is, `newest` is below the chat's last message as the engine knows it; rows between
`after` and `oldest` are not touched. An empty page, `{"", "", 0, false}`, means `after` is the
chat's last message or newer, or TDLib has nothing newer to give. The UI uses it for a window
that starts at the first unread message (`after` the last read one, `chat.history` for the
older side), paging forward from there until `more` is false. The engine never anchors it on
the chat's last message: TDLib's history with a negative offset (how it reaches newer messages)
aborts the whole process when its `from` is the chat's last message or beyond and that message is
not loaded in memory (a `closeChat` unloads it). So TDLib is asked only while `after` (then the
newest message collected) is below the chat's last message id, and `after` at or past it is
answered at once with the empty page. The chat's newest page is `chat.open`'s.

With `topic` the same holds for the topic's messages, and the bound is the topic's newest message instead of the chat's: the
engine asks Telegram for the topic first (`getForumTopic`, which also writes its `topics` row), so `more` is whether `newest` is
below the topic's newest message at that moment. A topic's history (`getForumTopicHistory`) has no abort to avoid: TDLib
answers it from Telegram only, never from its loaded messages (no `CHECK(!have_a_gap)` is on that path). A topic's page of
newer messages is cut one message shorter by TDLib than a chat's, which the engine allows for.

**`chat.pins`:** the chat's pinned messages (with `topic`, the topic's), as Telegram has them now: a pin never expires and
an unpin leaves no message, so the list is asked for every time (`searchChatMessages` with the pinned filter, 100 a call,
following `next_from_message_id` until a call brings nothing, at most 5 calls). Every message's row is written first, so the
UI can read its preview and jump to it. The reply `{ids}`, the message ids newest message first (id descending; a pin's own
date is not kept), comes after that commit; no pins is an empty list. TDLib's errors are the `td` error.

**`chat.read`:** marks the chat read up to its newest message, or with `upto` up to that message
(an id above the chat's newest counts as the newest; anything but a message id is `bad_request`).
`unread` changes with the `chats` event that follows.

**`chat.set`:** `pin`, `archive`, `unread` and `mute` as HyperBerry's. The row changes when
Telegram's echo arrives (a `chats` event), not before.

**`msg.send`:** a text. `mentions` are the people the text's "@name"s point at: `jid` the
user id, `name` what the text reads after the "@". Each "@name" found in the text becomes a
mention of that user; one that is not in the text, or whose `jid` is not a user id, is skipped. The row is in `chats.db` as *sending* (state 0) under a temporary id
before the reply comes, and `{id}` is that id. When Telegram takes the message the row is
replaced by one with the final id; a `msgs` event names both ids. On failure the row stays,
state 4.

**`media.send`:** a file as a new message in the chat, sent as `msg.send` sends a text: the same `quote`,
the same row (*sending*, state 0, under a temporary id) and the reply `{id}` with that id. `path` is the absolute
path of an existing regular file of at most 2000 MB (2000 x 2^20 bytes). `caption` is a string, `width` and `height`
whole numbers from 0 (a photo's or video's size in pixels; 0 when not given). A `path` that is missing or is not
such a file, or a field of the wrong kind, is `bad_request` and nothing is sent. The extension, in any case, decides
the kind: `.jpg` and `.jpeg` a photo; `.mp4`, `.3gp` and `.mov` a video (streamable, its duration 0: the request has
none); `.mp3`, `.m4a`, `.aac`, `.amr`, `.ogg` and `.wav` an audio file; anything else a document, PNG, GIF and PDF
included. Only a photo or a video carries `caption`: for the other kinds it is not used, and the UI sends the text
as a `msg.send` of its own. `thumb`, a base64 JPEG that HyperBerry needed, is accepted and ignored: TDLib makes its
own thumbnails. What TDLib refuses (a photo whose `width` and `height` add up to more than 10000, say) is the `td`
error.

**`msg.delete`:** `everyone: true` deletes for all sides where Telegram allows it, `false` only
here. The rows go when Telegram confirms (a `msgs` event names them: they no longer read back).

**`msg.edit`:** the row's `kind` decides: a text is edited as text, a photo or video gets a new
caption; any other kind is `bad_request`, a message `chats.db` doesn't hold `not_found`. The row
changes when Telegram's echo arrives.

**`msg.forward`:** TDLib forwards the one message (`forwardMessages`, as a forward, not a copy: Telegram names the
original sender). A missing or non-numeric `to` is `bad_request`. What TDLib refuses (a message that can't be forwarded,
a chat we can't write to) comes back as its `td` error. Our row in the target chat is `msg.send`'s: queued as *sending*
under a temporary id, and `{id}` is that id.

**`msg.react`:** adds our reaction of `emoji` to the message (`addMessageReaction`), or with `remove: true` takes our reaction of
that emoji back (`removeMessageReaction`). When the account may have one reaction only, TDLib replaces our old one. Anyone may
react in a channel. Custom emoji and the paid reaction are not supported. What TDLib refuses (a reaction the chat doesn't allow, a
message that can't be reacted to) comes back as its `td` error. The row changes with Telegram's update of the message's
interactions (`extra.reactions`, a `msgs` event); the reply only says TDLib took the request.

**`poll.vote`:** our answer to the poll (`setPollAnswer`): the options we choose, all of them, so a new vote replaces the old and
an empty or missing `options` takes it back. An option number that is not a whole number of 0 to 1000 is `bad_request`. TDLib
refuses what the poll doesn't allow (several answers to a single-choice poll, a vote when revoting is off, a closed poll) as its
`td` error. The poll's row changes with Telegram's update (`extra.votes`, `mine`, `voted`).

**`media.get`:** downloads the message's file (a photo's largest size) into TDLib's files folder
and answers when it is complete, after the row has `media_state` 2 and `path` (a `msgs` event).
A file that is already here answers at once. Nothing downloads by itself: the UI asks for what it
shows. `not_found` is a message TDLib doesn't have or one without a file.

With `preview: true` it downloads, in the same way, the picture a bubble shows before the file is opened, not the
file: for a photo its display size (the smallest size whose shorter side is at least 400 px, the largest when none
reaches that: it can be the photo's own file), for a video, GIF or round video its thumbnail when that is a JPEG.
Anything else, or a video with no JPEG thumbnail, is `not_found`. The row then has `extra.preview` (a `msgs` event
follows) and the answer is `{path}` of that picture. Without `preview` nothing changes.

While a `media.get` without `preview` downloads, the engine sends `media.progress` events (see Events). A preview is
never watched.

**`media.cancel`:** stops the download of the message's file (`cancelDownloadFile`) and answers when TDLib has. The
`media.get` still waiting on that file answers the error `cancelled` instead of `download_failed`. It is not an error
when nothing is downloading.

**`voice.start`, `voice.send`, `voice.cancel`:**
- `path` is the WAV the recorder is writing (`AudioRecorder`: 48 kHz stereo 16-bit). The sizes in its header are not
  trusted (they are 0 or 0xFFFFFFFF until the recorder finishes): the data runs to the end of the file. It must be a
  `.wav` inside the engine's data folder, with no `..` in it, or the reply is `bad_request`.
- `voice.start` remembers the chat and the path as the recording and sends Telegram's "recording a voice message"
  action to the chat, and again every 4 s (Telegram shows it for about 5 s) until `voice.send` or `voice.cancel`; a
  recording neither ends (the UI died) stops after 30 minutes.
  One recording runs at a time: a new `voice.start` cancels the old one, as `voice.cancel` would. The reply comes when
  TDLib has taken the action.
- `voice.send` reads the finished WAV (16-bit PCM; stereo is mixed down to mono; a rate other than 8, 12, 16, 24 or 48
  kHz is `bad_request`), encodes all of it then to `data/voice/<ms since the epoch>.ogg` (OGG Opus: voice, 20 kbps,
  20 ms frames), deletes the WAV and sends the file as a voice note. The encoding runs on the engine's own thread, which
  answers nothing else meanwhile: about a second per minute of audio on the phone. A recording of under 0.2 s is
  `bad_request`, and its WAV is deleted. The note has `duration` in whole seconds (rounded, at least 1) and TDLib's
  waveform (100 samples of 5 bits, the loudest one 31). Then it is `media.send`: the same `quote`, the same row
  (*sending*, state 0, under a temporary id) and the reply `{id}` with that id. The recording is over: our action is
  cancelled in the chat.
- `voice.cancel` deletes the WAV and, when it is the recording, cancels our action in its chat. It is not an error
  when nothing is recording. A recording the UI never cancels or sends (it died) leaves its WAV in the data folder.

**`voice.play`:**
- Gives the WAV to play, decoded from the note's OGG Opus into `data/cache/voice/<the file's remote unique id, else
  the local file's name>.wav` the first time and reused after. The cache is never pruned yet.
- If the file isn't here it is downloaded first, as `media.get` does, and the reply waits for it (`download_failed`
  when it does not come). A retry is another `voice.play`.
- Playing one of *their* notes asks TDLib to open the message's content: the first time `played` becomes 1 and a
  `msgs` event follows. Ours are never marked by it.
- `not_found`: no such message, it isn't a voice note, or its file can't be decoded (the `message` says why and the
  engine logs it).

**`profile.get`:** our own account, as Telegram has it: `name` is the first name, then a space and the last name
when there is one; `about` the bio; `phone` the number with a "+" in front ("" when Telegram gives none); `picture`
the absolute path of the big profile photo, downloaded first when it is not here ("" when there is no photo, or when
the download fails: that is only logged). `unlinked` before the login, as for `profile.set`.

**`profile.set`:** changes what is given. `name` is trimmed of spaces and holds 1 to 64 characters; the first name
is what comes before its first space, the last name the rest, trimmed (none when there is no space). `about` holds
0 to 70 characters (code points, not bytes; "" clears the bio). `picture` is the absolute path of an image, a regular
file, which becomes the profile photo; "" deletes the current photo (nothing happens when there is none). Everything
is checked first: a field that is not a string, none of the three, or a value outside those rules is `bad_request`
and nothing changes. The changes then go to Telegram one after another, in the order name, about, picture; the first
TDLib error is the reply (`td`) and the ones after it are not sent, while those before it stay done. The reply
comes after the last.

**`sessions.*`:** the devices our account is signed in on (`getActiveSessions`), all `unlinked` before the login.
`sessions.list` gives the current session (this device) first, then the others by `active`, the newest first. A
session has `id` (a string), `current`, `app` and `version` (the application's name and version), `official` (an
official application), `device`, `platform` and `system` (the device model, its OS and the OS version), `ip` and
`location` (where it was created, as TDLib words them; either may be ""), `active` and `login` (unix seconds of the
last use and of the login), `unconfirmed` (a login nobody confirmed from another session) and `passwordPending` (it
still needs the 2-step password). `sessions.terminate` ends the session `id`, `sessions.terminateOthers` every one but
this; neither changes the list the UI holds, which asks again. `sessions.confirmQr` signs a device in: `link` is what
the QR code of Telegram Desktop (or another app's login screen) holds, and anything that does not start with
`tg://login?token=` is `bad_request`. The reply is the session just created; TDLib's refusal (an expired or used code)
is the `td` reply.

**Privacy and security.** Settings → Privacy and security, as Telegram Android's page. All of these are `unlinked` before the login, and a
TDLib refusal that is not listed below is the `td` error.
- **`privacy.get`:** everything the page shows, in one reply. The engine sends the TDLib calls together and replies when every one has
  answered; one that fails (it is logged) leaves its field out and the reply goes on without it. `rules` has a value for each key:
  `phone` (who sees our number, `ShowPhoneNumber`), `phoneFind` (who finds us by it, `AllowFindingByPhoneNumber`: only `everybody` or
  `contacts`), `status` (last seen, `ShowStatus`), `photo` (`ShowProfilePhoto`), `forwards` (`ShowLinkInForwardedMessages`), `calls`
  (`AllowCalls`), `birthday` (`ShowBirthdate`), `gifts` (`AutosaveGifts`), `bio` (`ShowBio`), `music` (`ShowProfileAudio`) and `invites`
  (`AllowChatInvites`). A value is `everybody`, `contacts` or `nobody`: the base of the setting's rules. The first matching rule decides
  in Telegram's list and no match means "not allowed" (`td_api.tl`, `userPrivacySettingRules`), so the base is the first of
  `AllowAll` (everybody), `AllowContacts` (contacts) and `RestrictAll` (nobody) in the list, and `nobody` when the list has none: TDLib
  drops a trailing `RestrictAll` when it reads from Telegram, so "nobody" is often an empty list or a list of exceptions alone. A
  `RestrictContacts` rule is not looked at (Telegram's apps never write one). `exceptions` has `"<key>": true` for each key whose rules
  hold more than the base: a list of users or chats that is not empty, or a bots or a Premium users rule (TDLib turns a list it
  cannot keep, like a close friends rule, into an empty list; that is not an exception). The engine does not list who is in them.
  `password` is the two-step verification (below). `autoDelete` is the default auto-delete time of
  new chats in seconds, 0 off (`getDefaultMessageAutoDeleteTime`); `blocked` the number of blocked people (`total_count` of
  `getBlockedMessageSenders`); `ttlDays` the account's time to live in days (`getAccountTtl`); `topChats` whether frequent contacts are
  suggested (`!getOption("disable_top_chats")`: the option is empty until Telegram says it is off); `imported` how many contacts the
  address book upload holds (`getImportedContactCount`).
- **`privacy.set`:** changes the base of one setting and nothing else. The engine reads the setting's rules (`getUserPrivacySettingRules`),
  keeps every exception rule in its order (`AllowUsers`, `AllowChatMembers`, `RestrictUsers`, `RestrictChatMembers`, `AllowBots`,
  `RestrictBots`, `AllowPremiumUsers`; a list that is empty is dropped), drops the base rules (`AllowAll`, `AllowContacts`, `RestrictAll`,
  `RestrictContacts`) and puts the new base after them: `everybody` is `AllowAll`, `contacts` is `AllowContacts` then `RestrictAll`,
  `nobody` is `RestrictAll`. The exceptions come first, where the first match decides, so a person on a list is answered by it and everyone
  else by the base (TDLib leaves a trailing `RestrictAll` off the wire, `UserPrivacySettingRules::get_input_privacy_rules`). Then it
  calls `setUserPrivacySettingRules`. An unknown `key`, a `value` that is none of the three, or `phoneFind` with `nobody` is `bad_request`.
- **The password object** (the result of every `password.*`, and `privacy.get`'s `password`): `set` whether a password is set, `hint` its
  hint, `recovery` whether a recovery e-mail is set, `pendingEmail` the masked address Telegram sent a confirmation code to
  (its `email_address_pattern`), "" when no code is waiting, `resetDate` unix seconds after which the password can be reset at
  once, 0 when no reset is pending. They are `getPasswordState`'s `has_password`, `password_hint`, `has_recovery_email_address`, `recovery_email_address_code_info`'s
  `email_address_pattern` and `pending_reset_date`.
- **`password.set`:** `setPassword` with `old` (the current password, "" when none is set), `new`, `hint` and the recovery e-mail `email`
  ("" or missing: none). With an `email` Telegram does not apply the change until the e-mail is confirmed: TDLib takes the server's
  `EMAIL_UNCONFIRMED` for an answer, not an error, so the reply is the state with `pendingEmail` set (the change is applied only after the
  e-mail is confirmed, so `set` can still be false); `password.code` finishes it. **`password.email`** adds or changes the
  recovery e-mail of a password that is set (`setRecoveryEmailAddress`, with the password); the new address waits for its code the same
  way. **`password.code`** (`checkRecoveryEmailAddressCode`) confirms it. TDLib takes `CODE_INVALID` and `EMAIL_HASH_EXPIRED` for answers there and gives the
  state back, so a code that did not work is the state still holding a `pendingEmail`: the engine answers that as `bad_code`.
  **`password.resend`** sends the code again. **`password.off`** (`setPassword` with the current password `old` and an empty new one)
  turns the verification off. Forgetting the password and resetting it are not here (other Telegram apps do it).
- **Errors of `password.*`:** `bad_password` is `PASSWORD_HASH_INVALID` (the old password is wrong); `bad_email` is `EMAIL_INVALID`;
  `bad_code` is `CODE_INVALID`, `EMAIL_VERIFY_EXPIRED` or `EMAIL_HASH_EXPIRED`, and the unconfirmed case above; `too_fresh` is
  `PASSWORD_TOO_FRESH_<n>` or `SESSION_TOO_FRESH_<n>` (Telegram wants the password or this session older before it lets the change
  through) and its `message` is the seconds `n` when the text holds them, else the text. Their `message` is otherwise
  `<code> <TDLib's message>`, as `td`'s; anything else is `td`.
- **`autodelete.set`:** the default auto-delete time of new chats (`setDefaultMessageAutoDeleteTime`): `secs` is 0 (off), 86400 (a day),
  604800 (a week) or 2678400 (a month), else `bad_request`. **`account.ttl`:** the days of inactivity after which the account is deleted
  (`setAccountTtl`): `days` is 30, 90, 180, 365, 548 or 730, else `bad_request`. **`topchats.set`:** `on` false sets `disable_top_chats`,
  true clears it (`setOption`); TDLib tells Telegram.
- **`blocked.list`:** the people on the main block list (`getBlockedMessageSenders`), 50 a call from `offset`; `total` is how many there are
  in all. `jid` is the user's or the chat's id, `kind` `user` or `chat` (Telegram blocks users and supergroups), `name` the user's full
  name or the chat's title ("" when the engine does not know it: the chat is not in its list). `photo` is the user's small profile photo,
  absent when not downloaded yet or for a chat; for a user the engine then asks TDLib for it in the background, after the reply, and
  each one that comes arrives as a `photo` event. **`blocked.set`:** `setMessageSenderBlockList` with the main block list for `block:
  true` and none for false, by sender: `kind` `user` (the default; `jid` a user id) or `chat` (`jid` a chat id). `contact.block` blocks
  by chat; this one works for a person we have no chat with.
- **`websites.list`:** the websites we signed in to with Telegram (`getConnectedWebsites`). `id` (a string), `domain`, `bot` (the bot's
  name, "" when unknown), `browser`, `platform`, `ip`, `location` (as TDLib words them), `login` and `active` (unix seconds).
  **`websites.disconnect`** takes one `id` (`disconnectWebsite`) or `all: true` (`disconnectAllWebsites`); the UI asks again for the list.
- **`payments.clear`:** `deleteSavedOrderInfo` and `deleteSavedCredentials`, both sent at once; ok when both answered ok, else the first
  error (shipping info before cards).
- **`contacts.clearImported`:** `clearImportedContacts`: Telegram forgets the address book we uploaded; the contacts stay.
  **`contacts.book`:** the phone's address book to Telegram (`changeImportedContacts`, which also drops the contacts that are gone from the
  book). `entries` is a list of `{number, name}`, both strings: the digits of `number` are sent (international format, anything else
  ignored, as `link.phone`) and `name` is split at its first space into first and last name, as `profile.set` does. A `number` with no
  digit, an empty `name` or an entry that is not an object is `bad_request`, and nothing is sent. An empty list is valid. The reply `{imported}` counts the numbers that belong to a Telegram user. **The UI
  sends it only when the user turned "Sync contacts" on:** uploading the address book to Telegram is a setting, never a default. TDLib allows one change at a time (its `td` error
  otherwise).

**`contact.info`:** a person's page. `chat` is a private or a secret chat, any other is `bad_chat`. `phone` is the
number with a "+" in front, "" when the user hides it or Telegram has none; `about` the bio; `blocked` whether the
user is on our block list; `groups` the groups we both are in (at most 100, in TDLib's order), each with `jid` (the
chat id) and `name` (its title); `[]` for our own chat, Saved Messages, since TDLib has no groups in common with
ourselves; `picture` the absolute path of the user's big profile photo, downloaded first when it is not here ("" when
there is none, or when the download fails: that is only logged). A TDLib error is the `td` reply.

**`channel.info`:** a channel's page. `chat` is a channel (a supergroup with `is_channel`), any other is `bad_chat`.
`followers` is its member count (a number), `description` its description, `picture` the big chat photo, as
`contact.info`'s.

**`group.info`:** a group's page. `chat` is a basic group or a supergroup that is not a channel, any other is
`bad_chat`. `subject` is the title, `description` the group's description, `picture` the big chat photo as above, and
`membersAdd` whether the group's own permissions let every member invite people (an admin always may). `participants`
are the members that are users (a member that is a chat is left out), at most 200, in TDLib's order: the basic
group's own, or for a supergroup the recent ones, or only its administrators when Telegram does not let us list its
members. Each has `jid` (the user id), `name` (the full name as the engine knows it; "" when it does not), `admin`
(the creator or an administrator) and `me` (it is us). `count` is how many members the group has in all, which can be
more than the list holds. A participant's `photo` is the path of their small profile photo, and is absent when it is
not downloaded yet: the engine then asks TDLib for it in the background (for at most 50 members a request), after
the reply, and each one that comes arrives as a `photo` event.

**`contacts.list`:** the account's Telegram contacts: the result is a JSON array, not an object. `jid` is the user id,
`name` the full name, and they are sorted by `name` ignoring case (only for the ASCII letters: a name in another
script sorts by its UTF-8 bytes). `photo` is as a `group.info` participant's, for at most 100 contacts a request.

**`senders.get`:** what a group chat's cards show of the people who wrote in it. `chat` is a basic group or a supergroup
that is not a channel, any other is `bad_chat`; `users` holds 1 to 100 user ids (strings; duplicates count once), anything
else is `bad_request`. The result is an object keyed by user id, with one entry for each user the engine knows (it has had
an `updateUser` for them: TDLib sends one before it hands over a message of theirs, so the senders of a page `chat.open`
or `chat.history` made are known, but after an engine restart the sender of an older cached row may not be yet) and none
for the others, which can be asked again later. `color` is the
user's Telegram accent colour as one of the seven built-in ones: 0 red, 1 orange, 2 violet, 3 green, 4 cyan, 5 blue, 6
pink. A user whose `accent_color_id` is above 6 has a server colour, which maps to its `built_in_accent_color_id` from
TDLib's `updateAccentColors`; one that list does not name gets the id modulo 7. `tag` is what Telegram shows after the
name of an administrator: their custom title when they have one, else `owner` for the creator, else `admin`; "" for
everyone else, and for everyone when TDLib refuses `getChatAdministrators` (logged; the reply still goes). `photo` is the
path of the user's small profile photo, absent when it is not downloaded yet: the engine then asks TDLib for it in the
background (for at most 50 users a request), after the reply, and each one that comes arrives as a `photo` event.

**`chat.clear`:** deletes the chat's messages for us alone (TDLib's `deleteChatHistory`, the chat staying in the
list); an active secret chat is the one exception, TDLib deletes its history only for both sides. The rows go with
TDLib's delete updates, as `msg.delete`'s do (`msgs` events). The chat's `last_preview` stays as it was until its
next message: the engine keeps a chat's preview when TDLib says it has no newest message. A TDLib error is `td`.

**`chat.delete`:** for a private or a secret chat, deletes its history for us and removes the chat from the list (a
`chats` event names it). For a basic group, a supergroup or a channel it leaves the chat (`leaveChat`: deleting a group
in Telegram means leaving it, the others keep it), and then for a basic group also deletes its history and removes it
from the list; TDLib allows that call for no supergroup or channel we have left, so whether such a chat goes from the
list is TDLib's. The reply comes when the last call is done.

**`channel.unfollow`:** leaves a channel (`leaveChat`). Any other chat is `bad_chat`.

**`contact.block`:** puts the person on our block list (`setMessageSenderBlockList` with the main block list) with `block: true`, takes
them off with `false`. `chat` is a private or a secret chat, any other is `bad_chat`. `contact.info`'s `blocked` shows the result.
A TDLib error is `td`.

**`group.participants`:** one change to a group's members. `chat` is a basic group or a supergroup that is not a channel (any
other is `bad_chat`), `jid` a user id, `action` one of:
- `add`: `addChatMember` with the last 100 messages forwarded to the new member (TDLib ignores that in a supergroup). A person
  Telegram could not add (their privacy settings) comes back as a list of who failed, not as an error; a list that is not empty is
  `refused` with the message `403`.
- `remove`: `banChatMember` with no end date. In a supergroup the person cannot come back by a link until an admin unbans them;
  in a basic group they are only removed.
- `promote`: `setChatMemberStatus` to administrator with the rights Telegram's apps give a new one: change group info, delete
  messages, ban users, invite users, pin messages, manage video chats and manage topics; no right to add administrators, and not
  anonymous. (In a basic group every administrator has every right; TDLib ignores these.)
- `demote`: `setChatMemberStatus` to an ordinary member.

An unknown `action` is `bad_request`. TDLib's `USER_PRIVACY_RESTRICTED` (403, it also comes as an error from some paths) is
`refused` with the message `403`, and `USER_ALREADY_PARTICIPANT` is `refused` with `409`: the group info page words both. Any other
refusal (we are not an admin, the person is not in the group) is the `td` error. The group's member list changes when the page
asks `group.info` again.

**`group.create`:** a new basic group (`createNewBasicGroupChat`) named `name`, with the people in `participants` (user ids, strings)
in it. `name` is trimmed and holds 1 to 128 characters (code points): an empty one is `bad_request`, a longer one is
`name_too_long`, checked here because TDLib cuts a longer name silently. The reply `{jid, refused}` comes after the flush that
writes the chat to `chats.db`: `jid` is the chat's key, `refused` how many of the people Telegram would not add (their privacy
settings), the group having been made without them.

**`poll.create`:** a poll as a new message of the chat (`sendMessage` with `inputMessagePoll`), sent as `msg.send` sends a text: the
row is *sending* under a temporary id and `{id}` is that id, and `topic` sends it into that forum topic. `question` is not empty and
`options` holds 2 to 12 strings that are not empty (Telegram's `poll_answer_count_max`); anything else is `bad_request`. `multiple`
lets a voter pick several options. The poll is a regular one (not a quiz) that allows changing a vote. It is anonymous in a group or
a channel (a channel takes no other kind) and not anonymous in a chat with a person. TDLib takes a poll in a chat with a person only
for a bot and for Saved Messages: any other is its `td` error ("Polls can't be sent to the private chat"), as are Telegram's other
limits (a question over 255 characters, an option over 100, a group where members may not send polls).

**`invite.info`:** looks at an invite link (`checkChatInviteLink`) without using it. When the link is for a chat we can already
read, the result is `{member: true, jid}`, `jid` its key. Otherwise `{member: false, kind, code, jid, name, count}`: `kind` is
`group`, or `channel` for a channel; `code` is `url` as it came, to give to `invite.accept`; `jid` is "" for a chat TDLib has no
access to yet, or the chat's key for one it only lets us look at for a while (an invite that previews a channel); `name` is the
chat's title and `count` its member count. A link Telegram has no more or never had (`INVITE_HASH_EXPIRED`, `INVITE_HASH_INVALID`,
`INVITE_HASH_EMPTY`, or TDLib's own "Wrong invite link" for a text it can't read a link from) is `invite_invalid`; an empty or missing
`url` is `bad_request`; any other TDLib error is `td`. The reply comes after the flush that writes the chat, when there is one.

**`invite.accept`:** joins the chat of an invite link (`joinChatByInviteLink`). `code` is `invite.info`'s; the other three fields are
not read. When we are in, the reply `{jid}` comes after the flush that writes the chat to `chats.db`. Otherwise an error:
`request_sent` when the link only asks to join and the request went (an admin of the group has to let us in), `invite_not_allowed`
when the join was declined, or needs a guard bot's approval, or Telegram says `USER_BANNED_IN_CHANNEL` or `CHANNEL_PRIVATE`,
`invite_invalid` for a link that is gone (as `invite.info`). `CHANNELS_TOO_MUCH` and the rest are the `td` error.

**`chat.export`:** writes the chat's whole history to a text file, oldest message first, one line for each message:
`dd/mm/yy, HH:MM - Name: text` in the phone's local time. `Name` is `You` for our messages; in a chat with a person the chat's title
for theirs; in a group or a channel the sender's full name (the channel's title for a post). The text is the message's text or
caption; a media with none (photo, video, GIF, voice or video message, audio, file, sticker) is `<Media omitted>`; a service
message is worded as the chat list words it ("Anna joined"), which holds the name, so it has no `Name: `; a poll, a place or a contact
likewise. A text of several lines keeps them. The history is read with `getChatHistory`, 100 a call from the newest message down
until a call brings nothing, so the engine asks Telegram for what it does not hold; it stops at the newest 200 000 messages or 24 MiB of
text, whichever comes first. The file is `Telegram Chat with <the chat's title>.txt` (each of `/\:*?"<>|` becomes `_`), written over an
older one of that name, in `/accounts/1000/shared/downloads`, else in `downloads` in the data folder. The reply `{path}` is its
absolute path and comes when the file is written; a file that cannot be written is `write_failed`. An unknown chat is `bad_chat`, a
TDLib error while reading the history is `td`. A connection that closes meanwhile stops the walk.

**Forum topics.** A supergroup can be a forum, its messages sorted into topics (`chats.forum` is 1). A topic is a number, the
`topic` field of the requests and the `topic` column of the tables. **General is topic 1** (TDLib's `ForumTopicId::general()`;
Telegram puts a message that is in no other topic into it, and so does the engine for one TDLib gives with no topic in a
forum). Any other topic's id is the id of the service message that created it. Every `topic` field below is optional and is
a JSON number, a whole number from 1; anything else is `bad_request`. Without it the request is the chat's, as before.
- **`topics.list`:** `chat` must be a forum, else `bad_chat`. The engine asks Telegram for all the chat's topics
  (`getForumTopics`, 100 a call, going on until the approximate total is reached or a call brings nothing, at most 20 calls) and
  makes the chat's rows of `topics` exactly that list: rows are added or changed, the topics Telegram did not list are deleted,
  each change with a `topics` event. The reply `{count}`, the number of topics listed, comes after that commit. From then on
  the chat's rows are kept current (below).
- **History:** `chat.open`, `chat.history` and `chat.newer` with `topic` page through the topic (`getForumTopicHistory`) instead
  of the chat. The paging rules and the reply are the same, and when a page is made exactly Telegram's only the rows of that
  topic in its id range are compared: the other topics' rows in the same range stay. `chat.open` also opens the chat in TDLib
  (`chat.close` closes it). A topic's pages always come from Telegram: TDLib keeps no local list of a topic's messages.
- **`chat.read`** with `topic` marks the topic read up to its newest message, or up to `upto` (an id above the topic's newest
  counts as the newest): the engine asks Telegram for the topic first (to learn its newest message), then reads it
  (`viewMessages` with the forum topic as source), then fetches the topic again; its `unread` and `read_inbox` change with a
  `topics` event. In a forum, `chat.read` without `topic` is still the chat's.
- **Sending:** `msg.send`, `media.send`, `voice.send` with `topic` send into that topic (`sendMessage` with
  `messageTopicForum`); `msg.forward` with `topic` forwards into that topic of `to`. Without it Telegram places a message in a
  forum itself (in General, or in the topic of the message it replies to). A closed topic, or any rule Telegram has, is the `td` error.
- **Typing:** `chat.typing` and `voice.start` with `topic` show their action in that topic. The `typing` event of a chat
  gets `topic` (below), so the UI can show it on the right page.
- **Keeping the rows current:** a topic's row is fetched again (`getForumTopic`: one Telegram request) when TDLib says the topic
  changed (`updateForumTopicInfo`, `updateForumTopic`: a rename, close, pin, mute, a read on another device, a new topic), when a
  message is added to, edited in or deleted from the topic, and after a `chat.read` with `topic`; a `topics` event names it
  when its row changed. A topic that Telegram says does not exist (`Not Found`: it was deleted) loses its row. This is done
  only for the chats the UI has asked for: `topics.list`, or `chat.open`, `chat.newer` or `chat.read` with `topic`, or one that
  already had rows when the engine started; the messages of any other forum cost no request. One topic is fetched at most
  once at a time: a fetch asked for meanwhile runs after the one in flight. TDLib sends the updates above for every topic
  it answers a request of ours with: those are ignored while our own request for the topic, or for the chat's list, is
  out, so a change that comes only as such an update in that short time is seen with the next message of the topic.
- **Sorting the list:** `ORDER BY pinned DESC, CAST(ord AS INTEGER) DESC` (`ord` is TDLib's `order` for the topic, a decimal
  int64 as text: more than a QML number holds, so let SQLite sort it, never compare it in QML).

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
| `topics` | `chat`, `topics`: [topic ids, numbers] | these rows of `topics` of that chat changed or disappeared: re-read them |
| `typing` | `chat`, `who` (first name in a group, "" in a 1:1 chat), `on`, `voice` (recording a voice note), `topic` (the forum topic it happens in, 0 when it is not in a topic) | someone started or stopped typing; only for chats between their `chat.open` and `chat.close` |
| `presence` | `chat`, `online`, `lastSeen` (unix seconds, 0 when hidden) | the other person's online status in a 1:1 chat, once after `chat.open` and at every change until `chat.close` |
| `photo` | `jid` (a user id), `photo` (the absolute path of the user's small profile photo) | a photo that `group.info`, `contacts.list` or `senders.get` left out is downloaded: put it on that person's row |
| `media.progress` | `chat`, `id` (the message), `done`, `size` (bytes, numbers; `size` is the expected size when TDLib has no exact one yet) | the file a `media.get` (not a preview) is downloading has grown; at most every 200 ms per file, so a fast download may skip some; none after the file is complete or cancelled |

`chats`, `msgs` and `topics` come after the engine commits, at most every 50 ms. `photo` does not wait for that: it comes when
TDLib has downloaded the photo, which is always after the reply that left it out.

**The link body** (the `hello` result and every `link` event): `state`, `conn` and `memMB`, plus:

| `state` | TDLib's state | also |
|---|---|---|
| `linking` | starting, or a step is being checked | |
| `unlinked` | waiting for a phone number | |
| `qr` | waiting for another device to scan | `qr`: the `tg://login?token=…` link to draw |
| `code` | waiting for the code | `phone`, `codeType` (`app`, `sms`, `call`, `other`), `length` (0 unknown) |
| `password` | waiting for the two-step password | `hint` |
| `ready` | logged in | `me` (our user id), `name` |

`memMB` goes with every state: the engine's `RLIMIT_DATA` in MiB as it was at start, after the engine raised
its soft limit to the hard one (0 for none, or when it could not be read). **30 means the `headless-mem` addon
(`../rlimit-patch`) has not patched authman since boot**: the engine runs under the stock headless cap and
cannot get through a first sync. The UI offers the addon when it sees less than 128.

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
it*), with these meanings for Telegram. `user_version` is 5. A file at version 2, 3 or 4 is migrated in place when the engine starts;
the step from 3 to 4 adds `chats.forum`, `messages.topic` and `topics`, and **empties `messages`**:
the old rows have no topic, and the table is a cache that TDLib refills (the engine writes each chat's newest message again
when it starts, and a page of history when the UI opens a chat). `chats` and its rows stay. The step from 4 to 5 **drops the
`reactions` and `poll_votes` tables**, which were always empty: a message's reactions and a poll's counts are in `messages.extra`
now (below). A row written before that has none until the engine writes it again, which it does for a chat's newest page on
`chat.open` and for any update of the message (a new reaction, a vote, an edit).

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
| `read_inbox` | the id of the newest message read here or on another device; 0 when unknown (a database that was at `user_version` 2 has 0 until the engine next writes the chat's row) |
| `forum` | 1 for a supergroup with topics (see *Forum topics*); 0 for any other chat, and until the engine next writes the row of a chat in a database that was at `user_version` 3. A group can become a forum or stop being one: a `chats` event names the row. Messages already in `messages` keep the `topic` they had (0 for a group that was not a forum): the UI uses `messages.topic` only while `forum` is 1, and a chat that has just become a forum gets its topics' messages from `chat.open` with `topic` |

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
| `topic` | the forum topic the message belongs to: its id (General is 1); 0 in every chat that is not a forum. Index `messages_by_topic` on `(chat, topic, ts)`: a topic's page is `WHERE chat = ? AND topic = ? ORDER BY ts, CAST(id AS INTEGER)` |
| `extra` | JSON, NULL when empty: `gif: true`; a sticker's `animated: true` and `emoji`; a location's `lat`, `lon`; a contact's `contacts: [{name, tel: [number]}]`; a poll's `options`, `max` (1, or 0 for several), `votes`, `mine`, `voted` and `closed`; a message's `reactions`; a text's link preview `title`, `desc`, `url`; a document's `caption`; an audio file's `title`, `performer`; a call's `secs` |

**Reactions:** any kind's `extra` can carry `reactions: [{e, n, me}, …]`, one object for each emoji reaction on the message in
TDLib's order: `e` the emoji, `n` how many reacted with it, `me: true` when one of them is ours (absent otherwise). Custom emoji
and the paid reaction are left out, and a message with none has no `reactions`. `msg.react` changes them.

**Polls:** a poll's `extra` (kind 11) carries, beside `options` (the option names) and `max`: `votes`, the number of voters for
each option in the same order (all 0 until we have voted or the poll is closed, unless we made it: Telegram hides the counts
before); `mine`, the numbers (0-based) of the options we chose; `voted`, how many people voted in all; and `closed: true` when
the poll is closed (absent otherwise). There are no voters' names. `poll.vote` changes them.

Any kind's `extra` can also carry `forwarded: true` (the message is a forward) and, on a photo or
video, `spoiler: true` (Telegram covers it until tapped).

A photo's or video's `extra` can carry `preview`, the absolute path of the picture the bubble shows before the file
is opened, once `media.get` with `preview` has downloaded it (or it is here for another reason): a photo's display
size (see `media.get`), only while the photo itself is not here (`media_state` is not 2); a video's (also a GIF's or
a round video's) thumbnail, only when it is a JPEG. The row is written again when it appears.

Any kind's `extra` can also carry `ent`, the formatting of `text` (or of a document's
`extra.caption`): a list of `[offset, length, type]` or `[offset, length, type, argument]`, in
Telegram's order. Offsets and lengths are in UTF-16 code units, which is what a `QString` counts.
Types: `b` bold, `i` italic, `u` underline, `s` strikethrough, `sp` spoiler, `c` code, `p`
preformatted (argument: the language, when given), `q` block quote, `a` a link (argument: the
URL; absent when the text itself is the URL), `m` a mention (argument: the user id, when the
mention is by name), `h` hashtag, `cmd` bot command, `e` e-mail address, `t` phone number.
Other entity types are left out.

**`topics`**, primary key `(chat, topic)`: the topics of the forums the UI has asked for (`topics.list`, see *Forum topics*), kept
current by the engine. Only chats with `chats.forum` 1 have rows.

| column | meaning |
|---|---|
| `chat`, `topic` | the forum chat's id (text, as `messages.chat`) and the topic's id (a number; General is 1) |
| `name` | the topic's name |
| `color` | the icon's colour as a number `0xRRGGBB` (one of 0x6FB9F0, 0xFFD67E, 0xCB86DB, 0x8EEE98, 0xFF93B2, 0xFB6F5F); 0 when TDLib gives none |
| `general` | 1 for the General topic |
| `pinned` | 1 when pinned in the topic list |
| `closed` | 1 when closed: only an administrator or the topic's creator may write in it |
| `hidden` | 1 when the General topic is hidden (and closed) |
| `ord` | TDLib's order, a decimal int64 as text: the date of the topic's newest message shifted left 32 bits plus that message's number; 0 for a topic with no message. Sort `pinned DESC`, then `CAST(ord AS INTEGER) DESC` |
| `unread` | the unread count of the topic |
| `mentions` | the unread messages with a mention of us or a reply to us |
| `read_inbox` | the id of the newest incoming message read in the topic; 0 when none |
| `last_id`, `last_ts`, `last_preview`, `last_out` | the newest message: its id (text; NULL with `last_preview` when the topic has none), unix seconds (0 when none), the one-line preview as a group's chat list shows it ("Anna: Photo", "You: hi"), ours or not |
| `muted_until` | the topic's own mute, as `chats.muted_until` (unix seconds; 0 not muted; 2^40 forever). A topic that follows the chat's setting has 0, and so does one unmuted on purpose in a muted chat: a topic is muted when this is in the future or, when it is 0, when `chats.muted_until` is |

A notice's `text` is `call`, `video_call`, `missed_call` or `missed_video_call`.
`deleted`, `media` and `starred` are unused (0 or NULL).
