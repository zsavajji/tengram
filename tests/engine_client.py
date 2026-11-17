#!/usr/bin/env python3
"""Talks to tengramd on the phone through an ssh tunnel and prints what it says.

  engine_client.py [--host bb10rootx] [--dir /root/tengram/data] [--listen SECONDS] [cmd [json-object]]...

Reads engine.json over ssh, opens a tunnel to the engine's port, says hello, then sends each cmd with the
fields of the JSON object that follows it (if the next argument starts with "{"), as requests "1", "2", ...
Every line the engine sends is printed as it arrives, with the seconds since the connection. It stops when
every reply is in and --listen seconds have passed since (default 0), or when the engine closes the
connection. Example:

  engine_client.py link.phone '{"phone":"+391234567890"}' --listen 30
"""
import argparse
import json
import select
import socket
import subprocess
import sys
import time


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def requests_from(args):
    """["a", "{..}", "b"] -> [("a", {..}), ("b", {})]"""
    out = []
    for a in args:
        if a.startswith("{"):
            if not out:
                sys.exit("a JSON object needs a cmd before it")
            out[-1][1].update(json.loads(a))
        else:
            out.append((a, {}))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="bb10rootx")
    ap.add_argument("--dir", default="/root/tengram/data", help="the engine's data folder on the phone")
    ap.add_argument("--listen", type=float, default=0, help="seconds to keep listening after the last reply")
    ap.add_argument("commands", nargs="*", metavar="cmd|json")
    args = ap.parse_intermixed_args()
    reqs = requests_from(args.commands)

    try:
        info = json.loads(subprocess.check_output(
            ["ssh", "-o", "BatchMode=yes", args.host, "cat", args.dir + "/engine.json"], text=True))
    except subprocess.CalledProcessError:
        sys.exit("cannot read %s/engine.json on %s: is the engine running?" % (args.dir, args.host))

    local = free_port()
    tunnel = subprocess.Popen(["ssh", "-N", "-o", "BatchMode=yes", "-o", "ExitOnForwardFailure=yes",
                               "-L", "%d:127.0.0.1:%d" % (local, info["port"]), args.host])
    try:
        sock = None
        for _ in range(100):
            if tunnel.poll() is not None:
                sys.exit("the ssh tunnel ended: %s" % tunnel.returncode)
            try:
                sock = socket.create_connection(("127.0.0.1", local), timeout=1)
                break
            except OSError:
                time.sleep(0.1)
        if sock is None:
            sys.exit("the tunnel did not come up")
        talk(sock, info["token"], reqs, args.listen)
    finally:
        tunnel.terminate()
        tunnel.wait()


def talk(sock, token, reqs, listen):
    t0 = time.time()
    buf = b""
    pending = {"0"}
    sent = False
    quiet_since = None

    def send(obj):
        sock.sendall(json.dumps(obj).encode() + b"\n")

    send({"@": "0", "cmd": "hello", "token": token, "proto": 1})
    while True:
        if not pending and quiet_since is None:
            quiet_since = time.time()
        wait = 1.0
        if quiet_since is not None:
            left = quiet_since + listen - time.time()
            if left <= 0:
                return
            wait = left
        if not select.select([sock], [], [], wait)[0]:
            continue
        data = sock.recv(65536)
        if not data:
            print("%7.3f connection closed by the engine" % (time.time() - t0))
            return
        buf += data
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", "replace")
            print("%7.3f %s" % (time.time() - t0, text), flush=True)
            try:
                msg = json.loads(text)
            except ValueError:
                continue
            if isinstance(msg, dict) and msg.get("@") in pending:
                pending.discard(msg["@"])
                if msg["@"] == "0" and not msg.get("ok"):
                    return  # refused: the engine closes the connection
                if msg["@"] == "0" and not sent:
                    sent = True
                    for i, (cmd, fields) in enumerate(reqs, 1):
                        pending.add(str(i))
                        send(dict(fields, **{"@": str(i), "cmd": cmd}))
                    quiet_since = None


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
