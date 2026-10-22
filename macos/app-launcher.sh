#!/bin/bash
# Verdandi.app launcher (Contents/MacOS/Verdandi): starts norns with the app's own files.
#
# the norns home is ~/norns (override: NORNS_USER_HOME): dust lives there, and ~/norns/norns
# links to the runtime inside this app, so the layout matches /home/we on the device.

CONTENTS="$(cd "$(dirname "$0")/.." && pwd)"
R="$CONTENTS/Resources"
U="${NORNS_USER_HOME:-$HOME/norns}"

mkdir -p "$U/dust/code" "$U/dust/audio" "$U/dust/data"

# ~/norns/norns -> this app's runtime (an existing real folder, e.g. a source checkout, wins)
if [ -L "$U/norns" ] || [ ! -e "$U/norns" ]; then
    ln -sfn "$R/norns" "$U/norns"
fi

# sc3-plugins from the app as a SuperCollider extension of the norns home
EXT="$U/Library/Application Support/SuperCollider/Extensions"
mkdir -p "$EXT"
if [ -L "$EXT/SC3plugins" ] || [ ! -e "$EXT/SC3plugins" ]; then
    ln -sfn "$R/deps/SC3plugins" "$EXT/SC3plugins"
fi

export NORNS_DEPS="$R/deps"
export NORNS_MAIDEN="$R/maiden"
# the app itself: its version, for the update check (mac_update.m)
export VERDANDI_APP="$(dirname "$CONTENTS")"
# a finder launch has a minimal PATH; norns.sh adds the bundled tools in front
export PATH="/usr/bin:/bin:/usr/sbin:/sbin"

# norns.sh through the ~/norns/norns link, so it sees ~/norns as the norns home
exec "$U/norns/macos/norns.sh" "$@"
