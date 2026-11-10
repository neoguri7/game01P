---
name: doc-digest
description: Verified pre-compression of one load-bearing source (paper, spec, long page) — every claim quoted verbatim, numbers checked by script, coverage ledger. Use when a design decision rests on a heavy source; routine browsing goes through the `browse` agent.
argument-hint: <url-or-path> [question it must serve]
---

This is the high-precision mode of pre-compression: the digest replaces the source for the reader, so recall comes first — a missing condition or number costs more than an extra record. Work in `docs/digests/<slug>/`.

## Steps
1. **Raw text.** Save the source as plain text to `source.txt`: PDF → `pdftotext -layout <pdf> source.txt`; web page → `curl -sL <url>` to a file and strip markup (arXiv: prefer the `/html/` version); local file → copy. Use raw text, never a summarizing fetch. Done when `source.txt` holds the full document.
2. **Ledger.** Split `source.txt` by its headings into sections; record each section's id, title and character span `[start, end]`. The spans together cover the whole file. Process sections in batches of ≤300,000 characters (keeps each prompt under 100K tokens, the cheap price tier).
3. **Typed records.** For every section, extract each load-bearing item as a record: `type` (number, definition, condition, method, result, limitation, counter, procedure), `claim` (one sentence), `quote` (verbatim text from that section). Anything with a number, unit, threshold, negation, or "only/unless/must/never" is load-bearing. Copy procedures, formats and must/never rules word for word. Fill `limitations` and `contradictions` for every section, writing "none found" when that is the result.
4. **Check.** Write `digest.json` (`{"source": "...", "sections": [{"id","title","span","records":[...],"limitations","contradictions"}]}`) and run `node .claude/skills/doc-digest/check-digest.mjs docs/digests/<slug>/digest.json docs/digests/<slug>/source.txt`. Fix every FAIL; for each WARN re-read that section and add the missing records. Done when the check exits 0.
5. **Omission pass.** Re-read each section against its records once more and add what is still missing; re-run the check.
6. **Brief.** Write `digest.md` (≤1,500 tokens): what the source is, the records that answer the given question, the limitations, and for each claim its section id so the reader can open the exact span. Return `digest.md`'s text plus the check's summary line.
