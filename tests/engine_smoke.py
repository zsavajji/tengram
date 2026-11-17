#!/usr/bin/env python3
"""A smoke test of tengramd on the phone, on the real account but only in Saved Messages.

  engine_smoke.py [--host bb10rootx] [--dir /root/tengram/data] [--batched]

Needs the engine running and logged in. It opens Saved Messages, pages its history, sends a text,
edits it, deletes it again, and checks a few refusals. --batched also runs one offline/online
cycle of the delivery mode (and puts the prefs back to auto, 300 s). Exit 0 when every check passed.
It never logs out and never touches another chat.
"""
import argparse
import json
import select
import socket
import subprocess
import sys
import time


class Engine:
    def __init__(self, host, data_dir):
        info = json.loads(subprocess.check_output(
            ["ssh", "-o", "BatchMode=yes", host, "cat", data_dir + "/engine.json"], text=True))
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0))
            local = s.getsockname()[1]
        self.tunnel = subprocess.Popen(["ssh", "-N", "-o", "BatchMode=yes", "-o", "ExitOnForwardFailure=yes",
                                        "-L", "%d:127.0.0.1:%d" % (local, info["port"]), host])
        self.sock = None
        for _ in range(100):
            try:
                self.sock = socket.create_connection(("127.0.0.1", local), timeout=1)
                break
            except OSError:
                time.sleep(0.1)
        if self.sock is None:
            self.close()
            sys.exit("the tunnel did not come up")
        self.buf = b""
        self.next_id = 0
        self.events = []
        self.hello = self.call("hello", token=info["token"], proto=1)

    def close(self):
        self.tunnel.terminate()
        self.tunnel.wait()

    def _lines(self, timeout):
        """The lines that arrive within timeout seconds (at least one read)."""
        out = []
        if select.select([self.sock], [], [], timeout)[0]:
            data = self.sock.recv(65536)
            if not data:
                raise SystemExit("the engine closed the connection")
            self.buf += data
            while b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                out.append(json.loads(line))
        return out

    def call(self, cmd, timeout=30, **fields):
        """Sends one request and returns its reply; events that come meanwhile are kept."""
        rid = str(self.next_id)
        self.next_id += 1
        self.sock.sendall(json.dumps(dict(fields, **{"@": rid, "cmd": cmd})).encode() + b"\n")
        end = time.time() + timeout
        while time.time() < end:
            reply = None
            for msg in self._lines(end - time.time()):  # all of them: events can follow the reply in one read
                if msg.get("@") == rid:
                    reply = msg
                else:
                    self.events.append(msg)
            if reply:
                return reply
        raise SystemExit("no reply to %s in %d s" % (cmd, timeout))

    def wait_event(self, want, timeout=20):
        """The first event, kept or new, for which want(event) is true; None after timeout."""
        end = time.time() + timeout
        while True:
            for i, ev in enumerate(self.events):
                if want(ev):
                    del self.events[:i + 1]
                    return ev
            self.events = []
            if time.time() >= end:
                return None
            self.events.extend(m for m in self._lines(end - time.time()) if "ev" in m)


failed = 0


def check(what, ok, detail=""):
    global failed
    failed += not ok
    print("%s  %s%s" % ("ok  " if ok else "FAIL", what, "" if ok else "   " + str(detail)[:200]))
    return ok


def code(reply):
    return (reply.get("error") or {}).get("code")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="bb10rootx")
    ap.add_argument("--dir", default="/root/tengram/data")
    ap.add_argument("--batched", action="store_true", help="also run one cycle of the batched mode")
    args = ap.parse_args()
    e = Engine(args.host, args.dir)
    try:
        run(e, args)
    finally:
        e.close()
    print("%d failed" % failed if failed else "all passed")
    sys.exit(1 if failed else 0)


def run(e, args):
    link = e.hello.get("result") or {}
    if not check("hello: logged in", link.get("state") == "ready" and link.get("me"), e.hello):
        return
    me = link["me"]  # Saved Messages is the chat with ourselves

    r = e.call("chat.open", chat=me)
    page = r.get("result") or {}
    check("chat.open: a verified page", r.get("ok") and page.get("count", 0) >= 0 and "oldest" in page, r)
    if page.get("oldest"):
        r = e.call("chat.history", chat=me, before=page["oldest"], limit=10)
        check("chat.history: at most 10 older", r.get("ok") and r["result"]["count"] <= 10, r)

    r = e.call("msg.send", chat=me, text="Tengram smoke test")
    temp = (r.get("result") or {}).get("id")
    check("msg.send: a temporary id", r.get("ok") and temp, r)
    ev = e.wait_event(lambda v: v.get("ev") == "msgs" and v.get("chat") == me and temp in v.get("ids", [])
                      and len(v["ids"]) >= 2)
    final = next((i for i in (ev or {}).get("ids", []) if i != temp), None)
    if check("msg.send: the final id replaces it", final, ev):
        r = e.call("msg.edit", chat=me, id=final, text="Tengram smoke test (edited)")
        check("msg.edit", r.get("ok"), r)
        ev = e.wait_event(lambda v: v.get("ev") == "msgs" and final in v.get("ids", []))
        check("msg.edit: the row changed", ev, "no msgs event")
        r = e.call("msg.delete", chat=me, ids=[final], everyone=True)
        check("msg.delete", r.get("ok"), r)
        ev = e.wait_event(lambda v: v.get("ev") == "msgs" and final in v.get("ids", []))
        check("msg.delete: the row went", ev, "no msgs event")

    check("chat.read", e.call("chat.read", chat=me).get("ok"))
    check("chat.typing on", e.call("chat.typing", chat=me, on=True).get("ok"))
    check("chat.typing off", e.call("chat.typing", chat=me, on=False).get("ok"))
    check("chat.close", e.call("chat.close", chat=me).get("ok"))

    check("unknown command refused", code(e.call("nope")) == "unknown_cmd")
    check("unknown chat refused", code(e.call("chat.open", chat="1")) == "bad_chat")
    check("msg.send without text refused", code(e.call("msg.send", chat=me)) == "bad_request")
    check("link.qr refused while logged in", code(e.call("link.qr")) == "linked")
    check("prefs.set out of range refused", code(e.call("prefs.set", batchSecs=5)) == "bad_request")

    if args.batched:
        e.events = []
        check("prefs.set batched", e.call("prefs.set", mode="batched", batchSecs=60).get("ok"))
        ev = e.wait_event(lambda v: v.get("ev") == "conn" and v.get("state") == "offline", timeout=30)
        check("batched: the connection closes", ev, "no conn offline event in 30 s")
        check("ui.state in front", e.call("ui.state", fullscreen=True, openChat="").get("ok"))
        ev = e.wait_event(lambda v: v.get("ev") == "conn" and v.get("state") == "ready", timeout=30)
        check("in front: the connection is back", ev, "no conn ready event in 30 s")
        check("prefs back to auto", e.call("prefs.set", mode="auto", batchSecs=300).get("ok"))
        check("ui.state not in front", e.call("ui.state", fullscreen=False).get("ok"))


if __name__ == "__main__":
    main()
