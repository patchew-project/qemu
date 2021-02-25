#!/bin/sh -e
#
# Helper script for the install process to apply entitlements

SRC="$1"
DST="$2"
ENTITLEMENT="$3"

cd "$MESON_INSTALL_DESTDIR_PREFIX"
mv -f "$SRC" "$DST"
codesign --entitlements "$ENTITLEMENT" --force -s - "$DST"
