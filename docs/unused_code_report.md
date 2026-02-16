# Unused Code Report

Generated on: Sun Feb 15 09:40:43 PM IST 2026
Project Root: /home/daniel/projects/spectra

## Summary
- Unused Source Files: 0
- Unused Headers: 0
- Unused Targets: 0
- Unused Assets: 0
- Obsolete Code candidates: 0
- Structural Findings: 3

## Methodology
1. **Inventory**: Parsed `compile_commands.json`.
2. **Source Analysis**: filesystem vs compiled list.
3. **Header Analysis**: `#include` grep.
4. **Asset Analysis**: filename grep in source (checking for C++ var names for shaders).
5. **Obsolete Code**: regex for `#if 0`, `deprecated`, and manual structural analysis.

## 1. High-Impact Cleanup Candidates (Structural)
| Module/Symbol | Path | Confidence | Evidence |
|---------------|------|------------|----------|
| `Animator::add_timeline` | `src/anim/animator.cpp` | High | Method add_timeline is never called in the codebase. |
| `class Timeline` | `src/anim/timeline.cpp` | High | Timeline class is not used by the new animation system. |
| `Renderer::update_frame_ubo` | `src/render/renderer.cpp` | High | Unused member function. Rendering uses per-axes UBO updates. |

## 2. Unused Source Files
| File Path | Recommendation |
|-----------|----------------|
| *None* | |

## 3. Unused Assets
| File Path | Recommendation |
|-----------|----------------|
| *None* | |

## 4. Unused Targets
| Target | Recommendation |
|--------|----------------|
| *None* | |

## 5. Unused Headers
| File Path | Recommendation |
|-----------|----------------|
| *None* | |

## 6. Potential Obsolete Code Blocks
| File | Line | Type | Content |
|------|------|------|---------|
| *None* | | | |

## Next Actions
1. **Remove Animator/Timeline**: These are old animation classes superseded by `TimelineEditor`/`TransitionEngine`.
2. **Remove Unused Assets**: Delete the reported unused assets (if any remain).
3. **Clean Unused Targets**: Remove unused targets from CMakeLists.txt.
4. **Review Obsolete Blocks**: Check the TODOs and deprecated warnings.
