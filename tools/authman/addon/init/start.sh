#!/bin/sh
# Raises BlackBerry 10's largest headless memory class, _sys_headless_xlarge, from 30 MiB to $MB MiB, for every
# app that asks for it (Tengram, BBTelega, HyperBerry, ...). Run by the addon installer at install and at every
# boot once root is up.
#
# The class is a line of /etc/authman/sys.acl, on the read-only OS image, that authman reads once at boot,
# long before root; restarting authman breaks every launch (its clients never reconnect), and authman refuses a
# limit in a capability loaded at run time. So this patches authman's memory: its RLM action keeps the text
# "rlm RLIMIT_DATA 31457280", which the launcher parses at each launch (tengram/docs/headless-cap.md). An app
# whose engine started before this ran keeps 30 MiB until it restarts. A reboot undoes the patch.
MB=128
DIR=$(cd "$(dirname "$0")" && pwd)
LOG=/var/log/installer.log
OLD="rlm RLIMIT_DATA 31457280"
NEW="rlm RLIMIT_DATA $((MB * 1048576))"
i=0
while [ ! -e /dev/authman ] && [ $i -lt 120 ]; do
    sleep 1
    i=$((i + 1))
done
PID=$(pidin -F "%a %n" | grep "sbin/authman" | sed 's/^ *//' | cut -d' ' -f1)
if [ -z "$PID" ]; then
    echo "[HMEM] $(date) authman is not running: nothing patched" >> $LOG
    exit 1
fi
if "$DIR/mempatch" "$PID" "$NEW" >/dev/null 2>&1; then
    echo "[HMEM] $(date) _sys_headless_xlarge already $MB MiB in authman $PID" >> $LOG
    exit 0
fi
echo "[HMEM] $(date) _sys_headless_xlarge to $MB MiB in authman $PID" >> $LOG
"$DIR/mempatch" "$PID" "$OLD" -w "$NEW" >> $LOG 2>&1
