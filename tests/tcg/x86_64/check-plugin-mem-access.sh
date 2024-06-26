#!/usr/bin/env bash

set -euo pipefail

die()
{
    echo "$@" 1>&2
    exit 1
}

check()
{
    file=$1
    pattern=$2
    grep "$pattern" "$file" > /dev/null || die "\"$pattern\" not found in $file"
}

[ $# -eq 1 ] || die "usage: plugin_out_file"

plugin_out=$1

expected()
{
    cat << EOF
access: 0xf1,8,store,store_u8
access: 0x42,8,load,atomic_op_u8
access: 0xf1,8,store,atomic_op_u8
access: 0xf1,8,load,load_u8
access: 0xf123,16,store,store_u16
access: 0x42,16,load,atomic_op_u16
access: 0xf123,16,store,atomic_op_u16
access: 0xf123,16,load,load_u16
access: 0xff112233,32,store,store_u32
access: 0x42,32,load,atomic_op_u32
access: 0xff112233,32,store,atomic_op_u32
access: 0xff112233,32,load,load_u32
access: 0xf123456789abcdef,64,store,store_u64
access: 0x42,64,load,atomic_op_u64
access: 0xf123456789abcdef,64,store,atomic_op_u64
access: 0xf123456789abcdef,64,load,load_u64
access: 0xf122334455667788f123456789abcdef,128,store,store_u128
access: 0xf122334455667788f123456789abcdef,128,load,load_u128
EOF
}

expected | while read line; do
    check "$plugin_out" "$line"
done
