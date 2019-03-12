#!/bin/sh
# Enable automatic program execution by the kernel.

qemu_target_list="i386 i486 alpha arm armeb sparc32plus ppc ppc64 ppc64le m68k \
mips mipsel mipsn32 mipsn32el mips64 mips64el \
sh4 sh4eb s390x aarch64 aarch64_be hppa riscv32 riscv64 xtensa xtensaeb \
microblaze microblazeel or1k x86_64"

# check if given TARGETS is/are in the supported target list
qemu_check_target_list() {
    if [ $# -eq 0 ] ; then
      checked_target_list="$qemu_target_list"
      return
    fi
    unset checked_target_list
    for target ; do
        for cpu in $qemu_target_list ; do
            if [ "x$cpu" = "x$target" ] ; then
                checked_target_list="$checked_target_list $target"
                break
            fi
        done
        if [ "x$cpu" != "x$target" ] ; then
            echo "ERROR: unknown CPU \"$target\"" 1>&2
            usage
            exit 1
        fi
    done
}

i386_magic='\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x03\x00'
i386_mask='\xff\xff\xff\xff\xff\xfe\xfe\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
i386_family=i386

i486_magic='\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x06\x00'
i486_mask='\xff\xff\xff\xff\xff\xfe\xfe\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
i486_family=i386

x86_64_magic='\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x3e\x00'
x86_64_mask='\xff\xff\xff\xff\xff\xfe\xfe\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
x86_64_family=i386

alpha_magic='\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x26\x90'
alpha_mask='\xff\xff\xff\xff\xff\xfe\xfe\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
alpha_family=alpha

arm_magic='\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x28\x00'
arm_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
arm_family=arm

armeb_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x28'
armeb_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
armeb_family=armeb

sparc_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x02'
sparc_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
sparc_family=sparc

sparc32plus_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x12'
sparc32plus_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
sparc32plus_family=sparc

ppc_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x14'
ppc_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
ppc_family=ppc

ppc64_magic='\x7fELF\x02\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x15'
ppc64_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
ppc64_family=ppc

ppc64le_magic='\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x15\x00'
ppc64le_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\x00'
ppc64le_family=ppcle

m68k_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x04'
m68k_mask='\xff\xff\xff\xff\xff\xff\xfe\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
m68k_family=m68k

# FIXME: We could use the other endianness on a MIPS host.

mips_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x08'
mips_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
mips_family=mips

mipsel_magic='\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x08\x00'
mipsel_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
mipsel_family=mips

mipsn32_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x08'
mipsn32_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
mipsn32_family=mips

mipsn32el_magic='\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x08\x00'
mipsn32el_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
mipsn32el_family=mips

mips64_magic='\x7fELF\x02\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x08'
mips64_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
mips64_family=mips

mips64el_magic='\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x08\x00'
mips64el_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
mips64el_family=mips

sh4_magic='\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x2a\x00'
sh4_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
sh4_family=sh4

sh4eb_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x2a'
sh4eb_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
sh4eb_family=sh4

s390x_magic='\x7fELF\x02\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x16'
s390x_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
s390x_family=s390x

aarch64_magic='\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\xb7\x00'
aarch64_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
aarch64_family=arm

aarch64_be_magic='\x7fELF\x02\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\xb7'
aarch64_be_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
aarch64_be_family=armeb

hppa_magic='\x7f\x45\x4c\x46\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x0f'
hppa_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
hppa_family=hppa

riscv32_magic='\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\xf3\x00'
riscv32_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
riscv32_family=riscv

riscv64_magic='\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\xf3\x00'
riscv64_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
riscv64_family=riscv

xtensa_magic='\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x5e\x00'
xtensa_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
xtensa_family=xtensa

xtensaeb_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x5e'
xtensaeb_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
xtensaeb_family=xtensaeb

microblaze_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\xba\xab'
microblaze_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
microblaze_family=microblaze

microblazeel_magic='\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\xab\xba'
microblazeel_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff'
microblazeel_family=microblazeel

or1k_magic='\x7fELF\x01\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x5c'
or1k_mask='\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff'
or1k_family=or1k

qemu_get_family() {
    cpu=${HOST_ARCH:-$(uname -m)}
    case "$cpu" in
    amd64|i386|i486|i586|i686|i86pc|BePC|x86_64)
        echo "i386"
        ;;
    mips*)
        echo "mips"
        ;;
    "Power Macintosh"|ppc64|powerpc|ppc)
        echo "ppc"
        ;;
    ppc64el|ppc64le)
        echo "ppcle"
        ;;
    arm|armel|armhf|arm64|armv[4-9]*l|aarch64)
        echo "arm"
        ;;
    armeb|armv[4-9]*b|aarch64_be)
        echo "armeb"
        ;;
    sparc*)
        echo "sparc"
        ;;
    riscv*)
        echo "riscv"
        ;;
    *)
        echo "$cpu"
        ;;
    esac
}

usage() {
    cat <<EOF
Usage: qemu-binfmt-conf.sh [options] [TARGETS]

Configure binfmt_misc to use qemu interpreter for the given TARGETS.

Options and associated environment variables:

Argument             Env-variable     Description
TARGETS              QEMU_TARGETS     A single arch name or a list of them (see all names below);
                                      if empty, configure/clear all known targets.
-h|--help                             display this usage
-Q|--path PATH       QEMU_PATH        set path to qemu interpreter(s)
-F|--suffix SUFFIX   QEMU_SUFFIX      add a suffix to the default interpreter name
-p|--persistent      QEMU_PERSISTENT  (yes) load the interpreter and keep it in memory; all future
                                      uses are cloned from the open file.
-c|--credential      QEMU_CREDENTIAL  (yes) credential and security tokens are calculated according
                                      to the binary to interpret
-r|--clear           QEMU_CLEAR       (yes) remove registered interpreters for target TARGETS;
                                      then exit.
-t|--test            QEMU_TEST        (yes) test the setup with the provided arguments, but do not
                                      configure any of the interpreters.
-e|--exportdir PATH                   define where to write configuration files
                                      (default: $SYSTEMDDIR or $DEBIANDIR)
-s|--systemd                          don't write into /proc, generate file(s) for
                                      systemd-binfmt.service;
-d|--debian                           don't write into /proc, generate update-binfmts templates

Defaults:
QEMU_TARGETS=$QEMU_TARGETS
QEMU_PATH=$QEMU_PATH
QEMU_SUFFIX=$QEMU_SUFFIX
QEMU_PERSISTENT=$QEMU_PERSISTENT
QEMU_CREDENTIAL=$QEMU_CREDENTIAL
QEMU_CLEAR=$QEMU_CLEAR
QEMU_TEST=$QEMU_TEST

To import templates with update-binfmts, use :

    sudo update-binfmts --importdir ${EXPORTDIR:-$DEBIANDIR} --import qemu-CPU

To remove interpreter, use :

    sudo update-binfmts --package qemu-CPU --remove qemu-CPU $QEMU_PATH

The environment variable HOST_ARCH allows to override 'uname' to generate configuration files for
a different architecture than the current one.

QEMU target list: $qemu_target_list

EOF
}

qemu_check_access() {
    if [ ! -w "$1" ] ; then
        echo "ERROR: cannot write to $1" 1>&2
        exit 1
    fi
}

qemu_check_bintfmt_misc() {
    # load the binfmt_misc module
    if [ ! -d /proc/sys/fs/binfmt_misc ] ; then
      if ! /sbin/modprobe binfmt_misc ; then
          exit 1
      fi
    fi
    if [ ! -f /proc/sys/fs/binfmt_misc/register ] ; then
      if ! mount binfmt_misc -t binfmt_misc /proc/sys/fs/binfmt_misc ; then
          exit 1
      fi
    fi

    qemu_check_access /proc/sys/fs/binfmt_misc/register
}

installed_dpkg() {
    dpkg --status "$1" > /dev/null 2>&1
}

qemu_check_debian() {
    if [ ! -e /etc/debian_version ] ; then
        echo "WARNING: your system is not a Debian based distro" 1>&2
    elif ! installed_dpkg binfmt-support ; then
        echo "WARNING: package binfmt-support is needed" 1>&2
    fi
    qemu_check_access "$EXPORTDIR"
}

qemu_check_systemd() {
    if ! systemctl -q is-enabled systemd-binfmt.service ; then
        echo "WARNING: systemd-binfmt.service is missing or disabled" 1>&2
    fi
    qemu_check_access "$EXPORTDIR"
}

qemu_generate_register() {
    flags=""
    if [ "x$QEMU_CREDENTIAL" = "xyes" ] ; then
        flags="OC"
    fi
    if [ "x$QEMU_PERSISTENT" = "xyes" ] ; then
        flags="${flags}F"
    fi

    echo ":qemu-$cpu:M::$magic:$mask:$qemu:$flags"
}

qemu_register_interpreter() {
    echo "Setting $qemu as binfmt interpreter for $cpu"
    qemu_generate_register > /proc/sys/fs/binfmt_misc/register
}

qemu_generate_systemd() {
    echo "Setting $qemu as binfmt interpreter for $cpu for systemd-binfmt.service"
    qemu_generate_register > "$EXPORTDIR/qemu-$cpu.conf"
}

qemu_generate_debian() {
    cat > "$EXPORTDIR/qemu-$cpu" <<EOF
package qemu-$cpu
interpreter $qemu
magic $magic
mask $mask
credential $QEMU_CREDENTIAL
EOF
}

qemu_set_binfmts() {
    # probe cpu type
    host_family=$(qemu_get_family)

    # reduce the list of target interpreters to those given in the CLI
    targets=${@:-$QEMU_TARGET}
    qemu_check_target_list $targets

    # register the interpreter for each target except for the native one
    for cpu in $checked_target_list ; do
        magic=$(eval echo \$${cpu}_magic)
        mask=$(eval echo \$${cpu}_mask)
        family=$(eval echo \$${cpu}_family)

        if [ "x$magic" = "x" ] || [ "x$mask" = "x" ] || [ "x$family" = "x" ] ; then
            echo "INTERNAL ERROR: unknown cpu $cpu" 1>&2
            continue
        fi

        qemu="$QEMU_PATH/qemu-$cpu"
        if [ "x$cpu" = "xi486" ] ; then
            qemu="$QEMU_PATH/qemu-i386"
        fi

        qemu="$qemu$QEMU_SUFFIX"
        if [ "x$host_family" != "x$family" ] ; then
            $BINFMT_SET
        fi
    done
}

qemu_clear_notimplemented() {
    echo "ERROR: option clear not implemented for this mode yet" 1>&2
    usage
    exit 1
}

qemu_clear_interpreter() {
    printf %s -1 > "/proc/sys/fs/binfmt_misc/$1"
}

CHECK=qemu_check_bintfmt_misc
BINFMT_SET=qemu_register_interpreter
BINFMT_CLEAR=qemu_clear_interpreter

SYSTEMDDIR="/etc/binfmt.d"
DEBIANDIR="/usr/share/binfmts"

QEMU_TARGETS="${QEMU_TARGETS:-}"
QEMU_PATH="${QEMU_PATH:-/usr/local/bin}"
QEMU_SUFFIX="${QEMU_SUFFIX:-}"
QEMU_PERSISTENT="${QEMU_PERSISTENT:-no}"
QEMU_CREDENTIAL="${QEMU_CREDENTIAL:-no}"
QEMU_CLEAR="${QEMU_CLEAR:-no}"
QEMU_TEST="${QEMU_TEST:-no}"

options=$(getopt -o trdsQ:S:e:hcp -l test,clear,debian,systemd,path:,suffix:,exportdir:,help,credential,persistent -- "$@")
eval set -- "$options"

while true ; do
    case "$1" in
    -t|--test)
        QEMU_TEST="yes"
        ;;
    -r|--clear)
        QEMU_CLEAR="yes"
        ;;
    -d|--debian)
        CHECK=qemu_check_debian
        BINFMT_SET=qemu_generate_debian
        BINFMT_CLEAR=qemu_clear_notimplemented
        EXPORTDIR=${EXPORTDIR:-$DEBIANDIR}
        ;;
    -s|--systemd)
        CHECK=qemu_check_systemd
        BINFMT_SET=qemu_generate_systemd
        BINFMT_CLEAR=qemu_clear_notimplemented
        EXPORTDIR=${EXPORTDIR:-$SYSTEMDDIR}
        ;;
    -Q|--path)
        shift
        QEMU_PATH="$1"
        ;;
    -F|--suffix)
        shift
        QEMU_SUFFIX="$1"
        ;;
    -e|--exportdir)
        shift
        EXPORTDIR="$1"
        ;;
    -h|--help)
        usage
        exit 1
        ;;
    -c|--credential)
        QEMU_CREDENTIAL="yes"
        ;;
    -p|--persistent)
        QEMU_PERSISTENT="yes"
        ;;
    *)
        break
        ;;
    esac
    shift
done

shift

$CHECK

if [ "x$QEMU_TEST" = "xyes" ] ; then
    BINFMT_SET=:
    BINFMT_CLEAR=:
fi

if [ "x$QEMU_CLEAR" = "xyes" ] ; then
    qemu_check_target_list "$@"
    for t in $checked_target_list ; do
        $BINFMT_CLEAR "qemu-$t"
    done
    exit
fi

qemu_set_binfmts "$@"
