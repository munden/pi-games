#!/bin/sh
# usage: tools/smoke.sh BUILD_DIR FRAMES game...
# Runs each game headless for FRAMES ticks with scripted touches. Fails on a non-zero exit,
# a FATAL, or an exceeded triangle budget. Output of the game is shown only on failure.
build=$1; frames=$2; shift 2
rc=0
for g in "$@"; do
    exe="$build/games/$g/$g"
    touch=tools/smoke.touch
    [ -f "games/$g/test.touch" ] && touch="games/$g/test.touch"
    out=$(mktemp)
    if ! "$exe" --headless --frames "$frames" --touch-script "$touch" --seed 1 >"$out" 2>&1; then
        echo "SMOKE FAILED (exit code): $g"; cat "$out"; rc=1
    elif grep -Eq 'FATAL|budget exceeded|runtime error|AddressSanitizer' "$out"; then
        echo "SMOKE FAILED (bad output): $g"; cat "$out"; rc=1
    elif tm=$(sed -n 's/.*tris_max=\([0-9]*\).*/\1/p' "$out"); [ -n "$tm" ] && [ "$tm" -gt 10000 ]; then
        echo "SMOKE FAILED (triangle budget: tris_max=$tm > 10000, AGENTS.md section 5): $g"; rc=1
    else
        echo "smoke ok: $g  $(grep -o 'update=[0-9.]*ms cpu3d=[0-9.]*ms tris_avg=[0-9]* tris_max=[0-9]*' "$out")"
    fi
    rm -f "$out"
done
exit $rc
