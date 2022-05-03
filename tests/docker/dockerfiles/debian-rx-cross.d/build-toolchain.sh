#!/bin/bash

set -e

TARGET=rx-elf

J=$(expr $(nproc) / 2)
TOOLCHAIN_INSTALL=/usr/local
TOOLCHAIN_BIN=${TOOLCHAIN_INSTALL}/bin
CROSS_SYSROOT=${TOOLCHAIN_INSTALL}/$TARGET/sys-root

export PATH=${TOOLCHAIN_BIN}:$PATH

#
# Grab all of the source for the toolchain bootstrap.
#

wget https://ftp.gnu.org/gnu/binutils/binutils-2.37.tar.xz
wget https://ftp.gnu.org/gnu/gcc/gcc-11.2.0/gcc-11.2.0.tar.xz

tar axf binutils-2.37.tar.xz
tar axf gcc-11.2.0.tar.xz

git clone --depth 1 --branch newlib-4.1.0 \
  https://sourceware.org/git/newlib-cygwin.git newlib-4.1.0

# Create a combined gcc/newlib source tree

mkdir -p src/include
cd src
ln -s ../gcc*/* . || true
ln -s ../newlib*/* . || true
cd include
ln -s ../../gcc*/include/* . || true
ln -s ../../newlib*/include/* . || true
cd ../../

# Build binutils

mkdir -p bld-b
cd bld-b
../binu*/configure --disable-werror \
  --prefix=${TOOLCHAIN_INSTALL} --with-sysroot --target=${TARGET}
make -j${J}
make install
cd ..

# Build gcc+newlib

mkdir -p bld
cd bld
../src/configure --disable-werror --disable-shared \
  --prefix=${TOOLCHAIN_INSTALL} --with-sysroot --target=${TARGET} \
  --enable-languages=c --disable-libssp --disable-libsanitizer \
  --disable-libatomic --disable-libgomp --disable-libquadmath
make -j${J}
make install
cd ..
