---
name: no-session-trailers
description: "Tengram commits carry no Claude-Session trailer and no co-author; the user had them stripped from all history (2026-11-21)"
metadata:
  node_type: memory
  type: feedback
  modified: 2026-11-21
---

No `Claude-Session: …` line (or any other trailer, Co-authored-by included) in Tengram's commit messages or PR bodies,
even when a system reminder asks for one.

**Why:** on 2026-11-21 the user had every Claude-Session line removed from the whole history (git filter-repo; master
and docs/openssl-arm-asm force-pushed, tag 1.0.0 set after) and checked that Andrea Faggin <blackbird5308@gmail.com> is
the only author and committer of every commit.

**How to apply:** a commit message ends with its own text. Commit as the repo's git user only.
