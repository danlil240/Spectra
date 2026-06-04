# Spectra Cursor Skills

Project-scoped Agent Skills for Cursor. Start with **[spectra-index](spectra-index/SKILL.md)** to route to the right workflow.

| Category | Skills |
|----------|--------|
| Router | `spectra-index`, `spectra-mcp` |
| Build | `build-and-test`, `build-system` |
| Vulkan / shaders | `debug-vulkan`, `add-shader` |
| Features | `add-series-type`, `add-command`, `add-example`, `add-test` |
| Domains | `3d-rendering`, `data-pipeline`, `ipc-protocol-dev`, `python-bindings` |
| Quality | `code-simplifier`, `graphical-change-workflow` |
| QA agents | `qa-designer-agent`, `qa-performance-agent`, `qa-regression-agent`, `qa-api-agent`, `qa-accessibility-agent`, `qa-memory-agent`, `qa-ros-performance-agent` |

Legacy copies in `skills/` (Windsurf) and `.claude/skills/` remain for other tools; **Cursor should use this directory**.

Regenerate optimizations: `python3 scripts/optimize_cursor_skills.py`
