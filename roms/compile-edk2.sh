#!/bin/sh

# args
arch="$1"
cross="$2"

# figure gcc version, set cross build prefix vars
gccver=$(${cross}gcc --version \
        | awk '{ print $3; exit}' \
        | cut -d. -f1,2 \
        | tr -d ".")
case "$gccver" in
4*)     # nothing, keep "4x"
        ;;
5* | 6*)
        gccver=5
        ;;
esac
toolchain="GCC$gccver"
eval "export GCC${gccver}_X64_PREFIX=${cross}"
eval "export GCC${gccver}_ARM_PREFIX=${cross}"
eval "export GCC${gccver}_AARCH64_PREFIX=${cross}"

# what we are going to build?
case "$arch" in
i386)
        barch="IA32"
        project="OvmfPkg/OvmfPkgIa32.dsc"
        match="OvmfIa32/*/FV/OVMF_*.fd"
        ;;
x86_64)
        barch="X64"
        project="OvmfPkg/OvmfPkgX64.dsc"
        match="OvmfX64/*/FV/OVMF_*.fd"
        ;;
arm | aarch64)
        barch="$(echo $arch | tr 'a-z' 'A-Z')"
        project="ArmVirtPkg/ArmVirtQemu.dsc"
        match="ArmVirtQemu-${barch}/*/FV/QEMU_*.fd"
        ;;
esac

# setup edk2 build environment
cd edk2
source ./edksetup.sh --reconfig
bopts=""
bopts="$bopts -t $toolchain"
bopts="$bopts -D HTTP_BOOT_ENABLE"
bopts="$bopts -a $barch"
bopts="$bopts -p $project"

# go build everything
make -C BaseTools || exit 1
build $bopts      || exit 1

# copy over results
dest="../../pc-bios/edk2-${arch}"
mkdir -p "$dest"
cp -v Build/${match} "$dest"
