# Session-Log Mining: filling token_usage_statistics.txt

Session-specific detail backing the "Session Log Maintenance" section of
SKILL.md. Recorded 2026-09-12 while logging the Dodo game-engine run.

## Schema discovered (state.db `sessions` table)

```
id                TEXT  e.g. '20260912_222542_f9fa24'  (= YYYYMMDD_HHMMSS_xxxxx)
title             TEXT  often NULL; set later via `hermes sessions rename`
started_at        REAL  unix seconds -> datetime.fromtimestamp(ts)
message_count     INTEGER
tool_call_count   INTEGER
api_call_count    INTEGER
input_tokens      INTEGER
output_tokens     INTEGER
cache_read_tokens INTEGER
cache_write_tokens, reasoning_tokens, estimated_cost_usd ...
model             TEXT  e.g. 'glm-5.3-flash'
```

## Query pattern

```python
import sqlite3, os
con = sqlite3.connect(os.path.expanduser("~/.hermes/state.db"))
rows = con.execute(
    "SELECT id, title, started_at, message_count, tool_call_count, "
    "api_call_count, input_tokens, output_tokens, cache_read_tokens, model "
    "FROM sessions ORDER BY started_at DESC LIMIT 10").fetchall()
```

- No `sqlite3` CLI on this machine — use python3's sqlite3 module.
- Context resets / /new spawn a NEW session row whose title repeats the
  original. The live conversation can therefore span 2+ session rows: find
  them with `WHERE title LIKE '<known prefix>%'` or by started_at on the
  known date, and SUM their counters for the true session total.
- `hermes insights --days N` is a quick cross-check and gives top-tools /
  top-skills tables, but numbers keep increasing while the session is alive.

## Log-entry format that the user's file uses

Per session block: `[Session N]` header, id, start datetime, description of
what was built + what the run added to the skill (scripts/templates/pitfalls),
model, messages / tool calls / API calls / input / output / cache-read /
total tokens. Then update the CUMULATIVE footer (mark approximates with ~).
Note: the existing file's header names models from earlier runs
(deepseek-v4.1-flash / gemini-3.8-flash); keep per-session `Model:` lines
authoritative and leave the file header alone unless the user asks.

- Sessions that went unlogged (work landed in the skill, but no
  `[Session N]` block was appended at the time) must NOT be silently folded
  into the next block. Add a labeled adjustment line to the cumulative
  footer instead — e.g. "incl. two unnumbered Sep-14 runs (~186 msgs,
  ~5.8M tok)" — with their state.db totals, so the math stays auditable.
  First hit: Session 9 (SVG pipeline), where sessions 8's and 9's numbering
  had silently skipped the install-instructions update and NOTEPAD-80 build.

## Concurrent writeback guard (first hit: Sep 15, 2026)

The installed skill tree can be updated by a PARALLEL session while yours is
still running: this session appended Pitfalls 36-38 that a sibling session had
already landed (as 36-40, from the same build run) — the duplicate had to be
removed afterward, and the version had already been bumped.

Before appending session-derived pitfalls or blocks:

- Re-read the installed SKILL.md (and the relevant reference file) immediately
  before patching — do not trust an earlier read or the conversation-start
  snapshot; search the current pitfall numbering before choosing the next number.
- If the sibling already captured the lesson, extend THEIR entry (one merged
  sentence) instead of adding a parallel section; delete your draft, not theirs.
- Same discipline for this ledger: re-query state.db right before writing the
  block (live counters move), and check whether a sibling already appended its
  own `[Session N]` or adjustment line while you worked.
