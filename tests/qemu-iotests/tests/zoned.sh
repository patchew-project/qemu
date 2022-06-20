#!/usr/bin/env bash
#
# Test zone management operations.
#

seq=`basename $0`
echo "QA output created by $seq"

status=1 # failure is the default!

_cleanup()
{
  _cleanup_test_img
}
trap "_cleanup; exit \$status" 0 1 2 3 15

# get standard environment, filters and checks
. ./common.rc
. ./common.filter
. ./common.pattern

# much of this could be generic for any format supporting compression.
_supported_fmt qcow qcow2
_supported_proto file
_supported_os Linux

TEST_OFFSETS="0"
TEST_LENS="1000"
IMG="--image-opts driver=zoned_host_device,filename=/dev/nullb0 -c"
QEMU_IO_OPTIONS=$QEMU_IO_OPTIONS_NO_FMT

echo "Testing a null_blk device"
echo
echo "Simple cases: testing operations once at a time"
echo
echo "At beginning: report all of the zones"
echo
$QEMU_IO "$IMG zone_report"
$QEMU_IO "$IMG zone_open"
echo "After opening a zone:"
$QEMU_IO "$IMG zone_report"
$QEMU_IO "$IMG zone_close"
echo "After closing a zone:"
$QEMU_IO "$IMG zone_report"
$QEMU_IO "$IMG zone_reset"
echo "After resetting a zone:"
$QEMU_IO "$IMG zone_report"

# success, all done
echo "*** done"
rm -f $seq.full
status=0
