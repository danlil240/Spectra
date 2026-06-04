# Spectra — Conventions

## Formatting (`.clang-format`)

- Google base + C++20, **Allman braces**, 4-space indent, **100-column** limit
- Classes `PascalCase`; functions/variables `snake_case`; members trailing `_`; macros `UPPER_SNAKE`

## Architecture rules

- **No global state** — managers live on `App`, passed by pointer
- **RAII** — prefer stack; `std::unique_ptr` for ownership
- **Zero-copy data APIs** — `std::span<const float>`, `std::string_view` in public API
- Public headers in `include/spectra/` only; implementation in `src/`
- MATLAB-style series format strings: e.g. `"r--o"`

## Patterns

- **Builder:** `AnimationBuilder`, `AppConfig`
- **Strategy:** `Backend` / `VulkanBackend`
- **Registry:** commands, shortcuts, figures, series types, transforms, data sources
- **Composite:** App → Figure → Axes → Series
- **EventBus:** `include/spectra/event_bus.hpp`
- **Topics:** `include/spectra/topic.hpp` for live streaming

## Testing expectations

- New behavior → GTest in `tests/unit/` when non-trivial
- GPU-sensitive tests: CMake label `gpu` (excluded from sanitizer runs)
- Visual changes: golden baselines under `tests/golden/baseline/`

## Commits

Conventional commits: `feat|fix|refactor|test|docs|ci|perf|chore: description`

## Editing guidance (Serena)

- Prefer symbol tools for whole-method/class edits; `replace_content` for small intra-symbol patches
- After symbol API changes: `find_referencing_symbols` and update call sites
- Serena symbol line numbers are **0-based**