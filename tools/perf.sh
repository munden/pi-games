#!/bin/sh
# usage: tools/perf.sh BUILD_DIR FRAMES game...
# Headless benchmark table with the AGENTS.md section 5 budgets. Numbers are for THIS machine; the Pi Zero 2 W runs
# update+cpu3d roughly 8x slower (estimate - confirm on the device with `pg run <id> --fps`).
build=$1; frames=$2; shift 2
printf '%-16s %9s %9s %9s %9s %9s  %s\n' GAME update_ms cpu3d_ms tris_avg tris_max pi_est_ms status
rc=0
for g in "$@"; do
    t=tools/smoke.touch; [ -f "games/$g/test.touch" ] && t="games/$g/test.touch"
    line=$("$build/games/$g/$g" --headless --frames "$frames" --touch-script "$t" --seed 1 2>&1 | grep 'done:')
    upd=$(echo "$line" | sed -n 's/.*update=\([0-9.]*\)ms.*/\1/p'); c3=$(echo "$line" | sed -n 's/.*cpu3d=\([0-9.]*\)ms.*/\1/p')
    ta=$(echo "$line" | sed -n 's/.*tris_avg=\([0-9]*\).*/\1/p'); tm=$(echo "$line" | sed -n 's/.*tris_max=\([0-9]*\).*/\1/p')
    if [ -z "$upd" ]; then printf '%-16s  FAILED TO RUN\n' "$g"; rc=1; continue; fi
    est=$(awk -v u="$upd" -v c="$c3" 'BEGIN { printf "%.1f", (u + c) * 8 }')
    st=ok
    awk -v e="$est" 'BEGIN { exit !(e > 12.0) }' && st="OVER pi budget (~12 ms)"
    [ "$tm" -gt 10000 ] && { st="OVER triangle budget (10000)"; rc=1; }
    printf '%-16s %9s %9s %9s %9s %9s  %s\n' "$g" "$upd" "$c3" "$ta" "$tm" "$est" "$st"
done
exit $rc
