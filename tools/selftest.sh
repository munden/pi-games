#!/bin/sh
# Self-test of the repository tooling: manifest validation (positive + negative cases), channel rules,
# the PR scope checker, and the whole subscribe -> sync -> run -> prune flow of tools/pg on a throw-away repo.
# usage: tools/selftest.sh [BUILD_DIR]      (BUILD_DIR must contain tools/pgcat and sdk/; default: build)
cd "$(dirname "$0")/.." || exit 2
B=${1:-build}
PGCAT=$B/tools/pgcat
SANARG=""; case "$B" in *-san*) SANARG="SAN=1";; esac   # sanitized object files need sanitized links
[ -x "$PGCAT" ] || { echo "selftest: $PGCAT missing (make tools)"; exit 2; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fails=0
ok()  { echo "  ok: $1"; }
bad() { echo "  FAIL: $1"; fails=$((fails+1)); }

# a minimal valid BMP header (256x256, 24 bpp): pgcat only inspects the header
bmp() { printf 'BM\000\000\000\000\000\000\000\000\066\000\000\000\050\000\000\000\000\001\000\000\000\001\000\000\001\000\030\000' > "$1"; head -c 26 /dev/zero >> "$1"; }

mkgame() {   # mkgame ROOT ID
    d=$1/games/$2; mkdir -p "$d/src"
    cat > "$d/game.ini" <<INI
id = $2
name = Test $2
version = 1.0.0
author = Tester
license = MIT
description = A test game.
sdk_api = 1
players = 1-2
tags = test, demo
tested = desktop
INI
    echo 'int main(void){return 0;}' > "$d/src/main.c"; bmp "$d/icon.bmp"
}

expect_fail() {   # expect_fail NAME ROOT PATTERN
    out=$("$PGCAT" --root "$2" validate 2>&1); rc=$?
    if [ $rc -ne 0 ] && echo "$out" | grep -q "$3"; then ok "$1"; else bad "$1 (rc=$rc, wanted '$3', got: $out)"; fi
}

echo "manifest validation"
R=$T/v; mkdir -p $R/channels; mkgame $R good-one
out=$("$PGCAT" --root $R validate 2>&1) && ok "valid game passes" || bad "valid game rejected: $out"

for case in \
 "id-mismatch|s/^id = .*/id = other-id/|must equal the directory" \
 "missing-key|/^description/d|missing required key 'description'" \
 "unknown-key|\$a bogus = 1|unknown key 'bogus'" \
 "custom-key-ok-x|\$a x-foo = 1|__OK__" \
 "bad-license|s/^license = .*/license = Proprietary/|license 'Proprietary'" \
 "bad-version|s/^version = .*/version = 1.0/|version '1.0'" \
 "dup-key|\$a name = Again|duplicate key" \
 "bad-players|s/^players = .*/players = 9/|players '9'" \
 "bad-tested|s/^tested = .*/tested = yes/|tested must be" \
 "bad-sdk-api|s/^sdk_api = .*/sdk_api = 99/|sdk_api '99'" \
 "bad-tag|s/^tags = .*/tags = Bad Tag/|tag 'Bad Tag'" \
 "long-desc|s/^description = .*/description = $(printf 'x%.0s' $(seq 1 150))/|longer than 140" \
 "no-equals|\$a justtext|expected 'key = value'" ; do
    name=${case%%|*}; rest=${case#*|}; sedexpr=${rest%%|*}; want=${rest#*|}
    rm -rf $R/games/good-one; mkgame $R good-one
    sed -i "$sedexpr" $R/games/good-one/game.ini
    if [ "$want" = "__OK__" ]; then
        "$PGCAT" --root $R validate >/dev/null 2>&1 && ok "$name" || bad "$name should pass"
    else expect_fail "$name" $R "$want"; fi
done
rm -rf $R/games/good-one; mkgame $R good-one; bmp $R/games/good-one/icon.bmp; printf 'BM' | dd of=$R/games/good-one/icon.bmp bs=1 seek=18 conv=notrunc 2>/dev/null
rm -f $R/games/good-one/icon.bmp; expect_fail "missing-icon" $R "missing icon.bmp"
printf 'not a bmp at all, definitely not.....' > $R/games/good-one/icon.bmp; expect_fail "icon-not-bmp" $R "not a BMP"

echo "channels"
R=$T/c; mkdir -p $R/channels; mkgame $R aa; mkgame $R bb
printf '# comment\naa\n' > $R/channels/stable.txt
out=$("$PGCAT" --root $R validate 2>&1) && ok "channel file accepted" || bad "channel file rejected: $out"
[ "$("$PGCAT" --root $R ids @stable)" = "aa" ] && ok "@stable resolves to aa" || bad "@stable"
[ "$("$PGCAT" --root $R ids @incubating)" = "bb" ] && ok "@incubating is everything unlisted" || bad "@incubating"
printf 'nonexistent\n' > $R/channels/community.txt; expect_fail "channel lists unknown game" $R "does not exist"
printf 'aa\n' > $R/channels/community.txt; expect_fail "game in two channels" $R "more than one channel"
rm -f $R/channels/community.txt
"$PGCAT" --root $R ids @nope >/dev/null 2>&1 && bad "unknown channel accepted" || ok "unknown channel rejected"
"$PGCAT" --root $R catalog | grep -q '"channel": "stable"' && ok "catalog carries channels" || bad "catalog channel"
"$PGCAT" --root $R catalog | grep -q '"tags": \["test", "demo"\]' && ok "catalog carries tags" || bad "catalog tags"

echo "scope checker"
printf 'games/a/src/x.c\ngames/a/game.ini\n' | tools/check-scope.sh >/dev/null && ok "one game passes" || bad "one game"
printf 'games/a/x\ngames/b/y\n' | tools/check-scope.sh >/dev/null && bad "two games accepted" || ok "two games rejected"
printf 'games/a/x\nsdk/pg.c\n' | tools/check-scope.sh >/dev/null && bad "game+sdk accepted" || ok "game+sdk rejected"

echo "pg subscribe/sync/run on a throw-away repo"
R=$T/repo; mkdir -p $R/games $R/channels $R/build
cp -a sdk tools tests Makefile $R/
cp -a "$B/sdk" $R/build/sdk 2>/dev/null
for g in demo-a demo-b; do
    cp -a games/_template $R/games/$g
    sed -i "s/^id *=.*/id = $g/" $R/games/$g/game.ini
    sed -i "s/\"_template\"/\"$g\"/" $R/games/$g/src/main.c
done
echo demo-b > $R/channels/stable.txt
export PG_REPO=$R PG_HOME=$T/home PG_BUILD=$R/build PG_MAKE_ARGS="$SANARG"
PG=tools/pg
# icons come from the real headless icon path, exercising --icon too
for g in demo-a demo-b; do
    make -C $R --no-print-directory GAME=$g BUILD=$R/build $SANARG icon >/dev/null 2>$T/make.err || { bad "build/icon for $g: $(tail -3 $T/make.err)"; }
done
out=$($PG validate 2>&1) && ok "pg validate" || bad "pg validate: $out"
$PG subscribe demo-a >/dev/null && ok "subscribe id" || bad "subscribe id"
$PG subscribe nope >/dev/null 2>&1 && bad "subscribe unknown accepted" || ok "subscribe unknown rejected"
$PG list | grep -q '^\[x\] demo-a' && $PG list | grep -q '^\[ \] demo-b' && ok "list marks subscriptions" || bad "list marks"
$PG sync --no-pull >$T/sync.log 2>&1 && ok "sync" || { bad "sync: $(tail -5 $T/sync.log)"; }
[ -x $T/home/installed/demo-a/demo-a ] && [ -f $T/home/installed/demo-a/game.ini ] && [ -f $T/home/installed/demo-a/icon.bmp ] && [ -f $T/home/installed/demo-a/.pg-installed ] && ok "install layout" || bad "install layout"
grep -q '^demo-a	1.0.0	' $T/home/installed/index.tsv 2>/dev/null || grep -q '^demo-a	0.1.0	' $T/home/installed/index.tsv && ok "index.tsv" || bad "index.tsv: $(cat $T/home/installed/index.tsv 2>&1)"
[ ! -d $T/home/installed/demo-b ] && ok "unsubscribed game not installed" || bad "demo-b installed without subscription"
$PG run demo-a --headless --frames 20 >/dev/null 2>&1 && ok "pg run (headless, 20 ticks)" || bad "pg run"
$PG subscribe @stable >/dev/null && $PG sync --no-pull >/dev/null 2>&1 && [ -x $T/home/installed/demo-b/demo-b ] && ok "channel subscription installs demo-b" || bad "@stable subscription"
$PG unsubscribe demo-a >/dev/null && $PG sync --no-pull >/dev/null 2>&1 && [ ! -d $T/home/installed/demo-a ] && [ -d $T/home/installed/demo-b ] && ok "unsubscribe prunes demo-a, keeps demo-b" || bad "prune"

if [ $fails -ne 0 ]; then echo "selftest: $fails FAILED"; exit 1; fi
echo "selftest: OK"
