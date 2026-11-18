# Installing a .bar through the root PPS installer

How Tengram's unsigned `.bar` goes onto the phone with no Momentics, no BAR deployer and no developer
mode. The route is HyperBerry's. It needs the root shell on `bb10rootx` (USB, 169.254.0.1).

## The idea

BB10's installer daemon watches a PPS folder, `/pps/system/installer/upd/current/`. Root writes a
**job file** there, and the installer runs it like any install it is asked to do: it reads the
`.bar` from the given path, installs or upgrades the app, and appends the outcome to the same file.
Since every phone is rooted, nothing else is needed: no signing and no debug token.

## Steps

```sh
# 1. Copy the package to a folder the installer can read
scp build/tengram.bar bb10rootx:/var/rootdata/tgN.bar

ssh bb10rootx '
  chmod 644 /var/rootdata/tgN.bar
  J=/pps/system/installer/upd/current/job.tgN.bar

  # 2. Create the job file, writable by the installer (umask 000, mode 666)
  (umask 000; touch $J; chmod 666 $J)

  # 3. The job: what to do, and where the package is
  echo "action::install"                          >> $J
  echo "package_location::/var/rootdata/tgN.bar"  >> $J
  echo "extras::"                                 >> $J

  # 4. Wait up to 2 minutes for the installer to write result:: into the file
  i=0; while [ $i -lt 60 ]; do grep -q "result::" $J && break; sleep 2; i=$((i+1)); done
  grep -E "result::|actual_version::" $J

  # 5. Clean up
  rm -f /var/rootdata/tgN.bar $J
'
```

`N` is any tag, e.g. the build number. It only keeps the job file names apart.

## Reading the outcome

- `result::success` and `actual_version::0.1.0.N`: the install worked.
- Any other `result::` line carries the installer's error. The file stays the same if nothing answers
  within 2 minutes.

## What to know

- **Package-Version must go up.** With the same version the installer keeps the old app.
  `scripts/build-app.sh` takes the next build id from `build/buildid.txt`.
- **An install closes the app and its engine** (`tengramd`). Reopen it by hand.
- **Data survives an upgrade.** The sandbox (`chats.db`, `db/`, logs) is kept.
- **Addons are a different route.** `/var/rootdata/installer <zip>`, logged in `/var/log/installer.log`.
- **The rule here:** an agent installs only when you say so, each time (`docs/HANDOFF.md` §3).
