#!/bin/sh
# Adds two headless memory classes to authman, hmem_headless_64 and hmem_headless_128 (RLIMIT_DATA 64 and
# 128 MiB), beside the stock ones in /etc/authman/sys.acl, whose largest, _sys_headless_xlarge, is 30 MiB. An
# app gets one by asking for it in its bar (<permission system="true">hmem_headless_128</permission>).
# Run by the addon installer at install and at every boot once root is up (about 4 minutes in): authman is
# never restarted (its clients don't reconnect), the classes are added with its own regprefix and load, as
# carrier_mgr adds the carrier's capabilities. A reboot forgets them until this runs again. See
# tengram/docs/headless-cap.md.
DIR=$(cd "$(dirname "$0")/.." && pwd)
LOG=/var/log/installer.log
# authman reads load's files as the client, carrier_mgr, which can't read /var/addons.
ETC=/var/etc/hmem
i=0
while [ ! -e /dev/authman ] && [ $i -lt 120 ]; do
    sleep 1
    i=$((i + 1))
done
mkdir -p $ETC
chmod 755 $ETC
cp "$DIR/data/hmem.acl" "$DIR/data/hmem.res" $ETC/
chmod 644 $ETC/hmem.acl $ETC/hmem.res
echo "[HMEM] $(date) adding the headless memory classes" >> $LOG
"$DIR/init/authman-send" -u carrier_mgr "regprefix hmem" "load hmem $ETC/hmem" >> $LOG 2>&1
