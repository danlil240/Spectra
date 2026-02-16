#!/bin/bash

# Format all C++ source files in the Plotix project using .clang-format
# Usage: ./format_project.sh [options]
#   --check     : Check formatting without modifying files
#   --dry-run   : Same as --check
#   --verbose   : Show detailed output
#   --help      : Show this help

set -e  # Exit on any error

# Default options
CHECK_ONLY=false
VERBOSE=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --check|--dry-run)
            CHECK_ONLY=true
            shift
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        --help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  --check, --dry-run  Check formatting without modifying files"
            echo "  --verbose           Show detailed output"
            echo "  --help              Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

echo "=== Plotix Project Formatter ==="
if [ "$CHECK_ONLY" = true ]; then
    echo "Checking formatting (dry-run mode)..."
else
    echo "Formatting all C++ files with .clang-format..."
fi
echo

# Count total files
TOTAL_FILES=$(find . -type f \( \
    -name "*.cpp" -o \
    -name "*.hpp" -o \
    -name "*.h" \
    \) ! -path "./build*" \
    ! -path "./build_clang*" \
    ! -path "./.git/*" \
    ! -path "./third_party/*" \
    ! -path "./src/gpu/shaders/*" \
    ! -path "./tests/golden/baseline/*" \
    ! -path "./tests/golden/output/*" \
    | wc -l)

echo "Found $TOTAL_FILES C++ files to process"
echo

# Prepare clang-format options
if [ "$CHECK_ONLY" = true ]; then
    CLANG_OPTS="--dry-run --Werror"
    VERBOSITY="Checking"
else
    CLANG_OPTS="-i"
    VERBOSITY="Formatting"
fi

# Track results
PROCESSED=0
ERRORS=0

# Find all C++ source files and format them
find . -type f \( \
    -name "*.cpp" -o \
    -name "*.hpp" -o \
    -name "*.h" \
    \) ! -path "./build*" \
    ! -path "./build_clang*" \
    ! -path "./.git/*" \
    ! -path "./third_party/*" \
    ! -path "./src/gpu/shaders/*" \
    ! -path "./tests/golden/baseline/*" \
    ! -path "./tests/golden/output/*" \
    -print | while read -r file; do
    
    if [ "$VERBOSE" = true ] || [ "$PROCESSED" -eq 0 ]; then
        echo "$VERBOSITY: $file"
    elif [ $((PROCESSED % 10)) -eq 0 ]; then
        echo "$VERBOSITY: $file (processed $PROCESSED/$TOTAL_FILES)"
    fi
    
    if ! clang-format $CLANG_OPTS "$file" 2>/dev/null; then
        if [ "$CHECK_ONLY" = true ]; then
            echo "❌ Formatting issues found in: $file"
        else
            echo "ERROR: Failed to process $file"
        fi
        ERRORS=$((ERRORS + 1))
    fi
    
    PROCESSED=$((PROCESSED + 1))
done

echo
echo "=== Summary ==="
echo "Files processed: $PROCESSED"
echo "Errors: $ERRORS"

if [ "$CHECK_ONLY" = true ]; then
    if [ $ERRORS -eq 0 ]; then
        echo "✅ All files are properly formatted!"
    else
        echo "❌ $ERRORS files have formatting issues"
        echo "Run './format_project.sh' to fix formatting issues"
        exit 1
    fi
else
    if [ $ERRORS -eq 0 ]; then
        echo "✅ All files formatted successfully!"
    else
        echo "⚠️  $ERRORS files had formatting errors"
        echo "Check the output above for details"
    fi
fi

echo
echo "=== Done ==="
