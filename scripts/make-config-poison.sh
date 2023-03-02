#! /bin/sh

if test $# = 0; then
  exit 0
fi

# Create list of config switches that should be poisoned in common code...
# but filter out those which are special including:
#   CONFIG_TCG
#   CONFIG_USER_ONLY
#   TARGET_[32|64]_BIT
exec sed -n \
  -e' /CONFIG_TCG/d' \
  -e '/CONFIG_USER_ONLY/d' \
  -e '/TARGET_64BIT/d' \
  -e '/TARGET_32BIT/d' \
  -e '/^#define / {' \
  -e    's///' \
  -e    's/ .*//' \
  -e    's/^/#pragma GCC poison /p' \
  -e '}' "$@" | sort -u
