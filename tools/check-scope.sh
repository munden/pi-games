#!/bin/sh
# Pull-request scope rule (AGENTS.md section 10): a PR may change exactly one game directory and nothing else.
# Reads changed file names on stdin:   git diff --name-only origin/main...HEAD | tools/check-scope.sh
# Maintainer PRs are exempt (CI skips this check for the repository owner).
games=""; other=""
while IFS= read -r f; do
    [ -n "$f" ] || continue
    case "$f" in
        games/_template/*) other="$other $f";;
        games/*/*) id=${f#games/}; id=${id%%/*}; case " $games " in *" $id "*) ;; *) games="$games $id";; esac;;
        *) other="$other $f";;
    esac
done
games=$(echo $games); n=$(echo $games | wc -w | tr -d ' ')
if [ "$n" -eq 0 ]; then echo "scope: no game changes (maintainer-owned files only)"; exit 0; fi
if [ "$n" -gt 1 ]; then echo "scope: this PR changes $n games ($games) - one game per PR"; exit 1; fi
if [ -n "$other" ]; then echo "scope: a game PR must not touch anything outside games/$games/ - found:$other"; exit 1; fi
echo "scope: OK (games/$games/ only)"
