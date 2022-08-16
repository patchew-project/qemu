#!/usr/bin/env bash
#
# Test zone management operations.
#

seq="$(basename $0)"
echo "QA output created by $seq"
status=1 # failure is the default!

_cleanup()
{
  _cleanup_test_img
  sudo rmmod null_blk
}
trap "_cleanup; exit \$status" 0 1 2 3 15

# get standard environment, filters and checks
. ./common.rc
. ./common.filter
. ./common.qemu

# This test only runs on Linux hosts with raw image files.
_supported_fmt raw
_supported_proto file
_supported_os Linux

QEMU_IO="build/qemu-io"
IMG="--image-opts -n driver=zoned_host_device,filename=/dev/nullb0"
QEMU_IO_OPTIONS=$QEMU_IO_OPTIONS_NO_FMT

echo "Testing a null_blk device:"
echo "Simple cases: if the operations work"
sudo modprobe null_blk nr_devices=1 zoned=1

echo "(1) report the first zone:"
sudo $QEMU_IO $IMG -c "zrp 0 1"
echo
echo "report the first 10 zones"
sudo $QEMU_IO $IMG -c "zrp 0 10"
echo
echo "report the last zone:"
sudo $QEMU_IO $IMG -c "zrp 0x3e70000000 2"
echo
echo
echo "(2) opening the first zone"
sudo $QEMU_IO $IMG -c "zo 0 0x80000"
echo "report after:"
sudo $QEMU_IO $IMG -c "zrp 0 1"
echo
echo "opening the second zone"
sudo $QEMU_IO $IMG -c "zo 524288 0x80000" # 524288 is the zone sector size
echo "report after:"
sudo $QEMU_IO $IMG -c "zrp 268435456 1" # 268435456 / 512 = 524288
echo
echo "opening the last zone"
sudo $QEMU_IO $IMG -c "zo 0x1f380000 0x80000"
echo "report after:"
sudo $QEMU_IO $IMG -c "zrp 0x3e70000000 2"
echo
echo
echo "(3) closing the first zone"
sudo $QEMU_IO $IMG -c "zc 0 0x80000"
echo "report after:"
sudo $QEMU_IO $IMG -c "zrp 0 1"
echo
echo "closing the last zone"
sudo $QEMU_IO $IMG -c "zc 0x1f380000 0x80000"
echo "report after:"
sudo $QEMU_IO $IMG -c "zrp 0x3e70000000 2"
echo
echo
echo "(4) finishing the second zone"
sudo $QEMU_IO $IMG -c "zf 524288 0x80000"
echo "After finishing a zone:"
sudo $QEMU_IO $IMG -c "zrp 268435456 1"
echo
echo

echo "(5) resetting the second zone"
sudo $QEMU_IO $IMG -c "zrs 524288 0x80000"
echo "After resetting a zone:"
sudo $QEMU_IO $IMG -c "zrp 268435456 1"
# success, all done
echo "*** done"
rm -f $seq.full
status=0
