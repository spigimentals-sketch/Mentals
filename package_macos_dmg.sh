#!/bin/bash
# Packages every built macOS plugin format (AU components, VST3 bundles,
# Standalone .app bundles) into one distributable .dmg. Run after a
# successful `cmake --build build --config Release` on macOS -- see
# .github/workflows/build-macos.yml, which is the only place this
# currently runs, since there's no Mac in the primary development
# environment this codebase is otherwise built on.
set -euo pipefail

BUILD_DIR="build"
STAGING_DIR="installer/dmg_staging"
OUTPUT_DIR="installer/output"

rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR/VST3" "$STAGING_DIR/Components (AU)" "$STAGING_DIR/Standalone Apps" "$OUTPUT_DIR"

find "$BUILD_DIR" -type d -path "*/Release/VST3/*.vst3" -maxdepth 6 -exec cp -R {} "$STAGING_DIR/VST3/" \;
find "$BUILD_DIR" -type d -path "*/Release/AU/*.component" -maxdepth 6 -exec cp -R {} "$STAGING_DIR/Components (AU)/" \;
find "$BUILD_DIR" -type d -path "*/Release/Standalone/*.app" -maxdepth 6 -exec cp -R {} "$STAGING_DIR/Standalone Apps/" \;

vst3Count=$(find "$STAGING_DIR/VST3" -maxdepth 1 -name "*.vst3" | wc -l | tr -d ' ')
auCount=$(find "$STAGING_DIR/Components (AU)" -maxdepth 1 -name "*.component" | wc -l | tr -d ' ')
appCount=$(find "$STAGING_DIR/Standalone Apps" -maxdepth 1 -name "*.app" | wc -l | tr -d ' ')
echo "Staged $vst3Count VST3 bundles, $auCount AU components, $appCount Standalone apps."

if [ "$vst3Count" -eq 0 ] || [ "$auCount" -eq 0 ] || [ "$appCount" -eq 0 ]; then
    echo "ERROR: one or more plugin formats produced zero bundles -- the build likely failed partway through." >&2
    exit 1
fi

cat > "$STAGING_DIR/Read Me - Install Instructions.txt" << 'EOF'
Mentals Plugins -- macOS Install Instructions
==============================================

These builds are produced by this project's own CI and are NOT
notarized/code-signed by Apple, so Gatekeeper will block them the first
time you open one. For each item you copy into place below: right-click
it -> Open -> Open (once) -- or, in Terminal, run:
    xattr -dr com.apple.quarantine "<path to the item you copied>"

1. VST3 plugins
   Copy everything from the "VST3" folder into:
     ~/Library/Audio/Plug-Ins/VST3/

2. Audio Unit (AU) plugins
   Copy everything from the "Components (AU)" folder into:
     ~/Library/Audio/Plug-Ins/Components/

3. Standalone apps
   Copy anything you want from "Standalone Apps" into /Applications.

Mentals Suite hosts every other plugin in one chain -- if you only want
to try one thing first, install that one plus whichever individual
plugins you want available inside it.

Apple Silicon only: these builds target arm64 (M1 and later). They will
not run on an Intel Mac.
EOF

hdiutil create -volname "Mentals Plugins" -srcfolder "$STAGING_DIR" -ov -format UDZO "$OUTPUT_DIR/MentalsPluginsInstaller.dmg"

echo "Created $OUTPUT_DIR/MentalsPluginsInstaller.dmg"
