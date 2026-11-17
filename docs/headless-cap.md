# The headless memory cap

The engine runs as a headless app, under `RLIMIT_DATA` set by the launcher from the capability class in its
bar descriptor. Ours is `_sys_headless_xlarge`, the largest stock class: **30 MiB**, soft and hard, so the engine
cannot raise it. The first sync after a login (467 chats, then TDLib reloading the installed sticker sets) died
against it with `std::bad_alloc`, again and again (0.1.0.3, 2026-11-17).

**The fix lives in `../rlimit-patch`:** a root-installer addon that rewrites the class's text in the running
authman's memory at every boot (`rlm RLIMIT_DATA 31457280` → `134217728`). With it the engine logged
`RLIMIT_DATA` 128 MiB and got through that sync. Its `research/findings.md` is the whole investigation:
where the classes live (`/etc/authman/sys.acl` on the read-only OS image), authman's protocol, and why
editing the file, restarting authman, loading a class at run time and a boot-time overlay all fail.

What Tengram needs to know:
- **The engine logs its limits** at start (`limits()` in `engine/main.cpp`: `rlimit DATA: soft …, hard …`) and
  the first failed allocation (`out of memory: an allocation failed`, with the limits then). That line says
  whether the phone has the patch.
- **Without the patch the cap is 30 MiB.** Every user is rooted (the user, 2026-11-17), so the addon is part of
  installing Tengram. The engine should still keep its peak low: patch 0006's 8 MB SQLite cache and the
  sticker sets TDLib loads at the first sync are the first two to measure.
- **A reboot undoes the patch** until the addon runs, about 4 minutes in (root comes late); an engine started
  before then keeps 30 MiB until it restarts.
