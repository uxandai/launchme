#!/bin/bash
# Packs an existing Linux build into launchme-<version>-x86_64.AppImage (the asset the Linux updater
# looks for in a release). Highly experimental, like the Linux build itself.
#
# Usage: make-appimage.sh [build dir] [output dir]
#   build dir   default ~/lm/build (tools/linux/wsl-build.sh), with launchme, assets/, companion-model/
#   output dir  default ~/lm/release
# Environment: LM_SRC (the checkout, default ~/lm/src), LM_APPIMAGE_TOOLS (linuxdeploy-x86_64.AppImage and
# linuxdeploy-plugin-appimage-x86_64.AppImage, default ~/lm/appimage-tools).
#
# The launcher's libraries are bundled by linuxdeploy (glibc, the GPU drivers, Vulkan's loader, Fontconfig and
# FreeType stay the system's). launchme-browser (GTK 4 + WebKitGTK) goes in afterwards with no libraries of its
# own: it uses the system's WebKitGTK, and without one pages open in the user's browser.
set -euo pipefail
BUILD="${1:-$HOME/lm/build}"
OUT="${2:-$HOME/lm/release}"
SRC="${LM_SRC:-$HOME/lm/src}"
TOOLS="${LM_APPIMAGE_TOOLS:-$HOME/lm/appimage-tools}"
VERSION=$(sed -n 's/^project(launchme VERSION \([0-9.]*\).*/\1/p' "$SRC/CMakeLists.txt")
[ -n "$VERSION" ] || { echo "no version in $SRC/CMakeLists.txt"; exit 1; }
for tool in linuxdeploy-x86_64.AppImage linuxdeploy-plugin-appimage-x86_64.AppImage; do
  [ -x "$TOOLS/$tool" ] || { echo "missing $TOOLS/$tool"; exit 1; }
done

APPDIR="$OUT/AppDir"
NAME="launchme-$VERSION-x86_64.AppImage"
rm -rf "$APPDIR" "$OUT/$NAME"
mkdir -p "$APPDIR/usr/bin" "$OUT"

# The program and what it reads beside itself: music themes, studio logos, the icon font, the companion's model.
cp "$BUILD/launchme" "$APPDIR/usr/bin/launchme"
strip --strip-debug "$APPDIR/usr/bin/launchme"
cp -r "$BUILD/assets" "$APPDIR/usr/bin/assets"
[ -d "$BUILD/companion-model" ] && cp -r "$BUILD/companion-model" "$APPDIR/usr/bin/companion-model"
cp "$SRC/LICENSE" "$APPDIR/usr/bin/LICENSE.txt"
if [ -d "$BUILD/licenses" ]; then cp -r "$BUILD/licenses" "$APPDIR/usr/bin/licenses"; fi

# The desktop entry and the icon.
cat > "$OUT/launchme.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Launchme
GenericName=Game launcher
Comment=Console-style game launcher
Exec=launchme
Icon=launchme
Categories=Game;
Terminal=false
EOF
convert "$SRC/res/launchme.ico" -thumbnail 256x256 -background none -gravity center -extent 256x256 "$OUT/tmp-icons.png"
ICON=$(ls -S "$OUT"/tmp-icons*.png | head -1)
cp "$ICON" "$OUT/launchme.png"
rm -f "$OUT"/tmp-icons*.png

cd "$OUT"
export APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64
# ONNX Runtime ($ORIGIN) is found next to the program and bundled with the rest.
LD_LIBRARY_PATH="$BUILD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  "$TOOLS/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR" --executable "$APPDIR/usr/bin/launchme" \
  --desktop-file "$OUT/launchme.desktop" --icon-file "$OUT/launchme.png"

if [ -x "$BUILD/launchme-browser" ]; then
  cp "$BUILD/launchme-browser" "$APPDIR/usr/bin/launchme-browser"
  strip --strip-debug "$APPDIR/usr/bin/launchme-browser"
fi

LDAI_OUTPUT="$NAME" VERSION="$VERSION" "$TOOLS/linuxdeploy-plugin-appimage-x86_64.AppImage" --appdir "$APPDIR"
sha256sum "$NAME"
ls -la "$OUT/$NAME"
