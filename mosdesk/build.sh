#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
SDK_BUILD="$PROJECT_ROOT/Minimal-OS-SDK/build.sh"
PACKAGE_TOOL="$PROJECT_ROOT/Minimal-OS/tools/mkpkg/mkpkg.py"
DIST="$SCRIPT_DIR/dist"
PACKAGE="$DIST/mosdesk"

if [ ! -x "$SDK_BUILD" ]; then
    echo "MOSdesk: SDK build script missing at $SDK_BUILD" >&2
    echo "Initialize the Minimal-OS-SDK submodule before building." >&2
    exit 1
fi
if [ ! -f "$PACKAGE_TOOL" ]; then
    echo "MOSdesk: Minimal-OS package tool missing at $PACKAGE_TOOL" >&2
    exit 1
fi

mkdir -p "$DIST" "$PACKAGE"
"$SDK_BUILD" --output "$DIST/mosdesk.run" "$SCRIPT_DIR"
"$SDK_BUILD" --slib --output "$DIST/ttrend_raster.slib" \
    "$SCRIPT_DIR/libraries/ttrend_raster"
"$SDK_BUILD" --slib --output "$DIST/ttrend_outline.slib" \
    "$SCRIPT_DIR/libraries/ttrend_outline"

mkdir -p "$SCRIPT_DIR/fonts" "$PACKAGE/fonts"
python3 "$SCRIPT_DIR/tools/generate_font.py" "$SCRIPT_DIR/fonts/arial.font" "$PROJECT_ROOT/Open_Sans.zip"
python3 "$SCRIPT_DIR/tools/generate_font.py" "$SCRIPT_DIR/fonts/roboto.font" "$PROJECT_ROOT/Open_Sans.zip"
python3 "$SCRIPT_DIR/tools/generate_font.py" "$SCRIPT_DIR/fonts/default.font" "$PROJECT_ROOT/Open_Sans.zip"
cp "$SCRIPT_DIR/fonts/arial.font" "$PACKAGE/fonts/arial.font"
cp "$SCRIPT_DIR/fonts/roboto.font" "$PACKAGE/fonts/roboto.font"
cp "$SCRIPT_DIR/fonts/default.font" "$PACKAGE/fonts/default.font"
cp "$DIST/mosdesk.run" "$PACKAGE/mosdesk.run"
cp "$DIST/ttrend_raster.slib" "$PACKAGE/ttrend_raster.slib"
cp "$DIST/ttrend_outline.slib" "$PACKAGE/ttrend_outline.slib"
cp "$SCRIPT_DIR/mosdesk.pkg" "$PACKAGE/mosdesk.pkg"
python3 "$PACKAGE_TOOL" "$PACKAGE" "$DIST/mosdesk.mpkg"

printf 'MOSdesk deliver package: %s\n' "$DIST/mosdesk.mpkg"
printf 'Package directory:       %s\n' "$PACKAGE"