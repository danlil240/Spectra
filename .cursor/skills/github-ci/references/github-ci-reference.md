# GitHub CI — workflow reference

| File | Purpose |
|------|---------|
| `ci.yml` | Linux GCC+Clang, macOS, Windows, golden, sanitizers |
| `release.yml` | `v*` tags → packages + GitHub Release |
| `update-golden-baselines.yml` | Manual lavapipe baseline regen |
| `deploy-pages.yml` | GitHub Pages docs |

## Jobs (summary)

| Job | Notes |
|-----|-------|
| build-linux | ubuntu-24.04, gcc-13 / clang-17, `-Werror`, ctest `-L "!golden"` |
| build-macos | macos-15, golden OFF, ctest `-LE gpu` |
| build-windows | Vulkan SDK install, ctest `-LE gpu` |
| golden-tests | needs build-linux, lavapipe + `xvfb-run -a`, `-L golden -j1 --repeat until-pass:2` |
| sanitizers | address/undefined, `-LE gpu`, `SPECTRA_USE_ROS2=OFF` |

## CMake flags in CI

| Flag | CI note |
|------|---------|
| `SPECTRA_BUILD_GOLDEN_TESTS` | ON for golden/linux |
| `SPECTRA_BUILD_EXAMPLES` | Often OFF in golden/sanitizer/release |
| `SPECTRA_USE_ROS2` | ON only where ROS workspace exists |

## Lavapipe ICD pin (Linux GPU jobs)

```yaml
- name: Pin Vulkan ICD to lavapipe
  run: |
    set -e
    ICD_DIRS=(/usr/share/vulkan/icd.d /etc/vulkan/icd.d /usr/lib/x86_64-linux-gnu/vulkan/icd.d)
    LVP_ICD=""
    for d in "${ICD_DIRS[@]}"; do
      [ -d "$d" ] || continue
      for f in "$d"/lvp_icd*.json "$d"/*lavapipe*.json; do
        [ -f "$f" ] || continue
        LVP_ICD="$f"; break 2
      done
    done
    if [ -z "$LVP_ICD" ]; then
      for d in "${ICD_DIRS[@]}"; do
        [ -d "$d" ] || continue
        F_MATCH="$(grep -l 'libvulkan_lvp\.so' "$d"/*.json 2>/dev/null | head -n1 || true)"
        [ -n "$F_MATCH" ] && LVP_ICD="$F_MATCH" && break
      done
    fi
    if [ -n "$LVP_ICD" ]; then
      echo "VK_ICD_FILENAMES=$LVP_ICD" >> "$GITHUB_ENV"
      echo "VK_DRIVER_FILES=$LVP_ICD" >> "$GITHUB_ENV"
    else
      echo "::error::lavapipe ICD not found"; exit 1
    fi
```

Golden env also: `LIBGL_ALWAYS_SOFTWARE=1`, `SPECTRA_UPDATE_BASELINES=0`.
