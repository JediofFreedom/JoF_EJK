#!/usr/bin/env bash
#
# rend2-sync.sh — port new rend2 commits from SomaZ/OpenJK (rend2)
# into this repo.
#
# This repo uses upstream's MP renderer in codemp/rd-rend2, with local
# customizations. The separate rend2-unified-wip branch has different APIs
# and SP/MP state; remapping its shared/rd-rend2 paths does not make it
# compatible. Only import codemp/rd-rend2 from the matching rend2 branch.
# The initial rend2 baseline is 1dd147b0f: its renderer was imported by local
# commit 3605a226a, with only the build include path and font/ratio additions.
#
# State: scripts/rend2-sync/last-synced-commit holds the last upstream commit
# that was processed. Commits are applied oldest-first; the script stops at
# the first conflict so later commits are never applied out of order. Fix the
# conflicting commit by hand, write its hash into the state file, commit, and
# re-run to continue.
#
# Usage: scripts/rend2-sync/rend2-sync.sh
#   UPSTREAM_URL / UPSTREAM_BRANCH env vars override the default upstream.
#   Requires a clean working tree. Creates commits on the CURRENT branch.
#
set -euo pipefail

UPSTREAM_URL="${UPSTREAM_URL:-https://github.com/SomaZ/OpenJK.git}"
UPSTREAM_BRANCH="${UPSTREAM_BRANCH:-rend2}"
STATE_FILE="scripts/rend2-sync/last-synced-commit"
SUMMARY_FILE="${SUMMARY_FILE:-}"

cd "$(git rev-parse --show-toplevel)"

if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "error: working tree not clean" >&2
    exit 1
fi

echo "Fetching $UPSTREAM_URL $UPSTREAM_BRANCH ..."
git fetch --quiet "$UPSTREAM_URL" "$UPSTREAM_BRANCH"
UPSTREAM_HEAD=$(git rev-parse FETCH_HEAD)

LAST=$(tr -d ' \r\n' < "$STATE_FILE")
if [ -z "$LAST" ]; then
    echo "error: $STATE_FILE is empty; seed it with an upstream commit hash" >&2
    exit 1
fi

if ! git merge-base --is-ancestor "$LAST" "$UPSTREAM_HEAD"; then
    echo "error: state commit $LAST is not an ancestor of upstream HEAD." >&2
    echo "Upstream branch was likely rebased; re-baseline $STATE_FILE by hand." >&2
    exit 1
fi

# Follow the branch's integration order. Diff merge commits against their
# first parent so fixes merged from other branches are imported exactly once.
COMMITS=$(git rev-list --first-parent --reverse "$LAST..$UPSTREAM_HEAD" -- codemp/rd-rend2)

if [ -z "$COMMITS" ]; then
    echo "Up to date with upstream ($UPSTREAM_HEAD); nothing to do."
fi

applied=()
already_present=()
conflict=""
new_state="$LAST"

for c in $COMMITS; do
    subject=$(git log -1 --format='%h %s' "$c")

    patch=$(git diff --binary "$c^" "$c" -- codemp/rd-rend2)

    # A here-string avoids SIGPIPE/pipefail false failures when git apply
    # exits before reading the entire patch.
    if apply_log=$(git apply --3way --binary <<< "$patch" 2>&1); then
        # A three-way merge can succeed without changing anything when the
        # upstream fix has already been ported manually.
        if git diff --cached --quiet; then
            echo "SKIP (present)  $subject"
            already_present+=("$subject")
            new_state="$c"
            continue
        fi
        git add -A codemp/rd-rend2
        git commit --quiet \
            --author "$(git log -1 --format='%an <%ae>' "$c")" \
            -m "$(git log -1 --format=%B "$c")" \
            -m "(ported from SomaZ/OpenJK $c)"
        echo "APPLIED         $subject"
        applied+=("$subject")
        new_state="$c"
    else
        git reset --hard --quiet HEAD
        # If the patch reverse-applies, the tree already contains this
        # commit's end state — skip it instead of reporting a conflict.
        if git apply --reverse --check --binary <<< "$patch" 2>/dev/null; then
            echo "SKIP (present)  $subject"
            already_present+=("$subject")
            new_state="$c"
            continue
        fi
        echo "CONFLICT        $subject"
        printf '%s\n' "$apply_log" >&2
        conflict="$subject"
        break
    fi
done

if [ "$new_state" != "$LAST" ]; then
    echo "$new_state" > "$STATE_FILE"
    git add "$STATE_FILE"
    git commit --quiet -m "rend2-sync: advance upstream state to ${new_state:0:8}"
fi

remaining=0
if [ -n "$conflict" ]; then
    remaining=$(git rev-list --first-parent --count "$new_state..$UPSTREAM_HEAD" -- codemp/rd-rend2)
fi

echo
echo "=== rend2-sync summary ==="
echo "applied:  ${#applied[@]}"
echo "present:  ${#already_present[@]}"
if [ -n "$conflict" ]; then
    echo "stopped at conflict: $conflict ($remaining upstream commit(s) still pending)"
    echo "port it by hand, set $STATE_FILE to its full hash, commit, re-run."
fi

if [ -n "$SUMMARY_FILE" ]; then
    {
        echo "Automated port of rend2 commits from [SomaZ/OpenJK \`$UPSTREAM_BRANCH\`](https://github.com/SomaZ/OpenJK/tree/$UPSTREAM_BRANCH)."
        echo "Only \`codemp/rd-rend2\` changes are imported; local renderer customizations are preserved."
        echo
        if [ ${#applied[@]} -gt 0 ]; then
            echo "### Applied"
            for s in "${applied[@]}"; do echo "- $s"; done
            echo
        fi
        if [ ${#already_present[@]} -gt 0 ]; then
            echo "### Skipped (already present)"
            for s in "${already_present[@]}"; do echo "- $s"; done
            echo
        fi
        if [ -n "$conflict" ]; then
            echo "### :warning: Stopped at conflict"
            echo "- $conflict"
            echo
            echo "$remaining upstream commit(s) remain, including this one. Port it manually,"
            echo "update \`$STATE_FILE\` to its full hash, commit, and re-run the workflow."
        fi
    } > "$SUMMARY_FILE"
fi

if [ -n "${GITHUB_OUTPUT:-}" ]; then
    {
        echo "applied=${#applied[@]}"
        echo "conflict=$conflict"
    } >> "$GITHUB_OUTPUT"
fi
