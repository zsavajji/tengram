# tengramd

The engine: the only process that talks to Telegram (through TDLib). It keeps `chats.db` current
for the UI and answers the UI over a loopback socket. The contract is `docs/protocol.md`; the
decisions and measurements are in `NOTES.md`.

| file | what |
|---|---|
| `main.cpp` | arguments, signals, the one `poll()` loop, the 50 ms flush |
| `td.cpp` | the TDLib client: a receive thread that queues, callbacks on the main thread |
| `link.cpp` | login states and `link.*` |
| `chats.cpp`, `body.cpp` | the chat list mirrored into `chats`, previews |
| `messages.cpp`, `requests.cpp` | the `messages` table, `chat.*`, `msg.*`, `media.get` |
| `topics.cpp` | the `topics` table (forum topics): `topics.list`, and the rows kept current |
| `voice.cpp`, `opus_ogg.cpp` | `voice.*`: the recording's requests; the WAV / OGG Opus codec (no TDLib in it) |
| `mode.cpp` | delivery mode (instant, batched, auto), `ui.state`, `prefs.set` |
| `notifier.cpp`, `hub*.cpp` | notifications from TDLib's groups to the Hub (or to the log) |
| `bb10.cpp` | `--service` only: navigator and network events |
| `db.cpp`, `server.cpp` | encrypted `chats.db`, the socket |

## Build and run on the phone

```sh
scripts/build-deps.sh                      # once: TDLib, OpenSSL, libopus (Docker)
engine/build.sh                            # build/tengramd, build/dbq, build/voice_check (the codec's check)
E=$(scripts/push.sh build/tengramd)        # copies and trusts it, prints its path on the phone
ssh bb10rootx "cd /root/tengram && TG_API_ID=… TG_API_HASH=… $E data >engine.log 2>&1 </dev/null &"
```

`data` holds TDLib's session (log in once with the probe: `probe/login-qr.sh`). Stop it with
`kill <pid>` (SIGTERM): it closes TDLib, logs the update counts and removes `engine.json`.

## Talk to it, look into the database

```sh
tests/engine_client.py chat.open '{"chat":"<id>"}' --listen 10    # through an ssh tunnel; prints replies and events
ssh bb10rootx "cd /root/tengram && ./dbq-<sha>-1 data/chats.db data/db.key 'SELECT count(*) FROM chats'"
```

On the real account, try sends, reads, edits and deletes on Saved Messages only (its chat id is
your own user id, `me` in the `hello` reply), and never run `link.logout`: it ends the session.
