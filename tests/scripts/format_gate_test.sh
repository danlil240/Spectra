#!/usr/bin/env bash
#
# Regression test for format_project.sh (the `make check-format` gate).
#   usage: format_gate_test.sh <path/to/format_project.sh> <path/to/.clang-format>
# Exits 77 (ctest SKIP_RETURN_CODE) when clang-format 18 is not available.

SCRIPT="$1"
STYLE="$2"
if [ ! -f "$SCRIPT" ] || [ ! -f "$STYLE" ]; then
    echo "usage: $0 <format_project.sh> <.clang-format>" >&2
    exit 2
fi

CF=""
for c in "${CLANG_FORMAT:-}" clang-format-18 clang-format; do
    [ -n "$c" ] || continue
    if command -v "$c" > /dev/null 2>&1 \
        && "$c" --version 2> /dev/null | grep -q 'clang-format version 18\.'; then
        CF="$(command -v "$c")"
        break
    fi
done
if [ -z "$CF" ]; then
    echo "SKIP: clang-format 18 not found"
    exit 77
fi
export CLANG_FORMAT="$CF"

WORK="$(mktemp -d "${TMPDIR:-/tmp}/spectra-format-test.XXXXXX")" || exit 2
trap 'rm -rf "$WORK"' EXIT
# A directory with a space exercises path quoting.
DIR="$WORK/dir with space"
mkdir -p "$DIR"
cp "$STYLE" "$DIR/.clang-format"

FAILS=0
fail()
{
    echo "FAIL: $*"
    FAILS=$((FAILS + 1))
}

printf 'int main()\n{\n    return 0;\n}\n' > "$DIR/clean.cpp"
printf 'int  main( ){return 0;}\n' > "$DIR/bad one.cpp"
printf 'int main()\r\n{\r\n    return 0;\r\n}\r\n' > "$DIR/crlf.cpp"

# 1. Clean files (LF and CRLF) pass the check.
out="$(bash "$SCRIPT" --check "$DIR/clean.cpp" "$DIR/crlf.cpp" 2>&1)"
rc=$?
[ $rc -eq 0 ] || fail "check on clean files returned $rc: $out"

# 2. A misformatted file makes the check fail and is counted.
out="$(bash "$SCRIPT" --check "$DIR/clean.cpp" "$DIR/bad one.cpp" 2>&1)"
rc=$?
[ $rc -eq 1 ] || fail "check on misformatted file returned $rc (expected 1)"
echo "$out" | grep -q 'Files checked: *2' || fail "wrong checked count: $out"
echo "$out" | grep -q 'Files needing format: *1' || fail "wrong needs-format count: $out"
echo "$out" | grep -q 'needs formatting: .*bad one.cpp' || fail "file not reported: $out"

# 3. Check mode must not modify the file.
grep -q 'int  main( ){return 0;}' "$DIR/bad one.cpp" || fail "check mode modified the file"

# 4. Format mode fixes it; a second check passes.
out="$(bash "$SCRIPT" "$DIR/bad one.cpp" 2>&1)"
rc=$?
[ $rc -eq 0 ] || fail "format mode returned $rc: $out"
echo "$out" | grep -q 'Files reformatted: *1' || fail "wrong reformatted count: $out"
bash "$SCRIPT" --check "$DIR/bad one.cpp" > /dev/null 2>&1 || fail "file still misformatted"

# 5. A missing clang-format is an error, not a silent pass.
out="$(CLANG_FORMAT="$WORK/no-such-clang-format" bash "$SCRIPT" --check "$DIR/clean.cpp" 2>&1)"
rc=$?
[ $rc -eq 2 ] || fail "missing clang-format returned $rc (expected 2): $out"

# 6. A non-18 clang-format is refused unless explicitly allowed.
FAKE="$WORK/fake-clang-format"
printf '#!/bin/sh\necho "clang-format version 21.1.0"\n' > "$FAKE"
chmod +x "$FAKE"
out="$(CLANG_FORMAT="$FAKE" bash "$SCRIPT" --check "$DIR/clean.cpp" 2>&1)"
rc=$?
[ $rc -eq 2 ] || fail "clang-format 21 returned $rc (expected 2): $out"

# 7. A nonexistent file argument is an error.
bash "$SCRIPT" --check "$DIR/missing.cpp" > /dev/null 2>&1
rc=$?
[ $rc -ne 0 ] || fail "nonexistent file returned 0"

if [ $FAILS -ne 0 ]; then
    echo "$FAILS check(s) failed"
    exit 1
fi
echo "format_project.sh gate: all checks passed"
exit 0
