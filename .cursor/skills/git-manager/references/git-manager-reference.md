# Git Manager — extended workflows

## PR description template

```md
## Summary
- What changed? Why?

## Changes
- 

## Testing
- [ ] Built: `cmake --build build -j$(nproc)`
- [ ] Tests: `ctest --test-dir build -LE gpu --output-on-failure`
- [ ] Manual UI/rendering
- [ ] Not tested — reason:

## Notes
Known limitations / follow-ups.
```

## Release PR

```bash
git switch main && git fetch origin && git pull --rebase origin main
git switch -c release/<version>
printf "<version>\n" > version.txt
git add version.txt && git commit -m "chore: bump version to <version>"
git push -u origin HEAD
gh pr create --base main --title "chore: bump version to <version>"
```

After merge on main:

```bash
git switch main && git pull --rebase origin main
git tag -a v<version> -m "Release v<version>"
git push origin v<version>   # never bundle with branch push
```

## Hotfix

`fix/<slug>` from main → commit → PR → merge after CI.

## Changelog

```bash
git fetch --tags
git log --oneline $(git describe --tags --abbrev=0)..HEAD
```

Group by feat/fix/perf/refactor/ci/docs — only real commits.

## Merge conflicts

`git diff --name-only --diff-filter=U` → resolve → `git rebase --continue` or `git merge --continue`. Explain tradeoffs before `--ours`/`--theirs`.

## Squash / cherry-pick

Squash on PR branch only: `git rebase -i origin/main` → `git push --force-with-lease`.

Cherry-pick: new branch from main → `git cherry-pick <sha>` → PR.

## Updating existing PR

Changes on PR branch → logical commits → `git push`. Do not amend/squash pushed commits unless user asks.
