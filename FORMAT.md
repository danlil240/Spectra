# Code Formatting

This project uses `clang-format` to maintain consistent code style across all C++ source files.

## Configuration

The formatting rules are defined in `.clang-format` at the project root. The configuration is based on Google style with customizations for the Plotix project:

- **C++20 standard**
- **4-space indentation** (Allman brace style)
- **100-character column limit**
- **Left-aligned pointers/references**
- **Template declarations always break**
- **Sorted includes**

## Usage

### Quick Format All Files
```bash
# Format all C++ files in the project
./format_project.sh

# Or using the Makefile
make -f Makefile.format format
```

### Check Formatting (Without Modifying)
```bash
# Check if files are properly formatted
./format_project.sh --check

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
clang-format -i path/to/file.cpp

# Check a single file
clang-format --dry-run --Werror path/to/file.cpp
```

## What Gets Formatted

The script formats all `.cpp`, `.hpp`, and `.h` files in the project, excluding:

- Build directories (`build*`)
- Git directory (`.git`)
- Third-party libraries (`third_party`)
- Generated shader files (`src/gpu/shaders`)
- Golden test baselines (`tests/golden/baseline`, `tests/golden/output`)

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
./format_project.sh --check
```

## Troubleshooting

If clang-format reports errors:

1. Ensure you have clang-format 14+ installed
2. Check that `.clang-format` exists and is valid
3. Verify the file isn't in an excluded directory
4. Run with `--verbose` to see detailed progress
