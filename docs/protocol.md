# Tengram: the UI ↔ engine protocol

The contract between the Cascades UI (`tengram`) and the engine (`tengramd`). It is HyperBerry's
(`../hyperberry/docs/protocol.md`) with Telegram's meanings, so the forked UI keeps its code.
Where this file is silent about a command, the engine does not have it yet (`unknown_cmd`).
When the code changes, this file changes with it.

## Files in the data folder

| file | what |
|---|---|
| `db/`, `files/` | TDLib's own database and files. Only the engine touches them. |
| `chats.db` | What the UI shows, written by the engine, read by the UI. SQLCipher 4, WAL. A cache: the engine deletes and rebuilds it when it can't be opened, its `user_version` isn't the engine's (version 2 is migrated in place to 3), or `db.key` is new. |
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
| `chat.open` | `chat`, `limit` (1 to 200, default 50) | `{oldest, newest, count, more}` | |
| `chat.history` | `chat`, `before` (a message id), `limit` (1 to 100, default 50) | `{oldest, newest, count, more}` | |
| `chat.newer` | `chat`, `after` (a message id), `limit` (1 to 100, default 50) | `{oldest, newest, count, more}` | |
| `chat.close` | `chat` | — | |
| `chat.read` | `chat`; optional `upto` (a message id) | — | |
| `chat.typing` | `chat`, `on` (bool) | — | |
| `chat.set` | `chat`; optional `pin` (bool), `mute` (unix seconds until; 0 off; -1 always), `archive` (bool), `unread` (bool); at least one | — | |
| `msg.send` | `chat`, `text`, `quote` (a message id, optional), `mentions` ([{`jid`, `name`}], optional), `topic` (optional) | `{id}` | |
| `media.send` | `chat`, `path` (absolute path of a file), `quote` (a message id), `caption`, `width`, `height` (whole numbers), `thumb` (ignored), `topic`; all but `chat` and `path` optional | `{id}` | |
| `msg.delete` | `chat`, `ids` [message ids], `everyone` (bool) | — | |
| `msg.edit` | `chat`, `id`, `text` (non-empty: the new text, or the new caption of a photo or video) | — | `not_found` |
| `media.get` | `chat`, `id` (a message with a file); optional `preview` (bool) | `{path}`, the absolute path of the file (of the preview picture with `preview`) | `not_found`, `download_failed`, `cancelled` |
| `media.cancel` | `chat`, `id` (a message with a file) | — | `not_found` |
| `voice.start` | `chat`, `path` (the `.wav` the recorder is writing, inside the data folder); optional `topic` | — | |
| `voice.send` | `chat`, `path` (as `voice.start`), `quote` (a message id, optional), `topic` (optional) | `{id}`, the message's id | |
| `voice.cancel` | `path` (as `voice.start`) | — | |
| `voice.play` | `chat`, `id` (a voice note's message id) | `{path}`, the absolute path of a WAV (48 kHz, mono) | `not_found`, `download_failed` |
| `profile.get` | — | `{name, about, phone, picture}` | |
| `profile.set` | `name`, `about`, `picture` (strings); each optional, at least one | — | |
| `contact.info` | `chat` (a person: a private or a secret chat) | `{phone, about, groups: [{jid, name}], blocked, picture}` | `bad_chat` |
| `channel.info` | `chat` (a channel) | `{followers, description, picture}` | `bad_chat` |
| `group.info` | `chat` (a basic group, or a supergroup that is not a channel) | `{subject, description, participants: [{jid, name, admin, me, photo}], count, picture, membersAdd}`; `photo` is absent when not downloaded yet | `bad_chat` |
| `contacts.list` | — | `[{jid, name, photo}]`, an array: the account's contacts by name; `photo` is absent when not downloaded yet | |
| `senders.get` | `chat` (a basic group, or a supergroup that is not a channel), `users` [user ids, 1 to 100] | `{"<user id>": {color, tag, photo}, …}`, `photo` is absent when not downloaded yet; a user the engine does not know is left out | `bad_chat` |
| `chat.clear` | `chat` | — | |
| `chat.delete` | `chat` | — | |
| `channel.unfollow` | `chat` (a channel) | — | `bad_chat` |
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
| `photo` | `jid` (a user id), `photo` (the absolute path of the user's small profile photo) | a photo that `group.info`, `contacts.list` or `senders.get` left out is downloaded: put it on that person's row |
| `media.progress` | `chat`, `id` (the message), `done`, `size` (bytes, numbers; `size` is the expected size when TDLib has no exact one yet) | the file a `media.get` (not a preview) is downloading has grown; at most every 200 ms per file, so a fast download may skip some; none after the file is complete or cancelled |

`chats` and `msgs` come after the engine commits, at most every 50 ms. `photo` does not wait for that: it comes when
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
it*), with these meanings for Telegram. `user_version` is 3.

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

A notice's `text` is `call`, `video_call`, `missed_call` or `missed_video_call`.
`deleted`, `media` and `starred` are unused (0 or NULL). `reactions` and `poll_votes` exist and
are empty for now.
