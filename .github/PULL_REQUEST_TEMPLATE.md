## Summary

<!-- What does this PR change, and why? Link related issues with "Fixes #123". -->

## Type of change

- [ ] Bug fix
- [ ] New feature
- [ ] Refactor / performance (no behaviour change)
- [ ] Documentation
- [ ] Build / CI / packaging

## How was this tested?

<!-- Commands you ran and their results. -->

- [ ] `ctest --test-dir build -LE gpu --output-on-failure` passes
- [ ] GPU / golden tests run (`xvfb-run -a ctest --test-dir build -L gpu -j1`) if rendering changed
- [ ] Python tests (`cd python && pytest tests/`) if the Python package changed

## Checklist

- [ ] Commits follow [Conventional Commits](https://www.conventionalcommits.org/) (`feat:`, `fix:`, `docs:` ...)
- [ ] Changed C++ files are formatted with `clang-format`
- [ ] New behaviour has unit tests in `tests/unit/` (GPU-dependent tests labelled `gpu`)
- [ ] Golden baselines updated and the image changes explained below (if rendering changed)
- [ ] Public API changes in `include/spectra/` are documented
- [ ] `CHANGELOG.md` has an entry under **Unreleased** for user-visible changes

## Screenshots / notes for reviewers

<!-- Optional: before/after images, follow-ups, known limitations. -->
