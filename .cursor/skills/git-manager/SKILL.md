---
name: git-manager
description: >-
  Spectra git and release workflows: conventional commits, branches, PRs, version
  bumps, release tags, merge conflicts, changelog. Use for commit, branch, PR, release,
  version.txt, squash merge, cherry-pick, or git status/diff questions.
---

# Git Manager (Spectra)

**Repo:** `danlil240/Spectra` · **default branch:** `main` (stable, demo-ready) · **version:** `version.txt` (semver) · **release:** push `v<version>` tag → `release.yml`

**Commits:** Conventional — `feat|fix|refactor|test|docs|ci|perf|chore: description` (≤72 chars subject)

## Policy

- **PR-first** — do not commit/push to `main` for normal work (exceptions: user explicitly asks, or trivial docs-only).
- **Never** force-push `main`, amend published commits, or `--no-verify` unless user asks.
- Before destructive ops: `git status` + `git stash list`.
- **Release:** version bump via PR on `release/<version>`; tag `main` only **after** merge; push tag separately: `git push origin v<version>`.

## Default interpretations

| User says | Do |
|-----------|-----|
| commit my changes | branch off main → commit → push → open PR |
| open a PR | commit if needed → push branch → `gh pr create` to main |
| bump version | `release/<ver>` → update `version.txt` → PR |
| release version X | merge bump PR → tag `vX` on main → push tag |
| merge the PR | check CI → `gh pr merge --squash --delete-branch` if safe |

## PR-first flow

```bash
git status && git diff --stat
git fetch origin && git pull --rebase origin main   # if on main, branch first
git switch -c <type>/<short-slug>                   # feat|fix|refactor|docs|ci|release
git add -p   # or specific files
git commit -m "<type>: <description>"
git push -u origin HEAD
gh pr create --base main --title "<type>: <description>" --body "$(cat <<'EOF'
## Summary
- What / why

## Testing
- [ ] `cmake --build build -j$(nproc)`
- [ ] `ctest --test-dir build -LE gpu --output-on-failure`
EOF
)"
```

**Branch names:** `<type>/<short-slug>` (e.g. `fix/lavapipe-icd-discovery`)

**Work committed on main locally:** `git switch -c <branch>` → push → PR → `git switch main && git reset --hard origin/main` (only if safe).

## Output

```text
Action:  <what>
Command: <exact>
Result:  PASS/FAIL or one line
Next:    <step or done>
```

Details: hotfix, squash, cherry-pick, changelog, conflict resolution → [references/git-manager-reference.md](references/git-manager-reference.md)
