#!/bin/sh
# Run tdprobe in the background on the phone and sample its memory and CPU time until it exits,
# or for a fixed time.
# Usage: TG_API_ID=... TG_API_HASH=... [TDPROBE=./tdprobe-<sha8>-1] ./measure.sh
#            [-i <sample seconds, default 60>] [-t <stop after seconds>] [-o <log prefix, default tdprobe>]
#            <tdprobe args...>
# tdprobe's output goes to <prefix>.log, the samples to <prefix>-mem.log. stdin is /dev/null, so
# log in once first (probe/login-qr.sh). Push it with scripts/push.sh: the phone runs a new
# script only once it is trusted, and `-h` is what push.sh checks it with.
[ "${1:-}" = "-h" ] && { echo "usage: measure.sh [-i s] [-t s] [-o prefix] <tdprobe args...>"; exit 0; }
interval=60
limit=0
prefix=tdprobe
while :; do
    case "${1:-}" in
    -i) interval=$2; shift 2 ;;
    -t) limit=$2; shift 2 ;;
    -o) prefix=$2; shift 2 ;;
    *) break ;;
    esac
done
[ -n "${TG_API_ID:-}" ] && [ -n "${TG_API_HASH:-}" ] || { echo "set TG_API_ID and TG_API_HASH" >&2; exit 2; }

"${TDPROBE:-./tdprobe}" "$@" </dev/null >>"$prefix.log" 2>&1 &
pid=$!
echo "tdprobe pid $pid, sampling every ${interval}s into $prefix-mem.log (kill $pid to stop)"

elapsed=0
while kill -0 "$pid" 2>/dev/null; do
    {
        echo "=== $(date '+%Y-%m-%d %H:%M:%S') pid=$pid"
        pidin -p "$pid" mem
        pidin -p "$pid" times
    } >>"$prefix-mem.log" 2>&1
    if [ "$limit" -gt 0 ] && [ "$elapsed" -ge "$limit" ]; then
        echo "=== $(date '+%Y-%m-%d %H:%M:%S') time limit, stopping tdprobe" >>"$prefix-mem.log"
        kill "$pid"
        break
    fi
    sleep "$interval"
    elapsed=$((elapsed + interval))
done
wait "$pid"
status=$?
echo "=== $(date '+%Y-%m-%d %H:%M:%S') tdprobe exited, status $status" >>"$prefix-mem.log"
