#!/bin/bash

CT=parallels-with-bitmap-ct
DIR=$PWD/parallels-with-bitmap-dir
IMG=$DIR/root.hds
XML=$DIR/DiskDescriptor.xml
TARGET=parallels-with-bitmap.bz2

rm -rf $DIR

prlctl create $CT --vmtype ct
prlctl set $CT --device-add hdd --image $DIR --recreate --size 2G

# cleanup the image
qemu-img create -f parallels $IMG 64G

# create bitmap
prlctl backup $CT

prlctl set $CT --device-del hdd1
prlctl destroy $CT

dev=$(ploop mount $XML | sed -n 's/^Adding delta dev=\(\/dev\/ploop[0-9]\+\).*/\1/p')
dd if=/dev/zero of=$dev bs=64K seek=5 count=2 oflag=direct
dd if=/dev/zero of=$dev bs=64K seek=30 count=1 oflag=direct
dd if=/dev/zero of=$dev bs=64K seek=10 count=3 oflag=direct
ploop umount $XML  # bitmap name will be in the output

bzip2 -z $IMG

mv $IMG.bz2 $TARGET

rm -rf $DIR
