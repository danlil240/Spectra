# Code Formatting

This project uses **clang-format 18** (the version CI installs) to keep a consistent code style
across all C/C++ source files. Other major versions format differently, so `format_project.sh`
refuses them unless `SPECTRA_FORMAT_ALLOW_ANY_VERSION=1` is set.

## Configuration

The formatting rules are defined in `.clang-format` at the project root. The configuration is based on Google style with customizations for the Spectra project:

- **C++20 standard**
- **4-space indentation** (Allman brace style)
- **100-character column limit**
- **Left-aligned pointers/references**
- **Template declarations always break**
- **Sorted includes**

## Usage

### Format Only Your Changes

CI checks the whole tree (`make check-format`); the tree is clean, so formatting your
changed lines keeps it that way.
```bash
git clang-format-18 origin/main
```

### Quick Format All Files
```bash
# Format all git-tracked C/C++ files in the project
./format_project.sh      # or: make format

# Or using the Makefile
make -f Makefile.format format
```

### Check Formatting (Without Modifying)
```bash
# Check if files are properly formatted; exits 1 if any file needs formatting
./format_project.sh --check      # or: make check-format

# Check specific files only
./format_project.sh --check src/core/figure.cpp include/spectra/figure.hpp

# Or using the Makefile
make -f Makefile.format check
```

### Verbose Output
```bash
# Show detailed progress
./format_project.sh --verbose

# Check with verbose output
./format_project.sh --check --verbose
```

### Format Individual Files
```bash
# Format a single file
clang-format-18 -i path/to/file.cpp

# Check a single file
clang-format-18 --dry-run --Werror path/to/file.cpp
```

## What Gets Formatted

Without file arguments the script processes the **git-tracked** `.c`, `.cc`, `.cpp`, `.cxx`,
`.h`, `.hh`, `.hpp`, `.hxx`, `.inl`, `.m` and `.mm` files, excluding:

- Third-party code (`third_party/`, `_deps/`)
- Build directories (`build/`, `build-*/`, `build_*/`)
- Generated shader files (`src/gpu/shaders`)
- Golden test baselines (`tests/golden/baseline`, `tests/golden/output`)

Untracked files (local QA probes, scratch code) are never checked or modified.

## Choosing the clang-format binary

The script uses, in order: `$CLANG_FORMAT`, `clang-format-18`, Homebrew `llvm@18`
(`/opt/homebrew/opt/llvm@18/bin/clang-format` or `/usr/local/opt/llvm@18/bin/clang-format`),
then `clang-format` on `PATH`. It stops with exit code 2 if none is found or the major version
is not 18.

- Ubuntu/Debian: `sudo apt install clang-format-18`
- macOS: `brew install llvm@18`
- Windows: install LLVM 18 and run the script from Git Bash or WSL, e.g.
  `CLANG_FORMAT="/c/Program Files/LLVM/bin/clang-format.exe" ./format_project.sh --check`

Exit codes: `0` clean (or all files formatted), `1` files need formatting (check mode) or
could not be formatted, `2` usage or environment error.

## Integration with IDE

Most IDEs can be configured to use `.clang-format` automatically:

### VS Code
Add to `.vscode/settings.json`:
```json
{
    "editor.formatOnSave": true,
    "C_Cpp.clang_format_style": "file",
    "C_Cpp.clang_format_fallbackStyle": "Google"
}
```

### CLion
Settings → Editor → Code Style → C/C++ → Set from → .clang-format

## Pre-commit Hook (Optional)

To automatically format before commits:
```bash
# Add to .git/hooks/pre-commit
#!/bin/bash
git clang-format-18 --diff --staged || {
    echo "Run: git clang-format-18 --staged"
    exit 1
}
```

## Troubleshooting

If clang-format reports errors:

1. Ensure clang-format **18** is installed (`clang-format-18 --version`), or point `CLANG_FORMAT` at it
2. Check that `.clang-format` exists and is valid
3. Verify the file isn't in an excluded directory
4. Run with `--verbose` to see detailed progress
