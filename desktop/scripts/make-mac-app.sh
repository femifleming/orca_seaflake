#!/bin/sh
# Builds Orca Seaflake as a macOS app and puts it on your Desktop.
#   npm run app:mac                     -> ~/Desktop/Orca Seaflake.app
#   npm run app:mac -- /Applications    -> /Applications/Orca Seaflake.app
set -e
cd "$(dirname "$0")/.."
[ "$(uname)" = "Darwin" ] || { echo "This builds the macOS app, so it has to run on a Mac."; exit 1; }
[ -d node_modules/electron-packager ] || npm ci
if [ "$(sysctl -in hw.optional.arm64)" = "1" ]; then ARCH=arm64; else ARCH=x64; fi
npm run "package:mac:$ARCH"
APP="dist/Orca-Seaflake-darwin-$ARCH/Orca-Seaflake.app"
node scripts/sign-mac-adhoc.js "$APP"
xattr -cr "$APP"
DEST="${1:-$HOME/Desktop}"
rm -rf "$DEST/Orca Seaflake.app"
cp -R "$APP" "$DEST/Orca Seaflake.app"
echo "Done: $DEST/Orca Seaflake.app"
