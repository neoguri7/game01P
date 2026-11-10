---
name: wp-spec
description: Design a game01P work package before any code is written — game design (player decision, readability, playtest hypothesis) and an engineering spec a Sonnet implementer can execute without asking a question. Use when starting a WP, a feature, a system change, or when the user says "design"/"spec"/"설계".
argument-hint: <WP-id> <one-line goal>
---

You are the designer. The spec carries two things: the game design (why this is worth playing) and every implementation decision (so a cheaper model executes it without choosing). Read `docs/design/decisions.md` and `combat-math.md` first; they bind every WP.

## Steps
1. **Frame.** Write the goal and the decisions it rests on. Set the header flags: size **S** (≤3 files, no new system) or **L**; **player-facing** yes/no; **certainty** settled/uncertain. Done when the header and §1 are filled.
2. **Settle open questions by kind.**
   - **Fact questions** ("how do proven games solve X", "what does the research show") → the `browse` agent, several in parallel for independent questions; mark sources a decision rests on as load-bearing. Reference games: dungeon and expedition design → Darkest Dungeon; build design → Path of Exile; UI and information design → Pillars of Eternity.
   - **Taste questions** (what the player should feel, which of two proven designs fits this game) with no ruling in `decisions.md` → collect them and ask the user once, as one-liners each with your recommendation and its source, so the user can accept them in one reply. Record each answer in `decisions.md`.
   - Research never overrides a 확정 ruling in `decisions.md`.
   Done when every decision is recorded with its source or the user's answer.
3. **Uncertain mechanics prototype first.** If certainty is "uncertain", stop the spec here and propose the cheapest playable probe (a data-only tweak or a throwaway prototype) for the user to play; continue after their feedback.
4. **Read the code the WP touches**, targeted (Grep for call sites, Read the exact functions). Done when every file in the file map is confirmed to exist, or is marked "create", and every signature you write matches the code.
5. **Write** `docs/wp/<id>/spec.md` from [template.md](template.md) and `docs/wp/<id>/features.json` (`{"wp": "<id>", "files": [...], "features": [{"id", "behavior", "test", "passes": false}]}`). S-size WPs use only the sections marked **[S]**, plus §2 when player-facing.
6. **Gate** — reviewers run in parallel, in the background, in fresh contexts (EGM goldfish: they get the spec path, their standard, and the file-map files, nothing from this conversation). EGM standards live under `~/.claude/plugins/cache/egm/egm/*/resources/document_standards/`.
   - **S:** `spec-critic`.
   - **L:** `spec-critic` + `egm:egm-comprehension` (a cold reader: could an implementer execute this without asking?) + `egm:egm-implementation-readiness` with `implementation-readiness-standard.md`.
   - **New system or architecture change:** add `egm:egm-zoom-out` (`zoom-out-standard.md`) and `egm:egm-design` (`feature-spec-standard.md`).
   You are the elephant: resolve each finding yourself (fix it, or decline with a stated reason); only a finding that would reverse a user decision goes to the user. Done when no Blocker/Major finding remains and spec-critic says PASS. Soft cap 2 rounds; then show the user what remains.
7. **Hand off.** Commit `docs/wp/<id>/` to main, so the spec commit is the implementer's base. Launch the `implementer` agent in the background with the WP id, the spec path and that base SHA. When it reports, launch `diff-reviewer`; send must-fix findings back to the same implementer (SendMessage) so it keeps its context. Before review run `node .claude/scripts/wp-gate.mjs <id> --worktree D:/repos/game01P --base <sha>`; merge by hand as in CLAUDE.md lifecycle step 4. Launch the next WP only after the previous one merged or was parked (one checkout).

## Quality bar
- **Zero open implementation questions** at handoff: "TBD", "maybe", "or similar", "as appropriate" are open questions. Design unknowns live in the playtest hypothesis instead.
- Every behavior is testable: an ID, a WHEN/THEN statement, and the test that proves it.
- Every player-facing choice has a stated trade-off and is readable before the player commits.
- Every task ends on a command and its expected result.
