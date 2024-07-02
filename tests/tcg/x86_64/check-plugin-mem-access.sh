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
,store_u8,.*,8,store,0xf1
,atomic_op_u8,.*,8,load,0x42
,atomic_op_u8,.*,8,store,0xf1
,load_u8,.*,8,load,0xf1
,store_u16,.*,16,store,0xf123
,atomic_op_u16,.*,16,load,0x42
,atomic_op_u16,.*,16,store,0xf123
,load_u16,.*,16,load,0xf123
,store_u32,.*,32,store,0xff112233
,atomic_op_u32,.*,32,load,0x42
,atomic_op_u32,.*,32,store,0xff112233
,load_u32,.*,32,load,0xff112233
,store_u64,.*,64,store,0xf123456789abcdef
,atomic_op_u64,.*,64,load,0x42
,atomic_op_u64,.*,64,store,0xf123456789abcdef
,load_u64,.*,64,load,0xf123456789abcdef
,store_u128,.*,128,store,0xf122334455667788f123456789abcdef
,load_u128,.*,128,load,0xf122334455667788f123456789abcdef
EOF
}

expected | while read line; do
    check "$plugin_out" "$line"
done
