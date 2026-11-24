#!/bin/sh
# Static rules from AGENTS.md that are cheap to check mechanically. Run by `make check` and CI.
# usage: tools/lint.sh [game-id...]   (no ids: every game directory)
cd "$(dirname "$0")/.." || exit 2
fail=0
if [ $# -gt 0 ]; then dirs=""; for g in "$@"; do dirs="$dirs games/$g"; done; else dirs=$(ls -d games/*/ | sed 's|/$||'); fi
bad() { echo "LINT: $*"; fail=1; }

# 1. languages: C only
for f in $(find $dirs sdk tools tests -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' -o -name '*.hpp' -o -name '*.py' -o -name '*.rs' -o -name '*.go' -o -name '*.js' -o -name '*.lua' \) 2>/dev/null); do
    bad "$f: only C (and POSIX sh tooling) is allowed"
done

# 2. forbidden APIs in games and the SDK (plain SDL2 + libc only; no processes, network, dynamic loading)
pat_inc='#[[:space:]]*include[[:space:]]*[<"](X11/|wayland|GL/|GLES|EGL/|vulkan|SDL3|SDL_image|SDL_ttf|SDL_mixer|SDL_net|SDL_opengl|SDL_syswm|gtk|Qt|curl/|sys/socket|netinet/|arpa/|dlfcn|pthread)'
pat_call='(^|[^a-zA-Z_0-9])(system|popen|fork|vfork|execl|execlp|execv|execvp|execve|socket|connect|dlopen|setuid|ptrace|ioctl)[[:space:]]*\('
scan() {  # scan <label> <regex> <files...>
    label=$1; re=$2; shift 2
    for f in "$@"; do
        hits=$(grep -nE "$re" "$f" | grep -vE '^[0-9]+:[[:space:]]*(//|/?\*)')
        if [ -n "$hits" ]; then echo "$hits" | sed "s|^|LINT: $f:|; s|\$|   <- $label|"; fail=1; fi
    done
}
scan "forbidden include" "$pat_inc" $(find $dirs sdk -type f \( -name '*.c' -o -name '*.h' \))
scan "forbidden call" "$pat_call" $(find $dirs sdk -type f \( -name '*.c' -o -name '*.h' \) ! -path 'sdk/pg_evdev.c')
scan "forbidden call" "$(echo "$pat_call" | sed 's/|ioctl//')" sdk/pg_evdev.c   # the native touch reader may use ioctl (EVIOCG*) and nothing else on the list
# 3. games never pick the video driver, create windows, present, or exit the process
scan "the SDK owns init/window/present/exit" 'SDL_VIDEODRIVER|SDL_HINT_RENDER_DRIVER|SDL_CreateWindow|SDL_Init[[:space:]]*\(|SDL_Quit[[:space:]]*\(|(^|[^a-zA-Z_])exit[[:space:]]*\(|abort[[:space:]]*\(|SDL_CreateRenderer|SDL_RenderPresent|SDL_GL_' $(find games -type f \( -name '*.c' -o -name '*.h' \))
# 4. size budgets: no big files in a game directory (everything is procedural), icon <= 400 KB
for d in $dirs; do
    id=$(basename "$d")
    for f in $(find "$d" -type f); do
        sz=$(wc -c < "$f")
        case "$f" in
          */icon.bmp) [ "$sz" -le 400000 ] || bad "$f is $sz bytes (max 400000)";;
          *.c|*.h|*.ini|*.touch|*.md) [ "$sz" -le 200000 ] || bad "$f is $sz bytes (max 200000)";;
          *) bad "$f: unexpected file type in a game directory (allowed: game.ini, icon.bmp, README.md, *.touch, src/*.c|h)";;
        esac
        [ -x "$f" ] && bad "$f has the executable bit set"
    done
done
# 5. a game's top-level layout
for d in $dirs; do
    id=$(basename "$d")
    case "$id" in _*) continue;; esac
    [ -f "$d/game.ini" ] || bad "$d: missing game.ini"
    [ -d "$d/src" ] || bad "$d: missing src/"
    [ -f "$d/icon.bmp" ] || bad "$d: missing icon.bmp (make icon GAME=$id)"
done
[ $fail -eq 0 ] && echo "lint: OK"
exit $fail
