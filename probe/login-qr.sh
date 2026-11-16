#!/bin/sh
# Logs tdprobe in by QR code, from the Mac: runs it on the phone over ssh and draws each login link
# as a QR code in this terminal. Scan it with an official Telegram mobile app:
# Settings > Devices > Link Desktop Device. The link is renewed about every 30 s; scan the newest.
# If the account has a 2FA password, type it when asked (it is shown as you type).
# Usage: TG_API_ID=... TG_API_HASH=... probe/login-qr.sh <tdprobe name on the phone>
# Needs qrencode (brew install qrencode).
set -eu
PHONE=${PHONE:-bb10rootx}
ssh "$PHONE" "cd /root/tengram && TG_API_ID=$TG_API_ID TG_API_HASH=$TG_API_HASH ./$1 /root/tengram/data --qr --once" |
while IFS= read -r line; do
    printf '%s\n' "$line"
    case $line in
    *"tg://login?token="*) qrencode -t ANSIUTF8 -m 2 "tg://login?token=${line##*tg://login?token=}" ;;
    esac
done
