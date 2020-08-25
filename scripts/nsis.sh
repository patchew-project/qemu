#!/bin/bash -e

OUTFILE="$1"
shift
PREFIX="$1"
shift
SRCDIR="$1"
shift
CPU="$1"
shift

DESTDIR=$(mktemp -d)
trap 'rm -rf $DESTDIR' EXIT
make DESTDIR="$DESTDIR/" install

do_signcode() {
    if [ -z "$SIGNCODE" ]; then
        return
    fi
    "$SIGNCODE" "$@"
}

shopt -s nullglob

(
    cd "$DESTDIR$PREFIX"
    for i in qemu-system-*.exe; do
        arch=${i%.exe}
        arch=${arch#qemu-system-}
        echo Section \""$arch"\" "Section_$arch"
        echo SetOutPath \"\$INSTDIR\"
        echo File \"\${BINDIR}\\$i\"
        echo SectionEnd
    done
) > "$DESTDIR$PREFIX/system-emulations.nsh"

(
    cd "$DESTDIR$PREFIX"
    for i in *.exe; do
        do_signcode "$i"
    done
)

if [ "$CPU" = "x86_64" ]; then
    CPUARG="-DW64"
    DLLDIR="w64"
else
    DLLDIR="w32"
fi

if [ -d "$SRCDIR/dll" ]; then
   DLLARG="-DDLLDIR=$SRCDIR/dll/$DLLDIR"
fi

makensis -V2 -NOCD -DSRCDIR="$SRCDIR" -DBINDIR="$DESTDIR$PREFIX" \
         $CPUARG $DLLARG -DOUTFILE="$OUTFILE" "$@"

do_signcode "$OUTFILE"
