---
name: signup-testing
description: "New-account sign-up can't be tried on Telegram's test servers any more; only a number never on Telegram tests it (2026-11-21)"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-21
---

Sign-up (link states `register`, `email`, `emailCode`, `premium`; engine/link.cpp, LinkPage.qml) was built on 2026-11-21
and has run on no real new number: logging out and back in with an existing account never reaches it.

`TG_TEST_DC=1 scripts/build-app.sh` builds an engine on Telegram's test servers (TDLib keeps their data apart, in
`db/td_test.binlog` and `db_test.sqlite`, so the real account's files are untouched). It does not help: on 2026-11-21 the
test servers sent a code for made-up `99966XYYYY` numbers but refused every code (`22222`, `222222`, `11111` on a DC 1
number: `PHONE_CODE_INVALID`), as reports since 2025 say (tdlib/td issues 3083, 3564). The `99966…` sign-up of old docs
(Telethon's, Pyrogram's) no longer works.

**How to apply:** to try the sign-up, use the normal build and a number that has never had a Telegram account (a prepaid
SIM, a friend's). Don't offer the test servers for it. Before a test build goes on the phone, the user logs out; after it,
log out again and install a normal build.
