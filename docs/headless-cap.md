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

## Route 1, tried 2026-11-17: ruled out

A patched copy (`xlarge` = 128 MiB) at `/var/etc/authman/sys.acl`, `ln -sP` over
`/base/etc/authman/sys.acl` (read back through both paths, checksum right), then authman started again as
`startup.sh` does (`/base/sbin/authman -U 505`). Authman came up and made `/dev/authman`, but **every app
launch failed from then on**: the navigator, the launchers and others opened `/dev/authman` once at boot and
never reconnect (navigator: `pps_fd (-1)`, "pthread_mutex_lock failed - Resource deadlock avoided"). A reboot
put everything back. A patched `sys.acl` can only work if the link exists before authman starts at boot, and
root arrives about 4 minutes too late for that.

## Route 2: a new class with authman's own `load`, no restart — ruled out

What the disassembly says (2026-11-17):
- **The wire format** is not a `write()`: the launcher sends an `_IO_MSG` with `MsgSendv`, a 16-byte header
  (`type 0x113`, `combine_len 16`, `mgrid 0xf001`, `subtype 0`, `slen` = the command's length with its NUL,
  `rlen` = the reply buffer's size), then the command; the reply comes back after the same header
  (launcher `0x1bb48`). `tools/authman/authman-send.c` does the same.
- **Under a prefix** (`apply_capability`, `0x5df4`): a capability's name must start with the prefix;
  `ARQ` actions are refused ("Prefixed capability cannot contain %s actions"); `MAC` goes to the macro code;
  every other action, `RLM` included, goes to the generic parser. So `hmem_headless_128` with
  `RLM RLIMIT_DATA 134217728` should load.
- **Who may:** `authman.cfg` grants `regprefix,load` to `carrier_mgr`; authman reads the files with the
  client's uid. The helper switches to `carrier_mgr` (`-u`), and the files sit in a folder it can read.

`tools/authman/` builds `build/hmem-addon.zip` (`build-addon.sh`), an addon for the root installer
(`/var/rootdata/installer`, the VoLTE addon's layout): at install and at every boot its `start.sh` waits for
`/dev/authman`, copies `hmem.acl`/`hmem.res` to `/var/etc/hmem`, and sends `regprefix hmem` and
`load hmem /var/etc/hmem/hmem` as `carrier_mgr`. Classes: `hmem_headless_64`, `hmem_headless_128`,
allowed to any app (`allow *`). An app asks for one in its bar descriptor, beside `_sys_headless_xlarge`.

Still unknown, to test on the phone: whether root-as-`carrier_mgr` passes the whitelist; the `.res` syntax
(`allow *`); whether the installer keeps an app permission it doesn't know; whether the launcher applies
`RLM` from a loaded capability, and which limit wins beside `_sys_headless_xlarge`.

### Tried on the phone, 2026-11-17

`authman-send -u carrier_mgr 'regprefix hmem'` → ok (root as `carrier_mgr` passes the whitelist).
`load hmem /var/etc/hmem/hmem.res` → ok. **Every `.acl` → `EINVAL`**, the RLM one and a minimal probe
(`GRP nto`) alike; as root without `-u`, `EACCES` (the prefix is `carrier_mgr`'s). The disassembly says why:
each action creator gets the prefix, and `arlm_create` (`0x4a4c`) takes the refusal path as soon as a prefix is
set (`cmp r7, #0; bne 0x4b2c`, right after it recognises `RLM`). Authman never accepts an `RLIMIT` from a
capability loaded at run time. The addon in `tools/authman/` cannot work as designed; it stays as the record
of the protocol. The `hmem` prefix and its `.res` stay in authman until the next reboot, harmless.

## An overlay before authman starts: impossible

A system-data package can mount an rcfs before authman reads its ACL (getroot overlays `/base/bin` that way),
but installing one is the step that needs root, and root arrives too late in the boot (the user, 2026-11-17).

## What works: patching authman's memory (2026-11-17)

`arlm_create` keeps nothing of an `RLM` action but its text, `"rlm " RES " " VALUE` in a 0x200-byte buffer at
`+0x3c` of a 0x23c-byte object; the number is parsed only to check it isn't too large. The launcher gets that
text at each launch and parses it. Root reads and writes another process's memory through `/proc/<pid>/as`.

`tools/authman/mempatch.c` walks the process's regions (`DCMD_PROC_MAPINFO`), finds a NUL-terminated string,
and with `-w` replaces it only when it occurs exactly once, then reads it back. In authman (24 regions) each
class's text occurs once, three objects 0x248 apart: `rlm RLIMIT_DATA 31457280` (xlarge) at `0x1058a2d4`,
6291456 (medium) at `0x1058a51c`, 10485760 (large) at `0x1058a764`. Writing `rlm RLIMIT_DATA 134217728`
over the xlarge one: authman kept running, no client noticed, and **the next Tengram engine logged
`rlimit DATA: soft 134217728, hard 134217728`**. Every app that asks for `_sys_headless_xlarge` and launches
afterwards gets 128 MiB; apps running before keep 30 MiB until they restart; a reboot undoes it.

`tools/authman/build-addon.sh` builds `build/headless-mem.zip`, an addon for the root installer
(`/var/rootdata/installer <zip>`), installed on the test phone: at install and at every boot, once root is up,
its `start.sh` waits for `/dev/authman`, finds authman, and patches (or logs `already`), in
`/var/log/installer.log` as `[HMEM]`. The size is `MB=128` in `start.sh`. Still to see: a reboot, where root
comes about 4 minutes in, so an engine started earlier by the boot needs one restart.
