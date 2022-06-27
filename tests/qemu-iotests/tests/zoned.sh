#!/usr/bin/env bash
#
# Test zone management operations.
#

QEMU_IO="build/qemu-io"
IMG="--image-opts driver=zoned_host_device,filename=/dev/nullb0"
QEMU_IO_OPTIONS=$QEMU_IO_OPTIONS_NO_FMT

echo "Testing a null_blk device"
echo "Simple cases: if the operations work"
sudo modprobe null_blk nr_devices=1 zoned=1
# hidden issues:
# 1. memory allocation error of "unaligned tcache chunk detected" when the nr_zone=1 in zone report
# 2. qemu-io: after report 10 zones, the program failed at double free error and exited.
echo "report the first zone"
sudo $QEMU_IO $IMG -c "zone_report 0 0 1"
echo "report: the first 10 zones"
sudo $QEMU_IO $IMG -c "zone_report 0 0 10"

echo "open the first zone"
sudo $QEMU_IO $IMG -c "zone_open 0 0x80000"
echo "report after:"
sudo $QEMU_IO $IMG -c "zone_report 0 0 1"
echo "open the last zone"
sudo $QEMU_IO $IMG -c "zone_open 0x3e70000000 0x80000"
echo "report after:"
sudo $QEMU_IO $IMG -c "zone_report 0x3e70000000 0 2"

echo "close the first zone"
sudo $QEMU_IO $IMG -c "zone_close 0 0x80000"
echo "report after:"
sudo $QEMU_IO $IMG -c "zone_report 0 0 1"
echo "close the last zone"
sudo $QEMU_IO $IMG -c "zone_close 0x3e70000000 0x80000"
echo "report after:"
sudo $QEMU_IO $IMG -c "zone_report 0x3e70000000 0 2"


echo "reset the second zone"
sudo $QEMU_IO $IMG -c "zone_reset 0x80000 0x80000"
echo "After resetting a zone:"
sudo $QEMU_IO $IMG -c "zone_report 0x80000 0 5"

# success, all done
sudo rmmod null_blk
echo "*** done"
#rm -f $seq.full
status=0
