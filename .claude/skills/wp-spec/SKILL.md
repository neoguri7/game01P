---
name: wp-spec
description: Design a game01P work package before any code is written — produce a spec a Sonnet implementer can execute without asking a question. Use when starting a WP, a feature, a system change, or when the user says "design"/"spec"/"설계".
argument-hint: <WP-id> <one-line goal>
---

You are the designer. The spec is the product of this step: every decision the implementer would otherwise make is made here, in writing. A cheaper model executes it, so ambiguity left here becomes a bug or a wasted fix round later.

## Steps
1. **Frame.** Write the goal and the player-facing intent in 2–4 lines. Size the WP: **S** (≤3 files, no new system) or **L** (everything else). Done when the size and the "why" are written.
2. **Evidence, only where a decision needs it.** For an open design or engineering question, give the question to the `browse` agent (several in parallel for independent questions) and decide from its compressed findings; mark sources a decision rests on as load-bearing so it digests them in verified mode. Record each decision with its source. Numeric balance values are not researched here — they become knobs for the balance optimizer (spec section "Numbers"). Done when no decision rests on an unrecorded guess.
3. **Read the code the WP touches**, targeted (Grep for call sites, Read the exact functions). Done when every file in the file map is confirmed to exist, or is marked "create", and every signature you write matches the code.
4. **Write** `docs/wp/<id>/spec.md` from [template.md](template.md) and `docs/wp/<id>/features.json` (one entry per behavior ID, all `"passes": false`). S-size WPs use only the sections the template marks **[S]**.
5. **Gate** — reviewers run in parallel, in the background, in fresh contexts (EGM goldfish: they get the spec path, their standard, and the file-map files, nothing from this conversation). EGM standards live under `~/.claude/plugins/cache/egm/egm/*/resources/document_standards/`.
   - **S:** `spec-critic`.
   - **L:** `spec-critic` + `egm:egm-comprehension` (a cold reader: could an implementer execute this without asking?) + `egm:egm-implementation-readiness` with `implementation-readiness-standard.md`.
   - **New system or architecture change:** add `egm:egm-zoom-out` (`zoom-out-standard.md`) and `egm:egm-design` (`feature-spec-standard.md`).
   You are the elephant: resolve each finding yourself (fix it, or decline with a stated reason); only a finding that would reverse a user decision goes to the user. Done when no Blocker/Major finding remains and spec-critic says PASS. Soft cap 2 rounds; then show the user what remains.
6. **Hand off.** Launch the `implementer` agent in the background with the WP id and the absolute spec path; it works in its own git worktree. When it reports, launch `diff-reviewer` on its branch; send must-fix findings back to the same implementer (SendMessage) so it keeps its context.

## Quality bar
- **Zero open questions** at handoff: "TBD", "maybe", "or similar", "as appropriate" are open questions.
- Every behavior is testable: an ID, a WHEN/THEN statement, and the test that proves it.
- Every task ends on a command and its expected result.
