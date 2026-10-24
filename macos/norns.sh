#!/bin/bash
# norns on macOS: start jack (coreaudio), sclang and norns with HOME set to the norns home.
#
# layout (same as /home/we on the device):
#   <norns home>/norns          this repository
#   <norns home>/dust           scripts, audio, data
#   <norns home>/matronrc.lua   written on first start (desktop screen)
#
#   <norns home>/Library/Application Support/SuperCollider   sclang config + extensions
#
#   <norns home>/maiden         maiden (web editor, REPLs, catalog), if installed
#
# usage: macos/norns.sh [--terminal]
#   default: like the device, matron and sclang REPLs are websockets (127.0.0.1:5555/5556)
#            used by maiden at http://127.0.0.1:5000/maiden; matron logs to norns.log
#   --terminal: matron's lua REPL and log are this terminal
#   everything listens on 127.0.0.1 only: these REPLs run arbitrary code.

set -u

TERMINAL=0
[ "${1:-}" = "--terminal" ] && TERMINAL=1

REPO="$(cd "$(dirname "$0")/.." && pwd)"
NORNS_HOME="$(dirname "$REPO")"
DEPS="${NORNS_DEPS:-}"
if [ -z "$DEPS" ]; then
    d="$REPO"
    while [ "$d" != "/" ] && [ ! -d "$d/deps/prefix" ]; do d="$(dirname "$d")"; done
    DEPS="$d/deps/prefix"
fi
if [ "$(uname -m)" = arm64 ]; then BREW_DEFAULT=/opt/homebrew; else BREW_DEFAULT=/usr/local; fi
BREW="${HOMEBREW_PREFIX:-$BREW_DEFAULT}"

export HOME="$NORNS_HOME"
SC_BIN="$DEPS/SuperCollider/SuperCollider.app/Contents/MacOS"
# build/macos: jack_connect / jack_disconnect / jack_lsp (Crone.sc and scripts call them).
# gnubin: the norns core and scripts shell out to GNU tools (find -printf, stat -c, sed -i,
# sha256sum, timeout, shuf ...), as on the device; only norns' own processes see them.
GNUBIN=""
if [ -d "$DEPS/gnubin" ]; then
    GNUBIN="$DEPS/gnubin:" # app bundle
else
    for f in coreutils findutils gnu-sed grep; do
        [ -d "$BREW/opt/$f/libexec/gnubin" ] && GNUBIN="$GNUBIN$BREW/opt/$f/libexec/gnubin:"
    done
fi
# bundled jack drivers (app bundle); homebrew's jackd knows its own
[ -d "$DEPS/lib/jack" ] && export JACK_DRIVER_DIR="$DEPS/lib/jack"
if [ -d "$DEPS/gnubin" ]; then
    # app bundle: only what it brings (and the system)
    export PATH="$REPO/build/macos:$GNUBIN$DEPS/bin:$PATH"
else
    export PATH="$REPO/build/macos:$GNUBIN$DEPS/bin:$BREW/bin:$PATH"
fi
# we start jackd ourselves; clients must not autostart a second server
export JACK_NO_START_SERVER=1
# no norns hardware: the keyboard drives K1-3 / E1-3 when nothing else wants the keys
# (lua/core/keyboard.lua)
export NORNS_HARDWARE_KEYS=1
# the lua core hides device-only menus (wifi, update)
export NORNS_PLATFORM=macos
# dock icon of the norns window
export NORNS_ICON="$REPO/macos/norns-icon.png"
# the MANUAL button when the running script has no docs in the catalog
export NORNS_MANUAL="$REPO/macos/manual/manual.html"
cd "$NORNS_HOME"

mkdir -p dust/code dust/audio dust/data
# the norns release this is (update/version.txt): scripts check it (norns.version.update,
# lua/core/norns.lua). on the device an update writes it; here it comes with the app
if [ -f "$REPO/update/version.txt" ] && ! cmp -s "$REPO/update/version.txt" version.txt; then
    cp "$REPO/update/version.txt" version.txt
fi
if [ ! -f matronrc.lua ]; then
    cat > matronrc.lua <<'EOF'
-- macOS desktop: the norns window (screen:sdl). keys/encoders: 1 2 3 and QW AS ZX on the
-- keyboard (see readme-macos.md), or click / scroll in the left, middle, right third.
_boot.add_io('screen:sdl', {})
EOF
fi

SC_CONF="$NORNS_HOME/Library/Application Support/SuperCollider"
mkdir -p "$SC_CONF/Extensions"
if [ ! -f "$SC_CONF/sclang_conf.yaml" ]; then
    # the include paths norns-config.sc sets up on the device
    cat > "$SC_CONF/sclang_conf.yaml" <<EOF
includePaths:
    - "$NORNS_HOME/norns/sc/core"
    - "$NORNS_HOME/norns/sc/engines"
    - "$NORNS_HOME/dust"
excludePaths:
    []
postInlineWarnings: false
EOF
fi

# one norns at a time: a second one finds the OSC / REPL ports taken
if pgrep -x 'norns|Verdandi' >/dev/null; then
    echo "norns.sh: norns is already running (close its window first)"
    exit 1
fi

# processes we started (bash 3.2: no negative indices, no empty arrays under set -u)
PIDS=""

# a process and everything below it (ws-wrapper -> sclang -> scsynth), children first.
# by parent pid, so a SuperCollider of your own is never touched
tree() {
    local c
    for c in $(pgrep -P "$1" 2>/dev/null); do tree "$c"; done
    echo "$1"
}

kill_tree() {
    local all p n=0
    all="$(tree "$1")"
    for p in $all; do kill "$p" 2>/dev/null; done
    while :; do
        local alive=""
        for p in $all; do kill -0 "$p" 2>/dev/null && alive="$alive $p"; done
        [ -z "$alive" ] && return
        n=$((n + 1))
        if [ $n -gt 25 ]; then
            for p in $alive; do kill -9 "$p" 2>/dev/null; done
            return
        fi
        sleep 0.2
    done
}

cleanup() {
    # reverse start order: jackd goes last, after everything that plays through it
    local p rev=""
    for p in $PIDS; do rev="$p $rev"; done
    for p in $rev; do kill_tree "$p"; done
    wait 2>/dev/null
}
trap cleanup EXIT INT TERM

# audio device, rate and buffer: SYSTEM > AUDIO (lua/core/menu/audio.lua) writes
# audio.conf (input= output= rate= period=, empty device = the macOS default) and restarts
# through AUDIO_MARK. NORNS_JACK_RATE / NORNS_JACK_PERIOD in the environment win.
AUDIO_CONF="$NORNS_HOME/audio.conf"
AUDIO_TOOL="$REPO/build/macos/audio_devices"
export NORNS_AUDIO_TOOL="$AUDIO_TOOL"
read_audio_conf() {
    JACK_IN=""
    JACK_OUT=""
    JACK_RATE=48000
    JACK_PERIOD=256
    if [ -f "$AUDIO_CONF" ]; then
        local k v
        while IFS='=' read -r k v; do
            case "$k" in
            input) JACK_IN="$v" ;;
            output) JACK_OUT="$v" ;;
            rate) case "$v" in 44100 | 48000 | 88200 | 96000) JACK_RATE="$v" ;; esac ;;
            period) case "$v" in 64 | 128 | 256 | 512 | 1024) JACK_PERIOD="$v" ;; esac ;;
            esac
        done < "$AUDIO_CONF"
    fi
    JACK_RATE="${NORNS_JACK_RATE:-$JACK_RATE}"
    JACK_PERIOD="${NORNS_JACK_PERIOD:-$JACK_PERIOD}"
}

JACK_WAIT="$REPO/build/macos/jack_wait"
JACK_PID=""
# helpers get /dev/null as stdin: they share the file description with matron's REPL,
# and one that flips it to non-blocking makes matron's reads fail
run_jackd() {
    local args=(-d coreaudio -r "$JACK_RATE" -p "$JACK_PERIOD")
    local in="$JACK_IN" out="$JACK_OUT"
    # one device chosen: the other side is the macOS default, named, so jackd opens both
    if [ -n "$in$out" ]; then
        [ -n "$in" ] || in="$("$AUDIO_TOOL" --default-input 2>/dev/null)"
        [ -n "$out" ] || out="$("$AUDIO_TOOL" --default-output 2>/dev/null)"
        [ -n "$in" ] && args+=(-C "$in")
        [ -n "$out" ] && args+=(-P "$out")
        # two devices have two clocks
        [ -n "$in" ] && [ -n "$out" ] && [ "$in" != "$out" ] && args+=(-s)
    fi
    echo "norns.sh: starting jackd (coreaudio, ${JACK_RATE} Hz, period ${JACK_PERIOD}${JACK_IN:+, chosen input}${JACK_OUT:+, chosen output})"
    jackd "${args[@]}" < /dev/null > "$NORNS_HOME/jackd.log" 2>&1 &
    JACK_PID=$!
    PIDS="$PIDS $JACK_PID"
    # as long as jackd is alive: the first start after an install is slow while macOS checks
    # the new binaries, and jackd waits while macOS asks for the microphone (every new
    # build of an ad-hoc signed app is asked again). at most 5 minutes
    local n=0
    while [ $n -lt 150 ]; do
        "$JACK_WAIT" 2 2>/dev/null && return 0
        kill -0 "$JACK_PID" 2>/dev/null || return 1
        n=$((n + 1))
    done
    return 1
}
start_jackd() {
    read_audio_conf
    run_jackd && return 0
    # the chosen setting does not start (device gone, rate not supported): macOS defaults,
    # and audio.conf.failed tells SYSTEM > AUDIO
    if [ -f "$AUDIO_CONF" ]; then
        echo "norns.sh: audio settings did not start, using the macOS defaults"
        kill_tree "$JACK_PID"
        mv -f "$AUDIO_CONF" "$AUDIO_CONF.failed"
        JACK_IN=""
        JACK_OUT=""
        JACK_RATE=48000
        JACK_PERIOD=256
        run_jackd && return 0
    fi
    echo "norns.sh: jackd did not start, see $NORNS_HOME/jackd.log"
    return 1
}
if ! "$JACK_WAIT" 0 2>/dev/null; then
    start_jackd || exit 1
fi

WS="$REPO/build/ws-wrapper/ws-wrapper"
# SYSTEM > RESTART (matron/src/system_cmd.cc) leaves this and quits norns
RESTART_MARK="$NORNS_HOME/.norns-restart"
rm -f "$RESTART_MARK"

# the binary: build/norns/norns, named Verdandi in the app (macos/make-app.sh)
NORNS_BIN="$REPO/build/norns/Verdandi"
[ -x "$NORNS_BIN" ] || NORNS_BIN="$REPO/build/norns/norns"

start_norns() {
    if [ $TERMINAL -eq 1 ]; then
        # explicit <&0: a non-interactive shell gives background jobs /dev/null as stdin,
        # which would leave matron's lua REPL without input
        "$NORNS_BIN" <&0 &
    else
        "$WS" "ws://127.0.0.1:5555" "$NORNS_BIN" < /dev/null > "$NORNS_HOME/norns.log" 2>&1 &
    fi
    NORNS_PID=$!
    PIDS="$PIDS $NORNS_PID"
    # sclang wires scsynth into crone's jack ports, so wait for crone
    local n=0
    until jack_lsp 2>/dev/null | grep -q '^crone:input_5$'; do
        n=$((n + 1))
        if [ $n -gt 50 ] || ! kill -0 "$NORNS_PID" 2>/dev/null; then
            echo "norns.sh: crone did not come up, sclang not started"
            return
        fi
        sleep 0.2
    done
    "$WS" "ws://127.0.0.1:5556" "$SC_BIN/sclang" -i maiden < /dev/null > "$NORNS_HOME/sclang.log" 2>&1 &
    SCLANG_PID=$!
    PIDS="$PIDS $SCLANG_PID"
}

# the new sclang must get the old one's OSC port (57120): stop the whole old tree first
stop_sclang() {
    [ -n "${SCLANG_PID:-}" ] && kill_tree "$SCLANG_PID"
}

start_norns

# the norns side of the AUv3: idle until the plugin renders in a DAW
AUV3_PID=""
start_auv3() {
    if [ -x "$REPO/build/macos/auv3_bridge" ]; then
        "$REPO/build/macos/auv3_bridge" < /dev/null > "$NORNS_HOME/auv3_bridge.log" 2>&1 &
        AUV3_PID=$!
        PIDS="$PIDS $AUV3_PID"
    fi
}
start_auv3

MAIDEN="${NORNS_MAIDEN:-$NORNS_HOME/maiden}"
if [ -x "$MAIDEN/maiden" ]; then
    # first start: the catalog sources maiden installs scripts from (tool/project-setup.sh)
    if [ ! -d dust/data/sources ]; then
        mkdir -p dust/data/sources dust/data/catalogs
        cp "$MAIDEN"/dist/sources/*.json dust/data/sources/ 2>/dev/null
    fi
    (cd "$MAIDEN" && exec ./maiden server --host 127.0.0.1 --port 5000 --app ./app/build \
        --data "$NORNS_HOME/dust" --doc "$NORNS_HOME/norns/doc" < /dev/null > "$NORNS_HOME/maiden.log" 2>&1) &
    PIDS="$PIDS $!"
    echo "norns.sh: maiden at http://127.0.0.1:5000/maiden"
fi
[ $TERMINAL -eq 0 ] && echo "norns.sh: matron log in $NORNS_HOME/norns.log (close the norns window or ctrl-c to stop)"

AUDIO_MARK="$NORNS_HOME/.norns-restart-audio"
rm -f "$AUDIO_MARK"
while :; do
    wait "$NORNS_PID"
    [ -f "$RESTART_MARK" ] || break
    rm -f "$RESTART_MARK"
    echo "norns.sh: restarting norns and sclang"
    stop_sclang
    # nothing of the old norns may stay in jack (the app input helper ends with it), and the
    # old crone must be gone from jackd before the new one registers
    pkill -f "$REPO/build/macos/app_input" 2>/dev/null
    n=0
    while jack_lsp 2>/dev/null | grep -q '^crone:' && [ $n -lt 25 ]; do
        sleep 0.2
        n=$((n + 1))
    done
    if jack_lsp 2>/dev/null | grep -q '^crone:' && [ -n "$JACK_PID" ]; then
        echo "norns.sh: the old crone is stuck in jackd, restarting audio"
        touch "$AUDIO_MARK"
    fi
    # new audio settings: jackd and its other client too (only a jackd started here)
    if [ -f "$AUDIO_MARK" ]; then
        rm -f "$AUDIO_MARK"
        if [ -n "$JACK_PID" ]; then
            echo "norns.sh: restarting audio"
            [ -n "$AUV3_PID" ] && kill_tree "$AUV3_PID"
            kill_tree "$JACK_PID"
            start_jackd || break
            start_auv3
        fi
    fi
    start_norns
done
