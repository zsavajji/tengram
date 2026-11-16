#!/bin/sh
# Copies a program or a shell script to the phone over ssh, makes it runnable, prints its path there.
# Usage: scripts/push.sh <file> [check-argument]     (PHONE=bb10rootx, DIR=/root/tengram)
#
# BB10 runs a new file (scripts included) only once `/proc/boot/pathtrust !<file>` trusts that one
# file, and a file it refused stays refused, even as a new file that reuses its inode. So each
# version gets a fresh name, <basename>-<sha8>-<n>, it is trusted before its first run, and a
# refused copy is emptied and parked in denied/ (kept, so its inode is never handed out again).
# The check is `<file> <check-argument>` exiting with anything but 126 (not permitted); the default
# argument is -h. For a script that must not run as a check, pass `-n` handling yourself or use
# a harmless argument. From ../hyperberry/scripts/push-engine.sh.
set -eu
PHONE=${PHONE:-bb10rootx}
DIR=${DIR:-/root/tengram}
SRC=$1
CHECK=${2:--h}
base=$(basename "$SRC")
name=$base-$(shasum -a 256 "$SRC" | cut -c1-8)
scp -q -o BatchMode=yes "$SRC" "$PHONE:/dev/shmem/$name"
ssh -n -o BatchMode=yes "$PHONE" "mkdir -p $DIR/denied && cd $DIR && for n in 1 2 3 4 5 6; do
	f=$name-\$n
	[ -e \$f ] || { cp /dev/shmem/$name \$f && chmod +x \$f && /proc/boot/pathtrust !$DIR/\$f; }
	./\$f $CHECK >/dev/null 2>&1 </dev/null; rc=\$?
	if [ \$rc -ne 126 ]; then
		rm -f /dev/shmem/$name; echo $DIR/\$f; exit 0
	fi
	: > \$f; mv \$f denied/
done
rm -f /dev/shmem/$name; echo 'no copy would run' >&2; exit 1"
