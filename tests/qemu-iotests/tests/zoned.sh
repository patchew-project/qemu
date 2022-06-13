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

TEST_OFFSETS="0 1000 4000"
TEST_LENS="1000"
TEST_OPS="zone_report zone_open zone_close zone_finish zone_reset zone_append"


echo "Testing a null_blk device"
echo

for offset in $TEST_OFFSETS; do
    echo "At offset $offset:"
    io_test "write -b" $offset $CLUSTER_SIZE 8
    io_test "read -b" $offset $CLUSTER_SIZE 8
    _check_test_img
done

# success, all done
echo "*** done"
rm -f $seq.full
status=0
