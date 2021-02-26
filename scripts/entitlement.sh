#!/bin/sh -e
#
# Helper script for the build process to apply entitlements

copy=:
if [ "$1" = --install ]; then
  shift
  copy=false
  cd "$MESON_INSTALL_DESTDIR_PREFIX"
fi

SRC="$1"
DST="$2"
ENTITLEMENT="$3"

if $copy; then
  trap 'rm "$DST.tmp"' exit
  cp -af "$SRC" "$DST.tmp"
  SRC="$DST.tmp"
fi

codesign --entitlements "$ENTITLEMENT" --force -s - "$SRC"
mv -f "$SRC" "$DST"
trap '' exit
