# Development Workflow

## Branch Model

`main` is the stable baseline. It should build, run, and stay easy to pull from
both Windows and macOS.

Use short-lived branches for all work:

- `feature/<area>-<slug>` for player-visible or runtime behavior.
- `fix/<area>-<slug>` for bug fixes.
- `chore/<area>-<slug>` for tooling, docs, config, and cleanup.
- `spike/<question>` for throwaway experiments. Do not merge spike branches
  directly; turn useful findings into a fresh feature/fix branch.

Default flow:

```bash
git switch main
git pull --ff-only
git switch -c feature/<area>-<slug>
```

Keep branches small enough to review in one sitting. Prefer a vertical game
slice over a layer-only mega branch: design/spec, data/config, components,
systems, debug UI, and verification should land together only when they are
small and coherent.

## Windows And macOS

Home Windows is the authoritative build and run environment. macOS is fine for
editing, design work, and lightweight checks.

Before switching machines:

```bash
git status --short
git add <changed files>
git commit -m "<short message>"
git push -u origin <branch>
```

On the other machine:

```bash
git fetch --all --prune
git switch <branch>
git pull --ff-only
```

Do not edit the same files independently in both checkouts. If you need to move
unfinished work, use a WIP commit on the branch and clean it up with squash
merge later.

## Verification Gates

Run the focused local check for the platform you are on:

```powershell
.\scripts\check-windows.ps1 -Configuration Debug
```

```bash
bash scripts/check-macos.sh Debug
```

Before merging meaningful runtime changes, verify on native Windows. From
macOS, use the remote Windows helper:

```bash
GAME01P_WIN_BRANCH="<branch>" GAME01P_WIN_HOST="<windows-host>" ./scripts/remote-windows-check.sh
```

Use Release checks before playtest builds, performance-sensitive changes, or
anything touching packaging/runtime dependencies.

## Merge Policy

Merge feature/fix/chore branches back to `main` only after:

- `git diff --check` passes.
- Platform-appropriate Debug build passes.
- Native Windows Debug build passes for runtime code.
- Any changed game code has been reviewed against the project ECS/factory/event
  boundaries.

Use squash merge as the default. Keep individual WIP commits useful while
moving between machines, but do not preserve noisy WIP history on `main`.

Recommended GitHub branch protection for `main`:

- Require pull request before merge.
- Require linear history.
- Require the Windows Build workflow once it is consistently green.
- Block force pushes.

## Releases And Assets

Do not add `develop` or full Git Flow yet. Create release branches only when
there is an actual playable candidate:

```bash
git switch -c release/playtest-YYYYMMDD main
```

Git LFS is not needed until binary assets are tracked. When `assets/` starts to
include source art, audio, fonts, or large exports such as `.png`, `.wav`,
`.aseprite`, `.psd`, `.fbx`, or `.blend`, add LFS rules in a dedicated chore
branch before committing those files.
