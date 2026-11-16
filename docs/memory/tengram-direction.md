---
name: tengram-direction
description: "Tengram = Telegram client for BB10; decisions taken 2026-11-15 (100% C++, HyperBerry UI fork, TDLib/TDLight)"
metadata:
  node_type: memory
  type: project
  originSessionId: 399035a0-4ff9-467a-8855-a6f7d958449c
  modified: 2026-11-15T17:14:20.817Z
---

Tengram (this dir, empty as of 2026-11-15) is a Telegram client for BlackBerry 10 (Q10 reference).

Decisions by the user, 2026-11-15:
- **100% C++** — engine included (no Go). Engine uses TDLib's native C++ API.
- UI: fork ../hyperberry's Cascades app; keep its engine/UI split (chats.db read-only by UI + NDJSON loopback protocol).
- TDLib pinned as (repo, commit) so ../tdlight (tdlight-team/tdlight) can be swapped in; QNX port patches kept as a series under patches/td/.
- User's point: TDLib runs fine on the Nokia N9 (../../meegram2), so the Q10 (dual Krait, 2 GB) has headroom. The real risks are BB10-specific: ~30 MB headless engine cap (hyperberry docs/plan/phase6.md) and TDLib never built for QNX (tele10 gate G2).

**Why:** user wants a WhatsApp-quality (HyperBerry) Telegram app; meegram2 proves TDLib on weaker hardware.
**How to apply:** first step is the TDLib-on-QNX probe with memory measurement on the Q10; reuse meegram2's daemon/Notifier patterns and ../tele10/PLAN.md engine design. Calls dropped for v1 (tgcalls port out of scope).
