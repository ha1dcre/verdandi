#!/bin/bash
# build.sh: build norns (waf configure when needed, then waf) in a neutral environment.
#
# waf keeps the environment it was configured in (home, user name, PATH, every variable) in
# build/ and .lock-waf*, and logs the paths it searched; run with this script nothing of the
# person building ends up in the folder.
#
# usage: macos/build.sh [configure]   (configure: configure again first)

set -eu
REPO="$(cd "$(dirname "$0")/.." && pwd)"
d="$REPO"
while [ "$d" != "/" ] && [ ! -d "$d/deps/prefix" ]; do d="$(dirname "$d")"; done
DEPS="${NORNS_DEPS:-$d/deps}"
if [ "$(uname -m)" = arm64 ]; then BREW_DEFAULT=/opt/homebrew; else BREW_DEFAULT=/usr/local; fi
BREW="${HOMEBREW_PREFIX:-$BREW_DEFAULT}"
mkdir -p "$DEPS/.home"
CONFIGURE=0
{ [ "${1:-}" = "configure" ] || [ ! -f "$REPO/build/c4che/_cache.py" ]; } && CONFIGURE=1
exec env -i HOME="$DEPS/.home" PATH="$BREW/bin:/usr/bin:/bin:/usr/sbin:/sbin" TMPDIR="${TMPDIR:-/tmp}" \
    LANG=en_US.UTF-8 TERM="${TERM:-dumb}" HOMEBREW_PREFIX="$BREW" NORNS_DEPS="$DEPS/prefix" \
    /bin/bash -c 'cd "$1" && if [ "$3" = 1 ]; then python3 "$2/bin/waf" configure; fi && python3 "$2/bin/waf"' \
    build "$REPO" "$DEPS" "$CONFIGURE"
