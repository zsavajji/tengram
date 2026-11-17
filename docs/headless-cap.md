# The headless memory cap: where it is, and how root could raise it

Read from the test phone (a Classic, OS 10.3.3.3216) on 2026-11-17, read-only, with the launcher and
authman binaries disassembled on the Mac. Nothing on the phone was changed.

## What the engine gets

Logged by tengramd at start (`limits()` in `engine/main.cpp`):

| limit | soft | hard |
|---|---|---|
| `RLIMIT_DATA` | 31,457,280 (30 MiB) | 31,457,280 |
| `RLIMIT_AS`, `RLIMIT_STACK` | none | none |
| `RLIMIT_FREEMEM` | 970 (97 % of the phone) | 970 |

Soft equals hard, so the process cannot raise it. The first sync after a login (467 chats, then TDLib
reloading the installed sticker sets) died with `std::bad_alloc` against it, again and again, until one run
got through.

## Where 30 MiB comes from

- `/etc/system/config/launcher.cfg`: `headless_heapsize_limit_mb=3`, a headless app with no memory class.
- **`/etc/authman/sys.acl`**, read by `authman` when it starts:
  ```
  _sys_headless_large
      RLM RLIMIT_DATA 10485760
  _sys_headless_medium
      RLM RLIMIT_DATA 6291456
  _sys_headless_xlarge
      RLM RLIMIT_DATA 31457280
  ```
  `_sys_headless_xlarge` (our bar asks for it) is the largest class. No binary contains the name: it exists
  only in this ACL, which is why HyperBerry's notes could not find its size.
- The launcher asks authman for an app's capabilities at each launch (`defapp`, `getcap`, text lines on
  `/dev/authman`) and sets `RLIMIT_DATA` from their `RLM` entries ("Only RLIMIT_DATA currently supported").
- No capability in `sys.acl` grants the procmgr `rlimit` ability (`PAB` entries), so no stock permission
  lets a process raise its own hard limit.

Apps on this phone that ask for `_sys_headless_xlarge` and would gain from a larger class: BBTelega,
HyperBerry, bbmusic, Tengram, the system's Traveler PIM provider and install handler.

## The files

`/etc` is a procnto link to `/base/etc`, inside `/base`, the signed read-only rcfs OS image
(`/dev/emmc/os0t179`). Changing the OS image is out; a procnto link (`ln -sP`, `pathmgr_symlink`) over a
single path wins over the rcfs by longest-prefix match. That is how the VoLTE addon (`../bb10-volte`,
TECHNICAL.md §4.5) serves its carrier config over `/base/etc/carrier/config/<id>.xml`, and its lesson
applies: root arrives about 4 minutes into the boot, so a link alone changes nothing for a process that
already read the file. The stock process has to read it again.

## authman

- Started once by `/base/scripts/startup.sh`, runlevel 0: `authman -U 505` (`-i` on insecure devices),
  then `WAITFOR /dev/authman`. Nothing restarts it. Options: `-i -c: -a: -b -p: -U: -v`.
- Commands are text on `/dev/authman`, in a table of `{name, permission, bit, handler}` (0x153b4): `defapp
  defpid renapp clrapp uninst rescap path setcap clrcap dyncap chkcap getcap setacc sigreq valpol info
  setsandboxname regprefix load delcap debug watch`. `authman.cfg [client_whitelist]` grants each user
  some of them; `carrier_mgr` has `regprefix,load,delcap`.
- `regprefix <prefix>` (0x8b94) registers a capability name prefix owned by the caller's uid:gid, and
  refuses one that existing rules already use ("A cap rule with prefix '%s' exists").
- `load <prefix> <file>` (0x8c48), separated by space, tab or comma: the prefix's owner must be the
  caller; the file ends in `.acl` or `.res`, or both are loaded; authman reads it with the caller's
  euid/egid. This is how the carrier's `subsystem/<id>.acl` files add their `cds_*` capabilities.

So `load` cannot redefine `_sys_headless_xlarge` (its prefix is taken), only add new capabilities.

## Two root routes, both untested

1. **Patch the class and restart authman.** Copy `sys.acl` to a writable place with a larger
   `_sys_headless_xlarge` value; `ln -sP <copy> /base/etc/authman/sys.acl`; stop authman and start
   `/base/sbin/authman -U 505` again. Every xlarge app gains. Risk: authman's run-time state goes with
   it: the launcher's registrations of running apps (`defapp`/`defpid`), `carrier_mgr`'s `cds` prefix and
   its loaded subsystem ACLs, dynamic capabilities. Apps launched before the restart may fail later checks
   (`chkcap` from PIM, media, BBID) until relaunched; carrier apps until `carrier_mgr` loads again. Whether
   a restart is survivable at all is the first thing to try.
2. **Add a class through `load`, no restart.** As a user the whitelist allows (`carrier_mgr`), `regprefix
   tgm` and `load tgm <dir>/tgm` with `tgm.acl` = `tgm_headless_huge` + `RLM RLIMIT_DATA <bytes>` and
   `tgm.res` allowing the apps. Each app then asks for `tgm_headless_huge` in its bar. Unknowns: whether
   a loaded ACL may carry `RLM`, whether the installer keeps an app permission it doesn't know, whether the
   launcher applies `RLM` from a loaded capability. Redone at every boot, after root.

Both change the phone's OS state (a reboot undoes them) and wait for the user's go-ahead.
