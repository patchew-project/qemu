#!/bin/sh

# This script checks the git log for URLs to the QEMU launchpad bugtracker
# and optionally checks whether the corresponding bugs are not closed yet.

function show_help {
    echo "Usage:"
    echo "  -s <commit>  : Start searching by this commit"
    echo "  -e <commit>  : End searching by this commit"
    echo "  -c           : Check if bugs are still open"
    echo "  -b           : Open bugs in browser (firefox by default)"
}

while [ $# -ge 1 ]; do
   case "$1" in
    -s)  START="$2" ; shift ;;
    -e)  END="$2" ; shift ;;
    -c)  CHECK_IF_OPEN=1 ;;
    -b)  SHOW_IN_BROWSER=1 ;;
    -h)  show_help ; exit 0 ;;
    *)   echo "Unkown option $1 ... use -h for help." ; exit 1 ;;
   esac
   shift
done

if [ "x$START" = "x" ]; then
    START=`git tag | grep 'v[0-9]*\.[0-9]*.0$' | tail -n 2 | head -n 1`
fi
if [ "x$END" = "x" ]; then
    END=`git tag | grep 'v[0-9]*\.[0-9]*.0$' | tail -n 1`
fi
if [ "x$BROWSER" = "x" ]; then
    BROWSER=firefox
fi

if [ "x$START" = "x" -o "x$END" = "x" ]; then
    echo "Could not determine start or end revision ... Please note that this"
    echo "script must be run from a checked out git repository of QEMU!"
    exit 1
fi

echo "Searching git log for bugs in the range $START..$END"

BUG_URLS=`git log $START..$END \
  | grep 'https://bugs.launchpad.net/\(bugs\|qemu/+bug\)/' \
  | sed 's,\(.*\)\(https://bugs.launchpad.net/\(bugs\|qemu/+bug\)/\)\([0-9]*\).*,\2\4,' \
  | sort -u`

echo Found bug URLs:
for i in $BUG_URLS ; do echo " $i" ; done

if [ "x$CHECK_IF_OPEN" = "x1" ]; then
    echo
    echo "Checking which ones are still opened..."
    for i in $BUG_URLS ; do
        if ! curl -s -L $i | grep "value status" | grep -q "Fix Released" ; then
            echo " $i"
            FINAL_BUG_URLS="$FINAL_BUG_URLS $i"
        fi
    done
else
    FINAL_BUG_URLS=$BUG_URLS
fi

if [ "x$FINAL_BUG_URLS" = "x" ]; then
    echo "No bugs found."
else
    if [ "x$SHOW_IN_BROWSER" = "x1" ]; then
        $BROWSER $FINAL_BUG_URLS
    fi
fi
