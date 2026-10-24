#!/bin/bash
# build-appex.sh <output .appex path> <version>: build the norns AUv3 extension (no Xcode
# project; clang + codesign). sandboxed, network client only, ad-hoc signed.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$1"
VERSION="${2:-dev}"

rm -rf "$OUT"
mkdir -p "$OUT/Contents/MacOS"

clang++ -std=c++17 -fobjc-arc -fapplication-extension -O2 -Wall -mmacosx-version-min=13.0 \
    -I "$HERE" "$HERE/NornsAU.mm" \
    -framework AudioToolbox -framework AVFoundation -framework CoreAudioKit -framework AppKit -framework Foundation \
    -Xlinker -e -Xlinker _NSExtensionMain \
    -o "$OUT/Contents/MacOS/verdandi-au"

cat > "$OUT/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>en</string>
    <key>CFBundleDisplayName</key><string>Verdandi</string>
    <key>CFBundleExecutable</key><string>verdandi-au</string>
    <key>CFBundleIdentifier</key><string>community.verdandi.app.au</string>
    <key>CFBundleName</key><string>verdandi-au</string>
    <key>CFBundlePackageType</key><string>XPC!</string>
    <key>CFBundleShortVersionString</key><string>0.1.3</string>
    <key>CFBundleVersion</key><string>$VERSION</string>
    <key>LSMinimumSystemVersion</key><string>13.0</string>
    <key>NSExtension</key>
    <dict>
        <key>NSExtensionAttributes</key>
        <dict>
            <key>AudioComponents</key>
            <array>
                <dict>
                    <key>description</key><string>Verdandi: norns in a DAW track</string>
                    <key>manufacturer</key><string>Vrdn</string>
                    <key>name</key><string>Verdandi: Verdandi</string>
                    <key>subtype</key><string>vrdn</string>
                    <key>type</key><string>aumf</string>
                    <key>version</key><integer>65536</integer>
                    <key>sandboxSafe</key><true/>
                    <key>tags</key><array><string>Effects</string></array>
                </dict>
            </array>
        </dict>
        <key>NSExtensionPointIdentifier</key><string>com.apple.AudioUnit-UI</string>
        <key>NSExtensionPrincipalClass</key><string>NornsViewController</string>
    </dict>
</dict>
</plist>
EOF

codesign --force --sign - --entitlements "$HERE/norns-au.entitlements" "$OUT"
echo "built $OUT"
