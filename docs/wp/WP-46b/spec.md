# WP-46b: Theme victory choice above the fold

Size: S · Player-facing: yes · Certainty: settled · Base commit: the commit that adds this spec · Branch: wp/WP-46b

## 1. Goal
A windowed check of WP-46 (1280x800) showed the theme-cleared camp with its two choice buttons, "Descend deeper" and "Return in triumph", below the fold: the RUNEWORDS block and the loadout sit above them, so the player has to scroll to find the choice the screen exists for. This follow-up moves the choice to the top of the theme-cleared camp and wraps its explanation line, which the review expected to be clipped at the 580-pixel window width.
- Decisions it rests on: 런의 끝 (WP-46) in `docs/design/decisions.md`; the WP-46 diff review (should-fix 2) and the window capture of 2026-11-12.

## 2. Player experience
- At a theme-cleared camp the heading, the run message and the line "The boss of <theme> has fallen. Seals: N." are followed directly by a section `EXPEDITION CHOICE` with the two buttons and the explanation, all visible without scrolling. The camp's services follow below as before.
- At every other camp nothing changes: the descend button stays at the bottom.
- Playtest hypothesis: expect the player to see the choice as soon as the camp opens · observe no remark about missing the return option · kill-if the player still scrolls past it.

## 3. Behaviors
| ID | Behavior | Proving test |
|---|---|---|
| F1 | WHEN the camp modal is rendered at a theme-cleared camp THEN the texts "Descend deeper: " and "Return in triumph" SHALL appear before "REST OR REINFORCE" and before "RUNEWORDS", each exactly once; at another camp the descend button SHALL stay after the loadout. | `camp_tests::CampViewShowsThemeVictory` |

## 5. File map
Modify:
- `src/presentation/FDungeonCampView.cpp` — `RenderDungeonCampView`.
- `src/gameplay/dungeon/FRunFlow.h` — two stale comments ("five core phases" / "five core phase ids" become six; review nit).
- `tests/features/camp_tests.cpp` — the extended case.

`RenderDungeonCampView`:
- When `themeCleared`, directly after the "The boss of … has fallen. Seals: …" line and before the separator that precedes the checkpoint line: `ImGui::SeparatorText("EXPEDITION CHOICE");`, then inside `ImGui::BeginDisabled(offerOpen)` … `ImGui::EndDisabled()` the two existing buttons with unchanged labels, sizes and intents ("Descend deeper: <next>" posts `LeaveCamp`; "Return in triumph (ends the expedition as a success)" posts `ReturnInTriumph`; both close the popup). When `offerOpen`, `ImGui::TextDisabled("Pick a rune or skip the offer first.");` under them.
- The explanation line under the triumph button is drawn wrapped in the disabled colour: `ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); ImGui::TextWrapped(...); ImGui::PopStyleColor();`. Its text is "Ends the run: its runes, levels and materials end with it. The checkpoint stays." followed by " Seals +N." only when `!MetaDisabled(registry)`.
- At the bottom of the modal the descend button, the "Pick a rune or skip the offer to descend." line and the triumph button are drawn only when `!themeCleared` (the triumph button no longer exists there at all). "End expedition at checkpoint" stays at the bottom in both cases.
- The doc comment above the `DrawAbandonConfirm` definition that still calls "Save and leave" the battle's option is updated (review nit).

Do not touch: everything else.

## 6. Tasks
Work in the developer seat named in the handoff, on branch `wp/WP-46b`. Build only what you need (`cmake --build --preset windows-msvc-agent --target camp_tests game01P`, `ctest --preset windows-msvc-agent -R camp_tests --output-on-failure`), then the full `node .claude/scripts/build.mjs --preset windows-msvc-agent` once at the final HEAD.
1. **T1** — extend the test (red), then the view change (green). Done: `camp_tests` passes; build.mjs → 0 failing tests.

## 7. Tests
`camp_tests::CampViewShowsThemeVictory`, boot A (theme-cleared): with `text` the captured camp text, `text.find("Descend deeper: ")`, `text.find("Return in triumph")` and `text.find("EXPEDITION CHOICE")` are all smaller than `text.find("REST OR REINFORCE")` and `text.find("RUNEWORDS")`; "Return in triumph" and "Descend deeper: " each occur exactly once; the text contains "The checkpoint stays." Boot B (other camp): `text.find("Descend to the next dungeon")` is greater than `text.find("LOADOUT (free)")`, and the text contains neither "EXPEDITION CHOICE" nor "Return in triumph". Existing assertions of the case stay, except the one on `"The checkpoint stays. Seals +"`, which becomes `"The checkpoint stays."` (the seals part depends on meta progress).

## 8. Decisions
- Resolved: the choice moves up only at theme-cleared camps — at other camps descending is the last step after using the services, which is why it sits at the bottom.
- Out of scope: a test that returning grants seals on a meta-enabled boot (review should-fix 1; the grant function itself is tested), the path strip's one-frame clipping on a new dungeon.

## 9. Pitfalls
- ImGui math operators are disabled: build `ImVec2` component-wise. Player-facing strings are English.
- A linker error about a corrupt `game01P_runtime.lib`: delete that `.lib` under `out/build/windows-msvc-agent/Debug/` and rebuild.

## 10. Open questions
None.
