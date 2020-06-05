#!/bin/sh
#
# Update syscall_nr.h files from linux headers asm-generic/unistd.h
#
# This code is licensed under the GPL version 2 or later.  See
# the COPYING file in the top-level directory.
#

# build project
# e.g.
# ./autogen.sh
# ./configure
# make -j$(nproc) all

# build fuzzers
# e.g.
# $CXX $CXXFLAGS -std=c++11 -Iinclude \
#     /path/to/name_of_fuzzer.cc -o $OUT/name_of_fuzzer \
#     $LIB_FUZZING_ENGINE /path/to/library.a

mkdir -p $OUT/lib/              # Shared libraries

# Build once to get the list of dynamic lib paths, and copy them over
./configure --datadir="./data/" --disable-werror --cc="$CC" --cxx="$CXX" \
    --extra-cflags="$CFLAGS -U __OPTIMIZE__ "
make CONFIG_FUZZ=y CFLAGS="$LIB_FUZZING_ENGINE" -j$(nproc) i386-softmmu/fuzz

for i in $(ldd ./i386-softmmu/qemu-fuzz-i386  | cut -f3 -d' '); do 
    cp $i $OUT/lib/
done
rm ./i386-softmmu/qemu-fuzz-i386

# Build a second time to build the final binary with correct rpath
./configure --datadir="./data/" --disable-werror --cc="$CC" --cxx="$CXX" \
    --extra-cflags="$CFLAGS -U __OPTIMIZE__" \
    --extra-ldflags="-Wl,-rpath,'\$\$ORIGIN/lib'"
make CONFIG_FUZZ=y CFLAGS="$LIB_FUZZING_ENGINE" -j$(nproc) i386-softmmu/fuzz

# Copy over the datadir
cp  -r ./pc-bios/ $OUT/pc-bios

# Copy over the qemu-fuzz-i386, naming it according to each available fuzz
# target (See 05509c8e6d fuzz: select fuzz target using executable name)
for target in $(./i386-softmmu/qemu-fuzz-i386 | awk '$1 ~ /\*/  {print $2}');
do
    cp ./i386-softmmu/qemu-fuzz-i386 $OUT/qemu-fuzz-i386-target-$target
done
