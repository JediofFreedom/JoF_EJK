#!/usr/bin/env bash
# Run with: bash tests/rend2-sync/check.sh
# Exercises the actual sync script against disposable local Git repositories.
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../.." && pwd)
sync_script="$repo_root/scripts/rend2-sync/rend2-sync.sh"
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/rend2-sync-test.XXXXXX")
test_dir=$(cd "$test_dir" && pwd)
cleanup() {
    # Only remove the absolute temporary directory created above.
    case "$test_dir" in
        /*/rend2-sync-test.??????) rm -rf -- "$test_dir" ;;
        *) printf 'Unexpected test directory: %s\n' "$test_dir" >&2 ;;
    esac
}
trap 'if [ "$?" -eq 0 ]; then cleanup; else echo "Test data: $test_dir" >&2; fi' EXIT

fail() {
    echo "FAIL: $*" >&2
    if [ -f "${case_dir:-}/log" ]; then cat "$case_dir/log" >&2; fi
    exit 1
}
expect_line() { grep -Fx -- "$2" "$1" >/dev/null || fail "$1: missing $2"; }
commit() {
    git -C "$1" add -A
    git -C "$1" -c commit.gpgSign=false commit --quiet -m "$2"
}
configure() {
    git -C "$1" config user.name 'Sync Test'
    git -C "$1" config user.email 'sync-test@example.invalid'
    git -C "$1" config core.autocrlf false
    git -C "$1" config commit.gpgSign false
}
setup() {
    case_dir="$test_dir/$1"
    upstream="$case_dir/upstream"
    downstream="$case_dir/downstream"
    mkdir -p "$upstream/codemp/rd-rend2" "$upstream/shared/rd-rend2"
    git -C "$upstream" init --quiet --initial-branch=rend2
    configure "$upstream"
    echo original > "$upstream/codemp/rd-rend2/renderer.txt"
    echo unrelated > "$upstream/shared/rd-rend2/ignored.txt"
    commit "$upstream" baseline
    baseline=$(git -C "$upstream" rev-parse HEAD)
    git -c core.autocrlf=false clone --quiet "$upstream" "$downstream"
    configure "$downstream"
    mkdir -p "$downstream/scripts/rend2-sync"
    # Windows checkouts can contain CRLF in this state file.
    printf '%s\r\n' "$baseline" > "$downstream/scripts/rend2-sync/last-synced-commit"
    commit "$downstream" 'Seed sync state'
}
run_sync() {
    : > "$case_dir/outputs"
    (
        cd "$downstream"
        UPSTREAM_URL="$upstream" UPSTREAM_BRANCH=rend2 \
            SUMMARY_FILE="$case_dir/summary.md" GITHUB_OUTPUT="$case_dir/outputs" \
            bash "$sync_script"
    ) > "$case_dir/log" 2>&1 || { cat "$case_dir/log"; return 1; }
}
expect_state() {
    local actual
    actual=$(tr -d '\r\n' < "$downstream/scripts/rend2-sync/last-synced-commit")
    [ "$actual" = "$1" ] || fail "state: expected $1, got $actual"
    [ -z "$(git -C "$downstream" status --porcelain)" ] || fail 'dirty tree after sync'
}

setup large_patch
awk 'BEGIN { for (i=0; i<10000; i++) print "renderer change " i }' \
    > "$upstream/codemp/rd-rend2/large.txt"
echo ignored_change > "$upstream/shared/rd-rend2/ignored.txt"
commit "$upstream" 'Large renderer patch with unrelated changes'
expected=$(git -C "$upstream" rev-parse HEAD)
run_sync
cmp "$upstream/codemp/rd-rend2/large.txt" "$downstream/codemp/rd-rend2/large.txt"
expect_line "$downstream/shared/rd-rend2/ignored.txt" unrelated
expect_line "$case_dir/outputs" applied=1
expect_line "$case_dir/outputs" conflict=
expect_state "$expected"
head_before=$(git -C "$downstream" rev-parse HEAD)
run_sync
[ "$(git -C "$downstream" rev-parse HEAD)" = "$head_before" ] || fail 'no-op made commits'
expect_line "$case_dir/outputs" applied=0
expect_line "$case_dir/outputs" conflict=
[ -s "$case_dir/summary.md" ] || fail 'no-op did not write summary'
echo 'PASS: large patch, path filtering, CRLF state, no-op rerun'

setup already_present
echo fixed > "$upstream/codemp/rd-rend2/renderer.txt"
commit "$upstream" 'Upstream fix'
expected=$(git -C "$upstream" rev-parse HEAD)
echo fixed > "$downstream/codemp/rd-rend2/renderer.txt"
commit "$downstream" 'Manual port'
run_sync
expect_line "$case_dir/outputs" applied=0
expect_line "$case_dir/outputs" conflict=
expect_state "$expected"
grep -F 'SKIP (present)' "$case_dir/log" >/dev/null || fail 'manual port not recognized'
echo 'PASS: already-present three-way merge advances state without empty commit'

setup conflict
echo upstream > "$upstream/codemp/rd-rend2/renderer.txt"
commit "$upstream" 'Conflicting fix'
echo later > "$upstream/codemp/rd-rend2/later.txt"
commit "$upstream" 'Must not import past conflict'
echo local > "$downstream/codemp/rd-rend2/renderer.txt"
commit "$downstream" 'Local customization'
run_sync
expect_state "$baseline"
expect_line "$downstream/codemp/rd-rend2/renderer.txt" local
[ ! -e "$downstream/codemp/rd-rend2/later.txt" ] || fail 'imported past conflict'
expect_line "$case_dir/outputs" applied=0
grep -E '^conflict=.+Conflicting fix$' "$case_dir/outputs" >/dev/null || fail 'missing conflict output'
grep -F 'renderer.txt' "$case_dir/log" >/dev/null || fail 'missing conflict details'
echo 'PASS: conflict preserves local code and state and stops later commits'

setup partial_progress
echo safe > "$upstream/codemp/rd-rend2/safe.txt"
commit "$upstream" 'Safe fix'
expected=$(git -C "$upstream" rev-parse HEAD)
echo upstream > "$upstream/codemp/rd-rend2/renderer.txt"
commit "$upstream" 'Conflicting fix'
echo local > "$downstream/codemp/rd-rend2/renderer.txt"
commit "$downstream" 'Local customization'
run_sync
expect_state "$expected"
expect_line "$downstream/codemp/rd-rend2/safe.txt" safe
expect_line "$downstream/codemp/rd-rend2/renderer.txt" local
expect_line "$case_dir/outputs" applied=1
grep -F 'including this one' "$case_dir/summary.md" >/dev/null || fail 'missing partial summary'
echo 'PASS: successful imports retained before a later conflict'

setup merged_fix
git -C "$upstream" checkout --quiet -b fix
echo merged > "$upstream/codemp/rd-rend2/renderer.txt"
commit "$upstream" 'Fix on a side branch'
git -C "$upstream" checkout --quiet rend2
git -C "$upstream" merge --quiet --no-ff fix -m 'Merge renderer fix'
expected=$(git -C "$upstream" rev-parse HEAD)
run_sync
expect_state "$expected"
expect_line "$downstream/codemp/rd-rend2/renderer.txt" merged
expect_line "$case_dir/outputs" applied=1
git -C "$downstream" log --format=%B -2 | grep -F "$expected" >/dev/null || fail 'merge not attributed'
echo 'PASS: merged renderer fixes imported once against first parent'

setup invalid_state
echo 0000000000000000000000000000000000000000 > "$downstream/scripts/rend2-sync/last-synced-commit"
commit "$downstream" 'Invalid baseline'
head_before=$(git -C "$downstream" rev-parse HEAD)
if run_sync; then fail 'invalid baseline accepted'; fi
[ "$(git -C "$downstream" rev-parse HEAD)" = "$head_before" ] || fail 'invalid baseline changed HEAD'
echo 'PASS: invalid baseline rejected without changes'
