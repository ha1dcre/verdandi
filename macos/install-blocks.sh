#!/bin/bash
# install-blocks.sh: put the script builder (macos/blocks) into maiden's web app, where maiden
# serves it at http://127.0.0.1:5000/maiden/blocks/, and link it from maiden's page.
#
# usage: macos/install-blocks.sh <maiden dir> <blockly package dir> <fengari-web package dir>
#   (setup-deps.sh calls it; make-app.sh then bundles <norns home>/maiden with it)
# the page needs, next to its own files: Blockly (vendor/blockly), fengari (vendor/fengari) and,
# for the screen preview, the norns libraries it runs scripts with and the norns font (lib/).

set -eu
REPO="$(cd "$(dirname "$0")/.." && pwd)"
M="$1"
BLOCKLY="$2"
FENGARI="$3"
APP="$M/app/build"
B="$APP/blocks"
[ -f "$APP/index.html" ] || { echo "install-blocks: no maiden web app in $APP"; exit 1; }

rm -rf "$B"
mkdir -p "$B/vendor/blockly/msg" "$B/vendor/fengari" "$B/lib"
# the page: everything in macos/blocks but the patcher's dev page
for f in "$REPO"/macos/blocks/*; do
    case "$(basename "$f")" in patcher-dev.html) ;; *) cp "$f" "$B/" ;; esac
done
cp "$BLOCKLY"/{blockly_compressed.js,blocks_compressed.js,lua_compressed.js,LICENSE} "$B/vendor/blockly/"
cp "$BLOCKLY/msg/en.js" "$B/vendor/blockly/msg/"
cp -R "$BLOCKLY/media" "$B/vendor/blockly/"
cp "$FENGARI/dist/fengari-web.js" "$FENGARI/LICENSE" "$B/vendor/fengari/"
for l in util tabutil musicutil sequins ui er; do cp "$REPO/lua/lib/$l.lua" "$B/lib/"; done
cp "$REPO/resources/norns.ttf" "$B/lib/"

# a link from maiden's page to the builder, once (maiden's own app is not rebuilt for it)
LINK='<a id="verdandi-blocks" href="/maiden/blocks/" title="build a script from blocks" style="position:fixed;right:14px;bottom:12px;z-index:1000;padding:5px 11px;border-radius:14px;background:#e0913f;color:#151515;font:600 12px -apple-system,BlinkMacSystemFont,sans-serif;text-decoration:none;box-shadow:0 1px 4px rgba(0,0,0,.35)">blocks</a>'
if ! grep -q 'id="verdandi-blocks"' "$APP/index.html"; then
    python3 -I - "$APP/index.html" "$LINK" <<'EOF'
import sys
path, link = sys.argv[1], sys.argv[2]
html = open(path).read()
assert html.count('</body>') == 1, 'install-blocks: maiden index.html without one </body>'
open(path, 'w').write(html.replace('</body>', link + '</body>'))
EOF
fi
echo "install-blocks: script builder in $B"
