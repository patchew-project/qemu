---
layout: post
title:  "Cache Modelling TCG Plugin"
date:   2021-06-14 06:00:00 +0000
author: Mahmoud Mandour
categories: [TCG plugins, GSOC]
---

TCG plugins provide means to instrument generated code for both user-mode and
full system emulation, including the ability to intercept every memory access
and instruction execution; this post introduces a new TCG plugin that's used to
simulate configurable L1 separate instruction cache and data cache with
simplicity in mind rather than simulate the intricate microarchitectural
details.

## Overview

The plugin simulates how L1 user-configured caches would behave when given a
working set defined by a program in user-mode, or system-wide working set.
Subsequently, it logs performance statistics along with the most N
cache-thrashing instructions.

### Configurability

The plugin is configurable in terms of:

* icache size parameters: `arg="I=CACHE_SIZE ASSOC BLOCK_SIZE"`
* dcache size parameters: `arg="D=CACHE_SIZE ASSOC BLOCK_SIZE"`
* Eviction policy: `arg="evict=lru|rand|fifo"`
* How many top-most thrashing insns to log: `arg="limit=TOP_N"`
* How many core caches to keep track of: `arg="cores=N_CORES"`

### Multicore caching

Multicore caching is achieved through having independent L1 caches for each
available core.

In __full-system emulation__, the number of available vCPUs is known to the
plugin at plugin-installation time, so separate caches are maintained for those.

In __user-space emulation__, the index of the vCPU initiating a memory access
monotonically increases and is limited with however much the kernel allows to
create. The approach used is that we allocate a static number of caches, and fit
all memory accesses into those cores. This is viable having more threads than
cores will result in interleaving those threads between the available cores so
they might thrash each other anyway.

## Design and implementation

### General structure

A generic cache data structure, `struct Cache`, is used to model either an
icache or dcache. For each known core, the plugin maintains an icache and a
dcache. On a memory access coming from a core, the corresponding cache is
interrogated.

Each cache has a number of cache sets that are used to store the actual cached
locations alongside with metadata that back eviction algorithms. The
structure of a cache with `n` sets, and `m` blocks per sets is summarized in
the following figure:

![cache structure](/screenshots/2021-06-15-cache-structure.png)

### Eviction algorithms

The plugin supports three eviction algorithms:

* Random eviction
* Least recently used (LRU)
* FIFO eviction

##### Random eviction

On a cache miss that requires eviction, a randomly-chosen block is evicted to
make room for the newly-fetched block.

Using random eviction effectively requires no meta data for each set.

#### Least recently used (LRU)

For each set, a generation number is maintained that is incremented on each
memory access and. Current generation number is assigned to the block
currently being accessed. On a cache miss, the block with the least generation
number is evicted.

#### FIFO eviction

A FIFO queue instance is maintained for each set. On a cache miss, the evicted
block is the first-in block, and the newly-fetched block is enqueued as the
last-in block.

## Usage

An example usage of the plugin with comparing naive matrix multiplication and
blocked matrix multiplication with matrix block size matching the cache block
size:

```
./x86_64-linux-user/qemu-x86_64 $(QEMU_ARGS) \
  -plugin ./contrib/plugins/libcache.so,arg="D=8192 4 64",arg="I=8192 4 64" \
  -d plugin \
  -D naive.log \
  ./naive_matmul
```

```
./x86_64-linux-user/qemu-x86_64 $(QEMU_ARGS) \
  -plugin ./contrib/plugins/libcache.so,arg="D=8192 4 64",arg="I=8192 4 64" \
  -d plugin \
  -D blocked.log \
  ./blocked_matmul
```

This will run QEMU and attaches the plugin for both programs. Both runs have the
same configuration:

* dcache: cachesize = 8KBs, associativity = 4, blocksize = 64B.
* icache: cachesize = 8KBs, associativity = 4, blocksize = 64B.
* Default eviction policy is LRU (used for both caches).
* Single core cache

Running both instances will log the following:

* naive.log

```
core #, data accesses, data misses, dmiss rate, insn accesses, insn misses, imiss rate
0       6832990        272871          3.9934%  15429566       10308           0.0668%

address, data misses, instruction
0x40000017cf (mm), 262143, movl (%rax, %rsi, 4), %eax
0x40000017a8 (mm), 5293, movl (%rax, %rcx, 4), %ecx
0x40019ad54e, 511, movzwl 6(%rcx), %edi
0x4000001886 (mm), 377, movl (%rax, %rdx, 4), %eax
...
```

* blocked.log

```
core #, data accesses, data misses, dmiss rate, insn accesses, insn misses, imiss rate
0       7240895        8882            0.1227%  16161159       10058           0.0622%

address, data misses, instruction
0x400000180c (bmm), 2088, movl (%rax, %rsi, 4), %eax
0x40000017ba (bmm), 980, movl (%rax, %rdx, 4), %edx
0x40000017e5 (bmm), 640, movl (%rax, %rcx, 4), %ecx
0x40019ad54e, 511, movzwl 6(%rcx), %edi
...
```

We can note that a mov instrution that belongs to a symbol calls `mm`(in this
case, `mm` is the function that carries out naive matrix multiplication) is the
causing about 96% of the overall instruction caches. On the other hand, in case
of the blocked matrix multiplication data miss rate is much less.

#### Multi-core caching

The plugin accepts a `cores=N_CORES` argument that represents the number of
cores that the plugin must keep track of. Memory accesses generated by excess
threads will be served through the available core caches.

An example usage of the plugin using the `cores` agrument against a program that
creates 4 threads:

```
./x86_64-linux-user/qemu-x86_64 $(QEMU_ARGS) \
    -plugin ./contrib/plugins/libcache.so,arg="cores=4" \
    -d plugin \
    -D logfile \
    ./threaded_prog
```

This reports out the following:

```
core #,data accesses,data misses,dmiss rate,insn accesses,insn misses,imiss rate
0      76739         4195         5.411666% 242616        1555           0.6409%
1      29029         932          3.211106% 70939         988            1.3927%
2      6218          285          4.511835% 15702         382            2.4328%
3      6608          297          4.411946% 16342         384            2.3498%
sum    118594        5709         4.811139% 345599        3309           0.9575%

address, data misses, instruction
0x40019d254e, 510, movzwl 6(%rcx), %edi
0x40019d24fe, 290, movq 8(%rcx), %rbp
0x4001814304, 238, movq %rdx, (%rcx)
0x40018142e0, 161, movq (%rax), %rcx
0x40018142f9, 159, movq 0x10(%rax), %rdx
...
```
