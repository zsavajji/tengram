# tdprobe

Measures TDLight on BB10: start time, memory, CPU, `getMemoryStatistics`. Needs TDLight installed in `build/prefix`.

Build: `scripts/build-deps.sh` once, then `probe/build.sh` (both on the Mac, they use Docker). The result is `build/tdprobe`, stripped.
Run (phone): `TG_API_ID=... TG_API_HASH=... ./tdprobe <data-dir> [--once] [--no-history] [--background] [--no-files] [--interval <s>]`. First run logs in on stdin; `./measure.sh [-i <s>] <args>` then runs it detached and samples `pidin` into `tdprobe-mem.log`.
Flags: `--once` exits after the load and one memory print; `--background` sets online=false after the load (default: online=true); `--no-files` sets the TDLight option `disable_auto_download` (local; `setAutoDownloadSettings` writes to the Telegram account, so it is not used); `--interval` is the status period, default 300 s, 0 = only SIGUSR1. `--batch <s>` (implies `--background`) sets the network to None after the load, then every <s> s runs one sync (WiFi, wait for Ready + 3 s with no update, None; 60 s limit) and prints `sync: connect=<ms to Ready> total=<ms> cpu=+<ms> updates=<n>` or `sync: timeout`; timed `status:` lines then skip `mem:` (SIGUSR1 still prints it).
Output: every line starts with `[ms since process start]`. `auth: X` marks each authorization state (`WaitPhoneNumber` = cold start to network, `Ready` = logged in); `conn: X` the connection state; `chats:` and `history:` the load phases with counts and ms.
`status:` uptime, updates so far, chats known, process CPU ms (and delta since the last status). `upd:` update counts per type since the last status. `mem:` TDLight memory statistics, one line per output line.
`kill -USR1 <pid>` prints a status now. RSS is not read here: use `pidin -p <pid> mem` (measure.sh does).
