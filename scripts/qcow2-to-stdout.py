#!/usr/bin/env python3

# This tool reads a disk image in any format and converts it to qcow2,
# writing the result directly to stdout.
#
# Copyright (C) 2024 Igalia, S.L.
#
# Authors: Alberto Garcia <berto@igalia.com>
#          Madeeha Javed <javed@igalia.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# qcow2 files produced by this script are always arranged like this:
#
# - qcow2 header
# - refcount table
# - refcount blocks
# - L1 table
# - L2 tables
# - Data clusters
#
# A note about variable names: in qcow2 there is one refcount table
# and one (active) L1 table, although each can occupy several
# clusters. For the sake of simplicity the code sometimes talks about
# refcount tables and L1 tables when referring to those clusters.

import argparse
import atexit
import math
import os
import signal
import struct
import sys
import subprocess
import tempfile
import time

QCOW2_DEFAULT_CLUSTER_SIZE = 65536
QCOW2_DEFAULT_REFCOUNT_BITS = 16
QCOW2_DEFAULT_VERSION = 3
QCOW_OFLAG_COPIED = 1 << 63

def bitmap_set(bitmap, idx):
    bitmap[int(idx / 8)] |= (1 << (idx % 8))

def bitmap_test(bitmap, idx):
    return (bitmap[int(idx / 8)] & (1 << (idx % 8))) != 0

# Kill the storage daemon on exit
def kill_storage_daemon(pid_file, raw_file, temp_dir):
    if os.path.exists(pid_file):
        with open(pid_file, 'r') as f:
            pid=int(f.readline())
        os.kill(pid, signal.SIGTERM)
        while os.path.exists(pid_file):
            time.sleep(0.1)
    os.unlink(raw_file)
    os.rmdir(temp_dir)

def write_features(header):
    qcow2_features = [
        # Incompatible
        (0, 0, 'dirty bit'),
        (0, 1, 'corrupt bit'),
        (0, 2, 'external data file'),
        (0, 3, 'compression type'),
        (0, 4, 'extended L2 entries'),
        # Compatible
        (1, 0, 'lazy refcounts'),
        # Autoclear
        (2, 0, 'bitmaps'),
        (2, 1, 'raw external data')
    ]
    struct.pack_into('>I', header, 0x70, 0x6803f857)
    struct.pack_into('>I', header, 0x74, len(qcow2_features) * 48)
    cur_offset = 0x78
    for (feature_type, feature_bit, feature_name) in qcow2_features:
        struct.pack_into('>BB46s', header, cur_offset,
                         feature_type, feature_bit, feature_name.encode('ascii'))
        cur_offset += 48

# Command-line arguments
parser = argparse.ArgumentParser(description='This program converts a QEMU disk image to qcow2 '
                                 'and writes it to the standard output')
parser.add_argument('input_file', help='name of the input file')
parser.add_argument('-f', dest='input_format', metavar='input_format',
                    default='raw',
                    help='format of the input file (default: raw)')
parser.add_argument('-c', dest='cluster_size', metavar='cluster_size',
                    help=f'qcow2 cluster size (default: {QCOW2_DEFAULT_CLUSTER_SIZE})',
                    default=QCOW2_DEFAULT_CLUSTER_SIZE, type=int,
                    choices=[1 << x for x in range(9,22)])
parser.add_argument('-r', dest='refcount_bits', metavar='refcount_bits',
                    help=f'width of the reference count entries (default: {QCOW2_DEFAULT_REFCOUNT_BITS})',
                    default=QCOW2_DEFAULT_REFCOUNT_BITS, type=int,
                    choices=[1 << x for x in range(7)])
parser.add_argument('-v', dest='qcow2_version', metavar='qcow2_version',
                    help=f'qcow2 version (default: {QCOW2_DEFAULT_VERSION})',
                    default=QCOW2_DEFAULT_VERSION, type=int, choices=[2, 3])
args = parser.parse_args()

qcow2_cluster_size = args.cluster_size
qcow2_refcount_bits = args.refcount_bits
qcow2_version = args.qcow2_version
input_format = args.input_format
input_file = args.input_file
if not os.path.exists(input_file):
    print(f'[Error] Input file {input_file} does not exist', file=sys.stderr)
    sys.exit(1)

if qcow2_refcount_bits != 16 and qcow2_version != 3:
    print(f'[Error] refcount_bits={qcow2_refcount_bits} is only supported with qcow2_version=3',
          file=sys.stderr)
    sys.exit(1)

# Some basic values
l1_entries_per_table = int(qcow2_cluster_size / 8)
l2_entries_per_table = int(qcow2_cluster_size / 8)
refcounts_per_table  = int(qcow2_cluster_size / 8)
refcounts_per_block  = int(qcow2_cluster_size * 8 / qcow2_refcount_bits)

# If the input file is not in raw format we can use
# qemu-storage-daemon to make it appear as such
if input_format != 'raw':
    temp_dir = tempfile.mkdtemp()
    pid_file = temp_dir + "/pid"
    raw_file = temp_dir + "/raw"
    open(raw_file, 'wb').close()
    atexit.register(kill_storage_daemon, pid_file, raw_file, temp_dir)
    ret = subprocess.run(["qemu-storage-daemon", "--daemonize", "--pidfile", pid_file,
                          "--blockdev", f"driver=file,node-name=file0,driver=file,filename={input_file},read-only=on",
                          "--blockdev", f"driver={input_format},node-name=disk0,file=file0,read-only=on",
                          "--export", f"type=fuse,id=export0,node-name=disk0,mountpoint={raw_file},writable=off"])
    if ret.returncode != 0:
        print('[Error] Could not start the qemu-storage-daemon', file=sys.stderr)
        sys.exit(1)
    input_file = raw_file

# Virtual disk size, number of data clusters and L1 entries
disk_size = math.ceil(os.path.getsize(input_file) / 512) * 512 # Round up to the nearest multiple of 512
total_data_clusters = math.ceil(disk_size / qcow2_cluster_size)
l1_entries = math.ceil(total_data_clusters / l2_entries_per_table)
allocated_l1_tables = math.ceil(l1_entries / l1_entries_per_table)

# Max L1 table size is 32 MB (QCOW_MAX_L1_SIZE in block/qcow2.h)
if ((l1_entries * 8) > (32 * 1024 *1024)):
    print('[Error] The image size is too large. Try using a larger cluster size',
          file=sys.stderr)
    sys.exit(1)

# Two bitmaps indicating which L1 and L2 entries are set
l1_bitmap = bytearray(int(allocated_l1_tables * l1_entries_per_table / 8))
l2_bitmap = bytearray(int(allocated_l1_tables * l1_entries_per_table * l2_entries_per_table / 8))
allocated_l2_tables = 0
allocated_data_clusters = 0
with open(input_file, 'rb') as reader:
    zero_cluster = bytes(qcow2_cluster_size)
    # Read all clusters from the input file
    for idx in range(total_data_clusters):
        cluster = reader.read(qcow2_cluster_size)
        # If the last cluster is smaller than qcow2_cluster_size pad it with zeroes
        if (len(cluster) < qcow2_cluster_size):
            cluster += bytes(qcow2_cluster_size - len(cluster))
        # If a cluster has non-zero data then it must be allocated
        # in the output file and its L2 entry must be set
        if cluster != zero_cluster:
            bitmap_set(l2_bitmap, idx)
            allocated_data_clusters += 1
            # Allocated data clusters also need their corresponding L1 entry and L2 table
            l1_idx = math.floor(idx / l2_entries_per_table)
            if not bitmap_test(l1_bitmap, l1_idx):
                bitmap_set(l1_bitmap, l1_idx)
                allocated_l2_tables += 1

# Total amount of allocated clusters excluding the refcount blocks and table
total_allocated_clusters = 1 + allocated_l1_tables + allocated_l2_tables + allocated_data_clusters

# Clusters allocated for the refcount blocks and table
allocated_refcount_blocks = math.ceil(total_allocated_clusters  / refcounts_per_block)
allocated_refcount_tables = math.ceil(allocated_refcount_blocks / refcounts_per_table)

# Now we have a problem because allocated_refcount_blocks and allocated_refcount_tables...
# (a) increase total_allocated_clusters, and
# (b) need to be recalculated when total_allocated_clusters is increased
# So we need to repeat the calculation as long as the numbers change
while True:
    new_total_allocated_clusters = total_allocated_clusters + allocated_refcount_tables + allocated_refcount_blocks
    new_allocated_refcount_blocks = math.ceil(new_total_allocated_clusters / refcounts_per_block)
    if new_allocated_refcount_blocks > allocated_refcount_blocks:
        allocated_refcount_blocks = new_allocated_refcount_blocks
        allocated_refcount_tables = math.ceil(allocated_refcount_blocks / refcounts_per_table)
    else:
        break

# Now that we have the final numbers we can update total_allocated_clusters
total_allocated_clusters += allocated_refcount_tables + allocated_refcount_blocks

# At this point we have the exact number of clusters that the output
# image is going to use so we can calculate all the offsets.
current_cluster_idx = 1

refcount_table_offset = current_cluster_idx * qcow2_cluster_size
current_cluster_idx += allocated_refcount_tables

refcount_block_offset = current_cluster_idx * qcow2_cluster_size
current_cluster_idx += allocated_refcount_blocks

l1_table_offset = current_cluster_idx * qcow2_cluster_size
current_cluster_idx += allocated_l1_tables

l2_table_offset = current_cluster_idx * qcow2_cluster_size
current_cluster_idx += allocated_l2_tables

data_clusters_offset = current_cluster_idx * qcow2_cluster_size

# Calculate some values used in the qcow2 header
if allocated_l1_tables == 0:
    l1_table_offset = 0

hdr_cluster_bits = int(math.log2(qcow2_cluster_size))
hdr_refcount_bits = 0
hdr_length = 0
if qcow2_version == 3:
    hdr_refcount_bits = int(math.log2(qcow2_refcount_bits))
    hdr_length = 0x70

### Write qcow2 header
cluster = bytearray(qcow2_cluster_size)
struct.pack_into('>4sIQIIQIIQQIIQQQQII', cluster, 0,
     b'QFI\xfb',            # QCOW magic string
     qcow2_version,
     0,                     # backing file offset
     0,                     # backing file sizes
     hdr_cluster_bits,
     disk_size,
     0,                     # encryption method
     l1_entries,
     l1_table_offset,
     refcount_table_offset,
     allocated_refcount_tables,
     0,                     # number of snapshots
     0,                     # snapshot table offset
     0,                     # compatible features
     0,                     # incompatible features
     0,                     # autoclear features
     hdr_refcount_bits,
     hdr_length)

if qcow2_version == 3:
    write_features(cluster)

sys.stdout.buffer.write(cluster)

### Write refcount table
cur_offset = refcount_block_offset
remaining_refcount_table_entries = allocated_refcount_blocks # Each entry is a pointer to a refcount block
while remaining_refcount_table_entries > 0:
    cluster = bytearray(qcow2_cluster_size)
    to_write = min(remaining_refcount_table_entries, refcounts_per_table)
    remaining_refcount_table_entries -= to_write
    for idx in range(to_write):
        struct.pack_into('>Q', cluster, idx * 8, cur_offset)
        cur_offset += qcow2_cluster_size
    sys.stdout.buffer.write(cluster)

### Write refcount blocks
remaining_refcount_block_entries = total_allocated_clusters # One entry for each allocated cluster
for tbl in range(allocated_refcount_blocks):
    cluster = bytearray(qcow2_cluster_size)
    to_write = min(remaining_refcount_block_entries, refcounts_per_block)
    remaining_refcount_block_entries -= to_write
    # All refcount entries contain the number 1. The only difference
    # is their bit width, defined when the image is created.
    for idx in range(to_write):
        if qcow2_refcount_bits == 64:
            struct.pack_into('>Q', cluster, idx * 8, 1)
        elif qcow2_refcount_bits == 32:
            struct.pack_into('>L', cluster, idx * 4, 1)
        elif qcow2_refcount_bits == 16:
            struct.pack_into('>H', cluster, idx * 2, 1)
        elif qcow2_refcount_bits == 8:
            cluster[idx] = 1
        elif qcow2_refcount_bits == 4:
            cluster[int(idx / 2)] |= 1 << ((idx % 2) * 4)
        elif qcow2_refcount_bits == 2:
            cluster[int(idx / 4)] |= 1 << ((idx % 4) * 2)
        elif qcow2_refcount_bits == 1:
            cluster[int(idx / 8)] |= 1 << (idx % 8)
    sys.stdout.buffer.write(cluster)

### Write L1 table
cur_offset = l2_table_offset
for tbl in range(allocated_l1_tables):
    cluster = bytearray(qcow2_cluster_size)
    for idx in range(l1_entries_per_table):
        l1_idx = tbl * l1_entries_per_table + idx
        if bitmap_test(l1_bitmap, l1_idx):
            struct.pack_into('>Q', cluster, idx * 8, cur_offset | QCOW_OFLAG_COPIED)
            cur_offset += qcow2_cluster_size
    sys.stdout.buffer.write(cluster)

### Write L2 tables
cur_offset = data_clusters_offset
for tbl in range(l1_entries):
    # Skip the empty L2 tables. We can identify them because
    # there is no L1 entry pointing at them.
    if bitmap_test(l1_bitmap, tbl):
        cluster = bytearray(qcow2_cluster_size)
        for idx in range(l2_entries_per_table):
            l2_idx = tbl * l2_entries_per_table + idx
            if bitmap_test(l2_bitmap, l2_idx):
                struct.pack_into('>Q', cluster, idx * 8, cur_offset | QCOW_OFLAG_COPIED)
                cur_offset += qcow2_cluster_size
        sys.stdout.buffer.write(cluster)

### Write data clusters
with open(input_file, 'rb') as reader:
    skip = 0
    for idx in range(total_data_clusters):
        if bitmap_test(l2_bitmap, idx):
            if skip > 0:
                reader.seek(qcow2_cluster_size * skip, 1)
                skip = 0
            cluster = reader.read(qcow2_cluster_size)
            # If the last cluster is smaller than qcow2_cluster_size pad it with zeroes
            if (len(cluster) < qcow2_cluster_size):
                cluster += bytes(qcow2_cluster_size - len(cluster))
            sys.stdout.buffer.write(cluster)
        else:
            skip += 1
