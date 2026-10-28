#!/usr/bin/env bash
#
# Format (or check) the Spectra C/C++ sources with the repository's .clang-format.
#
# Usage: ./format_project.sh [options] [--] [file ...]
#   --check, --dry-run  Report files that need formatting; change nothing.
#                       Exits 1 when any file needs formatting.
#   --verbose           Print every file and the clang-format diagnostics.
#   --help              Show this help.
#
# Without file arguments the script processes the git-tracked C/C++ files of the
# repository (third_party/, _deps/, build*/, generated shaders and golden output
# are skipped). Untracked files such as local QA probes are never touched.
# With file arguments only those files are processed (no git filtering).
#
# clang-format version: CI uses clang-format 18, and other major versions format
# differently, so the script looks for clang-format-18 first, then a Homebrew
# llvm@18, then plain clang-format, and refuses any other major version.
#   CLANG_FORMAT=/path/to/clang-format        use this binary
#   SPECTRA_FORMAT_ALLOW_ANY_VERSION=1        only warn on a non-18 version
#
# Exit codes: 0 clean / all formatted, 1 files need formatting (check) or could
# not be formatted, 2 usage or environment error (tool missing, wrong version).
#
# Works with bash 3.2+ (macOS), Linux, Git Bash and WSL. Paths with spaces are
# handled (NUL-separated file lists), and tool output is stripped of CR.

REQUIRED_MAJOR=18

CHECK_ONLY=false
VERBOSE=false
FILES=()

usage()
{
    sed -n '3,26p' "$0" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
    case "$1" in
        --check | --dry-run)
            CHECK_ONLY=true
            ;;
        --verbose)
            VERBOSE=true
            ;;
        --help | -h)
            usage
            exit 0
            ;;
        --)
            shift
            while [ $# -gt 0 ]; do
                FILES+=("$1")
                shift
            done
            break
            ;;
        -*)
            echo "error: unknown option: $1 (see --help)" >&2
            exit 2
            ;;
        *)
            FILES+=("$1")
            ;;
    esac
    shift
done

# --- Locate clang-format ----------------------------------------------------

find_clang_format()
{
    if [ -n "${CLANG_FORMAT:-}" ]; then
        if command -v "$CLANG_FORMAT" > /dev/null 2>&1; then
            command -v "$CLANG_FORMAT"
            return 0
        fi
        echo "error: CLANG_FORMAT='$CLANG_FORMAT' is not an executable" >&2
        return 1
    fi
    local candidate
    for candidate in clang-format-18 clang-format-18.exe \
        /opt/homebrew/opt/llvm@18/bin/clang-format \
        /usr/local/opt/llvm@18/bin/clang-format \
        clang-format clang-format.exe; do
        if command -v "$candidate" > /dev/null 2>&1; then
            command -v "$candidate"
            return 0
        fi
    done
    echo "error: clang-format not found." >&2
    echo "       Install clang-format ${REQUIRED_MAJOR} (Ubuntu: apt install clang-format-18," >&2
    echo "       macOS: brew install llvm@18, Windows: LLVM 18 installer) or set CLANG_FORMAT." >&2
    return 1
}

CF="$(find_clang_format)" || exit 2

CF_VERSION_LINE="$("$CF" --version 2> /dev/null | tr -d '\r' | head -n 1)"
CF_MAJOR="$(printf '%s\n' "$CF_VERSION_LINE" \
    | sed -n 's/.*clang-format version \([0-9][0-9]*\).*/\1/p')"
if [ -z "$CF_MAJOR" ]; then
    echo "error: could not determine the version of '$CF' (got: '$CF_VERSION_LINE')" >&2
    exit 2
fi
if [ "$CF_MAJOR" != "$REQUIRED_MAJOR" ]; then
    if [ "${SPECTRA_FORMAT_ALLOW_ANY_VERSION:-0}" = "1" ]; then
        echo "warning: $CF is clang-format $CF_MAJOR; CI uses clang-format $REQUIRED_MAJOR," >&2
        echo "         results may differ from CI." >&2
    else
        echo "error: $CF is clang-format $CF_MAJOR, but Spectra is formatted with" >&2
        echo "       clang-format $REQUIRED_MAJOR (as in CI). Install clang-format-18 or point" >&2
        echo "       CLANG_FORMAT at it; SPECTRA_FORMAT_ALLOW_ANY_VERSION=1 downgrades this" >&2
        echo "       to a warning." >&2
        exit 2
    fi
fi

# --- Collect files ----------------------------------------------------------

LIST_FILE="$(mktemp "${TMPDIR:-/tmp}/spectra-format.XXXXXX")" || exit 2
DIAG_FILE="$(mktemp "${TMPDIR:-/tmp}/spectra-format-diag.XXXXXX")" || exit 2
trap 'rm -f "$LIST_FILE" "$DIAG_FILE"' EXIT

if [ ${#FILES[@]} -gt 0 ]; then
    : > "$LIST_FILE"
    for f in "${FILES[@]}"; do
        printf '%s\0' "$f" >> "$LIST_FILE"
    done
else
    SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
    ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel 2> /dev/null | tr -d '\r')"
    if [ -z "$ROOT" ]; then
        echo "error: $SCRIPT_DIR is not inside a git checkout; pass files explicitly." >&2
        exit 2
    fi
    cd "$ROOT" || exit 2
    git ls-files -z -- \
        '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx' '*.inl' '*.m' '*.mm' \
        > "$LIST_FILE" || {
        echo "error: git ls-files failed" >&2
        exit 2
    }
fi

is_excluded()
{
    case "$1" in
        third_party/* | */third_party/* | _deps/* | */_deps/* | build/* | build[-_]*/* \
            | src/gpu/shaders/* | tests/golden/baseline/* | tests/golden/output/*)
            return 0
            ;;
    esac
    return 1
}

# --- Run --------------------------------------------------------------------

echo "=== Spectra Project Formatter ==="
echo "clang-format: $CF ($CF_VERSION_LINE)"
if [ "$CHECK_ONLY" = true ]; then
    echo "Mode: check (no files are modified)"
else
    echo "Mode: format in place"
fi
echo

PROCESSED=0
NEEDS_FORMAT=0
REFORMATTED=0
ERRORS=0
MISSING=0

# Read from a redirect, not a pipeline, so the counters survive the loop.
while IFS= read -r -d '' file; do
    if [ ${#FILES[@]} -eq 0 ] && is_excluded "$file"; then
        continue
    fi
    if [ ! -f "$file" ]; then
        # Tracked but deleted in the working tree, or a bad argument.
        if [ ${#FILES[@]} -gt 0 ]; then
            echo "error: no such file: $file" >&2
            ERRORS=$((ERRORS + 1))
        else
            MISSING=$((MISSING + 1))
        fi
        continue
    fi
    PROCESSED=$((PROCESSED + 1))
    if [ "$VERBOSE" = true ]; then
        echo "Checking: $file"
    fi

    "$CF" --dry-run --Werror "$file" > /dev/null 2> "$DIAG_FILE"
    rc=$?
    if [ $rc -eq 0 ]; then
        continue
    fi
    if ! grep -q 'clang-format-violations' "$DIAG_FILE"; then
        echo "error: clang-format failed on $file:" >&2
        tr -d '\r' < "$DIAG_FILE" | sed 's/^/    /' >&2
        ERRORS=$((ERRORS + 1))
        continue
    fi

    NEEDS_FORMAT=$((NEEDS_FORMAT + 1))
    if [ "$CHECK_ONLY" = true ]; then
        echo "needs formatting: $file"
        if [ "$VERBOSE" = true ]; then
            tr -d '\r' < "$DIAG_FILE" | sed 's/^/    /'
        fi
    else
        if "$CF" -i "$file" 2> "$DIAG_FILE"; then
            echo "formatted: $file"
            REFORMATTED=$((REFORMATTED + 1))
        else
            echo "error: clang-format -i failed on $file:" >&2
            tr -d '\r' < "$DIAG_FILE" | sed 's/^/    /' >&2
            ERRORS=$((ERRORS + 1))
        fi
    fi
done < "$LIST_FILE"

echo
echo "=== Summary ==="
echo "Files checked:          $PROCESSED"
if [ "$CHECK_ONLY" = true ]; then
    echo "Files needing format:   $NEEDS_FORMAT"
else
    echo "Files reformatted:      $REFORMATTED"
fi
echo "Errors:                 $ERRORS"
if [ $MISSING -gt 0 ]; then
    echo "Skipped (deleted):      $MISSING"
fi

if [ $PROCESSED -eq 0 ] && [ $ERRORS -eq 0 ]; then
    echo "error: no files were checked" >&2
    exit 2
fi
if [ $ERRORS -gt 0 ]; then
    echo "FAILED: $ERRORS file(s) could not be processed" >&2
    exit 1
fi
if [ "$CHECK_ONLY" = true ] && [ $NEEDS_FORMAT -gt 0 ]; then
    echo "FAILED: $NEEDS_FORMAT file(s) need formatting. Run 'make format' to fix." >&2
    exit 1
fi
echo "OK"
exit 0
