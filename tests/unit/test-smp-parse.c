/*
 * SMP parsing unit-tests
 *
 * Copyright (C) 2021, Huawei, Inc.
 *
 * Authors:
 *  Yanan Wang <wangyanan55@huawei.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qom/object.h"
#include "qemu/module.h"
#include "qapi/error.h"

#include "hw/boards.h"

#define T true
#define F false

/**
 * SMPTestData:
 * @config - the given SMP configuration for parsing
 * @should_be_valid - whether the given configuration is supposed to be valid
 * @expect - the CPU topology info expected to be parsed out
 */
typedef struct SMPTestData {
    SMPConfiguration config;
    bool should_be_valid;
    CpuTopology expect;
} SMPTestData;

/* the specific machine type info for this test */
static const TypeInfo smp_machine_info = {
    .name = TYPE_MACHINE,
    .parent = TYPE_OBJECT,
    .class_size = sizeof(MachineClass),
    .instance_size = sizeof(MachineState),
};

/*
 * prefer sockets over cores over threads before 6.2.
 * all possible SMP configurations and the corressponding expected outputs
 * are listed for testing, including the valid and invalid ones.
 */
static struct SMPTestData prefer_sockets[] = {
    {
        /* config: no smp configuration provided
         * expect: cpus=1,sockets=1,dies=1,cores=1,threads=1,maxcpus=1 */
        .config = (SMPConfiguration) { F, 0, F, 0, F, 0, F, 0, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 1, 1, 1, 1, 1, 1 },
    }, {
        /* config: -smp 8
         * expect: cpus=8,sockets=8,dies=1,cores=1,threads=1,maxcpus=8 */
        .config = (SMPConfiguration) { T, 8, F, 0, F, 0, F, 0, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 8, 1, 1, 1, 8 },
    }, {
        /* config: -smp sockets=2
         * expect: cpus=2,sockets=2,dies=1,cores=1,threads=1,maxcpus=2 */
        .config = (SMPConfiguration) { F, 0, T, 2, F, 0, F, 0, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 2, 2, 1, 1, 1, 2 },
    }, {
        /* config: -smp cores=4
         * expect: cpus=4,sockets=1,dies=1,cores=4,threads=1,maxcpus=4 */
        .config = (SMPConfiguration) { F, 0, F, 0, F, 0, T, 4, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 4, 1, 1, 4, 1, 4 },
    }, {
        /* config: -smp threads=2
         * expect: cpus=2,sockets=1,dies=1,cores=1,threads=2,maxcpus=2 */
        .config = (SMPConfiguration) { F, 0, F, 0, F, 0, F, 0, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 2, 1, 1, 1, 2, 2 },
    }, {
        /* config: -smp maxcpus=16
         * expect: cpus=16,sockets=16,dies=1,cores=1,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, F, 0, F, 0, F, 0, F, 0, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 16, 1, 1, 1, 16 },
    }, {
        /* config: -smp 8,sockets=2
         * expect: cpus=8,sockets=2,dies=1,cores=4,threads=1,maxcpus=8 */
        .config = (SMPConfiguration) { T, 8, T, 2, F, 0, F, 0, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 4, 1, 8 },
    }, {
        /* config: -smp 8,cores=4
         * expect: cpus=8,sockets=2,dies=1,cores=4,threads=1,maxcpus=8 */
        .config = (SMPConfiguration) { T, 8, F, 0, F, 0, T, 4, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 4, 1, 8 },
    }, {
        /* config: -smp 8,threads=2
         * expect: cpus=8,sockets=4,dies=1,cores=1,threads=2,maxcpus=8 */
        .config = (SMPConfiguration) { T, 8, F, 0, F, 0, F, 0, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 4, 1, 1, 2, 8 },
    }, {
        /* config: -smp 8,maxcpus=16
         * expect: cpus=8,sockets=16,dies=1,cores=1,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { T, 8, F, 0, F, 0, F, 0, F, 0, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 16, 1, 1, 1, 16 },
    }, {
        /* config: -smp sockets=2,cores=4
         * expect: cpus=8,sockets=2,dies=1,cores=4,threads=1,maxcpus=8 */
        .config = (SMPConfiguration) { F, 0, T, 2, F, 0, T, 4, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 4, 1, 8 },
    }, {
        /* config: -smp sockets=2,threads=2
         * expect: cpus=4,sockets=2,dies=1,cores=1,threads=2,maxcpus=4 */
        .config = (SMPConfiguration) { F, 0, T, 2, F, 0, F, 0, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 4, 2, 1, 1, 2, 4 },
    }, {
        /* config: -smp sockets=2,maxcpus=16
         * expect: cpus=16,sockets=2,dies=1,cores=8,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, T, 2, F, 0, F, 0, F, 0, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 1, 8, 1, 16 },
    }, {
        /* config: -smp cores=4,threads=2
         * expect: cpus=8,sockets=1,dies=1,cores=4,threads=2,maxcpus=8 */
        .config = (SMPConfiguration) { F, 0, F, 0, F, 0, T, 4, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 1, 1, 4, 2, 8 },
    }, {
        /* config: -smp cores=4,maxcpus=16
         * expect: cpus=16,sockets=4,dies=1,cores=4,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, F, 0, F, 0, T, 4, F, 0, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 4, 1, 4, 1, 16 },
    }, {
        /* config: -smp threads=2,maxcpus=16
         * expect: cpus=16,sockets=8,dies=1,cores=1,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, F, 0, F, 0, F, 0, T, 2, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 8, 1, 1, 2, 16 },
    }, {
        /* config: -smp 8,sockets=2,cores=4
         * expect: cpus=8,sockets=2,dies=1,cores=4,threads=1,maxcpus=8 */
        .config = (SMPConfiguration) { T, 8, T, 2, F, 0, T, 4, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 4, 1, 8 },
    }, {
        /* config: -smp 8,sockets=2,threads=2
         * expect: cpus=8,sockets=2,dies=1,cores=2,threads=2,maxcpus=8 */
        .config = (SMPConfiguration) { T, 8, T, 2, F, 0, F, 0, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 2, 2, 8 },
    }, {
        /* config: -smp 8,sockets=2,maxcpus=16
         * expect: cpus=8,sockets=2,dies=1,cores=8,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { T, 8, T, 2, F, 0, F, 0, F, 0, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 8, 1, 16 },
    }, {
        /* config: -smp 8,cores=4,threads=2
         * expect: cpus=8,sockets=1,dies=1,cores=4,threads=2,maxcpus=8 */
        .config = (SMPConfiguration) { T, 8, F, 0, F, 0, T, 4, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 1, 1, 4, 2, 8 },
    }, {
        /* config: -smp 8,cores=4,maxcpus=16
         * expect: cpus=8,sockets=4,dies=1,cores=4,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { T, 8, F, 0, F, 0, T, 4, F, 0, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 4, 1, 4, 1, 16 },
    }, {
        /* config: -smp 8,threads=2,maxcpus=16
         * expect: cpus=8,sockets=8,dies=1,cores=1,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { T, 8, F, 0, F, 0, F, 0, T, 2, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 8, 1, 1, 2, 16 },
    }, {
        /* config: -smp sockets=2,cores=4,threads=2
         * expect: cpus=16,sockets=2,dies=1,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, T, 2, F, 0, T, 4, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 1, 4, 2, 16 },
    }, {
        /* config: -smp sockets=2,cores=4,maxcpus=16
         * expect: cpus=16,sockets=2,dies=1,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, T, 2, F, 0, T, 4, F, 0, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 1, 4, 2, 16 },
    }, {
        /* config: -smp sockets=2,threads=2,maxcpus=16
         * expect: cpus=16,sockets=2,dies=1,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, T, 2, F, 0, F, 0, T, 2, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 1, 4, 2, 16 },
    }, {
        /* config: -smp cores=4,threads=2,maxcpus=16
         * expect: cpus=16,sockets=2,dies=1,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, F, 0, F, 0, T, 4, T, 2, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 1, 4, 2, 16 },
    }, {
        /* config: -smp 8,sockets=2,cores=4,threads=1
         * expect: cpus=8,sockets=2,dies=1,cores=4,threads=1,maxcpus=8 */
        .config = (SMPConfiguration) { T, 8, T, 2, F, 0, T, 4, T, 1, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 4, 1, 8 },
    }, {
        /* config: -smp 8,sockets=2,cores=4,maxcpus=16
         * expect: cpus=8,sockets=2,dies=1,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { T, 8, T, 2, F, 0, T, 4, F, 0, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 4, 2, 16 },
    }, {
        /* config: -smp 8,sockets=2,threads=2,maxcpus=16
         * expect: cpus=8,sockets=2,dies=1,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { T, 8, T, 2, F, 0, F, 0, T, 2, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 4, 2, 16 },
    }, {
        /* config: -smp 8,cores=4,threads=2,maxcpus=16
         * expect: cpus=8,sockets=2,dies=1,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { T, 8, F, 0, F, 0, T, 4, T, 2, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 4, 2, 16 },
    }, {
        /* config: -smp sockets=2,cores=4,threads=2,maxcpus=16
         * expect: -smp 16,sockets=2,dies=1,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, T, 2, F, 0, T, 4, T, 2, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 1, 4, 2, 16 },
    }, {
        /* config: -smp 8,sockets=2,cores=4,threads=2,maxcpus=16
         * expect: cpus=8,sockets=2,dies=1,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { T, 8, T, 2, F, 0, T, 4, T, 2, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 4, 2, 16 },
    }, {
        /* config: -smp 8,sockets=2,dies=1,cores=4,threads=2,maxcpus=16
         * expect: cpus=8,sockets=2,dies=1,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { T, 8, T, 2, T, 1, T, 4, T, 2, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 4, 2, 16 },
    }, {
        /* config: -smp 0
         * expect: error, "anything=0" is not allowed */
        .config = (SMPConfiguration) { T, 0, F, 0, F, 0, F, 0, F, 0, F, 0 },
        .should_be_valid = false,
    }, {
        /* config: -smp 8,sockets=0
         * expect: error, "anything=0" is not allowed */
        .config = (SMPConfiguration) { T, 8, T, 0, F, 0, F, 0, F, 0, F, 0 },
        .should_be_valid = false,
    }, {
        /* config: -smp 8,sockets=2,dies=0
         * expect: error, "anything=0" is not allowed */
        .config = (SMPConfiguration) { T, 0, T, 2, T, 0, F, 0, F, 0, F, 0 },
        .should_be_valid = false,
    }, {
        /* config: -smp 8,sockets=2,dies=1,cores=0
         * expect: error, "anything=0" is not allowed */
        .config = (SMPConfiguration) { T, 8, T, 2, T, 1, T, 0, F, 0, F, 0 },
        .should_be_valid = false,
    }, {
        /* config: -smp 8,sockets=2,dies=1,cores=4,threads=0
         * expect: error, "anything=0" is not allowed */
        .config = (SMPConfiguration) { T, 8, T, 2, T, 1, T, 4, T, 0, F, 0 },
        .should_be_valid = false,
    }, {
        /* config: -smp 8,sockets=2,dies=1,cores=4,threads=2,maxcpus=0
         * expect: error, "anything=0" is not allowed */
        .config = (SMPConfiguration) { T, 8, T, 2, T, 1, T, 4, T, 2, T, 0 },
        .should_be_valid = false,
    }, {
        /* config: -smp 8,dies=2
         * expect: error, multi-dies not supported */
        .config = (SMPConfiguration) { T, 8, F, 0, T, 2, F, 0, F, 0, F, 0 },
        .should_be_valid = false,
    }, {
        /* config: -smp 8,sockets=2,cores=8
         * expect: error, sum (16) != max_cpus (8) */
        .config = (SMPConfiguration) { T, 8, T, 2, F, 0, T, 4, T, 2, F, 0 },
        .should_be_valid = false,
    }, {
        /* config: -smp 8,sockets=2,cores=5,threads=2,maxcpus=16
         * expect: error, sum (20) != max_cpus (16) */
        .config = (SMPConfiguration) { F, 0, T, 3, F, 0, T, 5, T, 1, T, 16 },
        .should_be_valid = false,
    }, {
        /* config: -smp 16,maxcpus=12
         * expect: error, sum (12) < smp_cpus (16) */
        .config = (SMPConfiguration) { T, 16, F, 0, F, 0, F, 0, F, 0, T, 12 },
        .should_be_valid = false,
    },
};

static struct SMPTestData prefer_sockets_support_dies[] = {
    {
        /* config: -smp dies=2
         * expect: cpus=2,sockets=1,dies=2,cores=1,threads=1,maxcpus=2 */
        .config = (SMPConfiguration) { F, 0, F, 0, T, 2, F, 0, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 2, 1, 2, 1, 1, 2 },
    }, {
        /* config: -smp 16,dies=2
         * expect: cpus=16,sockets=8,dies=2,cores=1,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { T, 16, F, 0, T, 2, F, 0, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 8, 2, 1, 1, 16 },
    }, {
        /* config: -smp sockets=2,dies=2
         * expect: cpus=4,sockets=2,dies=2,cores=1,threads=1,maxcpus=4 */
        .config = (SMPConfiguration) { F, 0, T, 2, T, 2, F, 0, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 4, 2, 2, 1, 1, 4 },
    }, {
        /* config: -smp dies=2,cores=4
         * expect: cpus=8,sockets=1,dies=2,cores=4,threads=1,maxcpus=8 */
        .config = (SMPConfiguration) { F, 0, F, 0, T, 2, T, 4, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 1, 2, 4, 1, 8 },
    }, {
        /* config: -smp dies=2,threads=2
         * expect: cpus=4,sockets=1,dies=2,cores=1,threads=2,maxcpus=4 */
        .config = (SMPConfiguration) { F, 0, F, 0, T, 2, F, 0, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 4, 1, 2, 1, 2, 4 },
    }, {
        /* config: -smp dies=2,maxcpus=32
         * expect: cpus=32,sockets=16,dies=2,cores=1,threads=1,maxcpus=32 */
        .config = (SMPConfiguration) { F, 0, F, 0, T, 2, F, 0, F, 0, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 32, 16, 2, 1, 1, 32 },
    }, {
        /* config: -smp 16,sockets=2,dies=2
         * expect: cpus=16,sockets=2,dies=2,cores=4,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { T, 16, T, 2, T, 2, F, 0, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 4, 1, 16 },
    }, {
        /* config: -smp 16,dies=2,cores=4
         * expect: cpus=16,sockets=2,dies=2,cores=4,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { T, 16, F, 0, T, 2, T, 4, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 4, 1, 16 },
    }, {
        /* config: -smp 16,dies=2,threads=2
         * expect: cpus=16,sockets=4,dies=2,cores=1,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { T, 16, F, 0, T, 2, F, 0, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 4, 2, 1, 2, 16 },
    }, {
        /* config: -smp 16,dies=2,maxcpus=32
         * expect: cpus=16,sockets=16,dies=2,cores=1,threads=1,maxcpus=32 */
        .config = (SMPConfiguration) { T, 16, F, 0, T, 2, F, 0, F, 0, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 16, 2, 1, 1, 32 },
    }, {
        /* config: -smp sockets=2,dies=2,cores=4
         * expect: cpus=16,sockets=2,dies=2,cores=4,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, T, 2, T, 2, T, 4, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 4, 1, 16 },
    }, {
        /* config: -smp sockets=2,dies=2,threads=2
         * expect: cpus=8,sockets=2,dies=2,cores=1,threads=2,maxcpus=8 */
        .config = (SMPConfiguration) { F, 0, T, 2, T, 2, F, 0, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 2, 1, 2, 8 },
    }, {
        /* config: -smp sockets=2,dies=2,maxcpus=32
         * expect: cpus=32,sockets=2,dies=2,cores=8,threads=1,maxcpus=32 */
        .config = (SMPConfiguration) { F, 0, T, 2, T, 2, F, 0, F, 0, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 32, 2, 2, 8, 1, 32 },
    }, {
        /* config: -smp dies=2,cores=4,threads=2
         * expect: cpus=16,sockets=1,dies=2,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, F, 0, T, 2, T, 4, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 1, 2, 4, 2, 16 },
    }, {
        /* config: -smp dies=2,cores=4,maxcpus=32
         * expect: cpus=32,sockets=4,dies=2,cores=4,threads=1,maxcpus=32 */
        .config = (SMPConfiguration) { F, 0, F, 0, T, 2, T, 4, F, 0, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 32, 4, 2, 4, 1, 32 },
    }, {
        /* config: -smp dies=2,threads=2,maxcpus=32
         * expect: cpus=32,sockets=8,dies=2,cores=1,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { F, 0, F, 0, T, 2, F, 0, T, 2, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 32, 8, 2, 1, 2, 32 },
    }, {
        /* config: -smp 16,sockets=2,dies=2,cores=4
         * expect: cpus=16,sockets=2,dies=2,cores=4,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { T, 16, T, 2, T, 2, T, 4, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 4, 1, 16 },
    }, {
        /* config: -smp 16,sockets=2,dies=2,threads=2
         * expect: cpus=16,sockets=2,dies=2,cores=2,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { T, 16, T, 2, T, 2, F, 0, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 2, 2, 16 },
    }, {
        /* config: -smp 16,sockets=2,dies=2,maxcpus=32
         * expect: cpus=16,sockets=2,dies=2,cores=8,threads=1,maxcpus=32 */
        .config = (SMPConfiguration) { T, 16, T, 2, T, 2, F, 0, F, 0, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 8, 1, 32 },
    }, {
        /* config: -smp 16,dies=2,cores=4,threads=2
         * expect: cpus=16,sockets=1,dies=2,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { T, 16, F, 0, T, 2, T, 4, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 1, 2, 4, 2, 16 },
    }, {
        /* config: -smp 16,dies=2,cores=4,maxcpus=32
         * expect: cpus=16,sockets=4,dies=2,cores=4,threads=1,maxcpus=32 */
        .config = (SMPConfiguration) { T, 16, F, 0, T, 2, T, 4, F, 0, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 4, 2, 4, 1, 32 },
    }, {
        /* config: -smp 16,dies=2,threads=2,maxcpus=32
         * expect: cpus=16,sockets=8,dies=2,cores=1,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { T, 16, F, 0, T, 2, F, 0, T, 2, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 8, 2, 1, 2, 32 },
    }, {
        /* config: -smp sockets=2,dies=2,cores=4,threads=2
         * expect: cpus=32,sockets=2,dies=2,cores=4,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { F, 0, T, 2, T, 2, T, 4, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 32, 2, 2, 4, 2, 32 },
    }, {
        /* config: -smp sockets=2,dies=2,cores=4,maxcpus=32
         * expect: cpus=32,sockets=2,dies=2,cores=4,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { F, 0, T, 2, T, 2, T, 4, F, 0, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 32, 2, 2, 4, 2, 32 },
    }, {
        /* config: -smp sockets=2,dies=2,threads=2,maxcpus=32
         * expect: cpus=32,sockets=2,dies=2,cores=4,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { F, 0, T, 2, T, 2, F, 0, T, 2, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 32, 2, 2, 4, 2, 32 },
    }, {
        /* config: -smp dies=2,cores=4,threads=2,maxcpus=32
         * expect: cpus=32,sockets=2,dies=2,cores=4,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { F, 0, F, 0, T, 2, T, 4, T, 2, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 32, 2, 2, 4, 2, 32 },
    }, {
        /* config: -smp 16,sockets=2,dies=2,cores=4,threads=1
         * expect: cpus=16,sockets=2,dies=2,cores=4,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { T, 16, T, 2, T, 2, T, 4, T, 1, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 4, 1, 16 },
    }, {
        /* config: -smp 16,sockets=2,dies=2,cores=4,maxcpus=32
         * expect: cpus=16,sockets=2,dies=2,cores=4,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { T, 16, T, 2, T, 2, T, 4, F, 0, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 4, 2, 32 },
    }, {
        /* config: -smp 16,sockets=2,dies=2,threads=2,maxcpus=32
         * expect: cpus=16,sockets=2,dies=2,cores=4,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { T, 16, T, 2, T, 2, F, 0, T, 2, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 4, 2, 32 },
    }, {
        /* config: -smp 16,dies=2,cores=4,threads=2,maxcpus=32
         * expect: cpus=16,sockets=2,dies=2,cores=4,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { T, 16, F, 0, T, 2, T, 4, T, 2, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 4, 2, 32 },
    }, {
        /* config: -smp sockets=2,dies=2,cores=4,threads=2,maxcpus=32
         * expect: -smp 32,sockets=2,dies=2,cores=4,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { F, 0, T, 2, T, 2, T, 4, T, 2, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 32, 2, 2, 4, 2, 32 },
    }, {
        /* config: -smp 16,sockets=2,dies=2,cores=4,threads=2,maxcpus=32
         * expect: cpus=16,sockets=2,dies=2,cores=4,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { T, 16, T, 2, T, 2, T, 4, T, 2, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 4, 2, 32 },
    },
};

/*
 * prefer cores over sockets over threads since 6.2.
 * all possible SMP configurations and the corressponding expected outputs
 * are listed for testing, including the valid and invalid ones.
 */
static struct SMPTestData prefer_cores[] = {
    {
        /* config: no smp configuration
         * expect: cpus=1,sockets=1,dies=1,cores=1,threads=1,maxcpus=1 */
        .config = (SMPConfiguration) { F, 0, F, 0, F, 0, F, 0, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 1, 1, 1, 1, 1, 1 },
    }, {
        /* config: -smp 8
         * expect: cpus=8,sockets=1,dies=1,cores=8,threads=1,maxcpus=8 */
        .config = (SMPConfiguration) { T, 8, F, 0, F, 0, F, 0, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 1, 1, 8, 1, 8 },
    }, {
        /* config: -smp sockets=2
         * expect: cpus=2,sockets=2,dies=1,cores=1,threads=1,maxcpus=2 */
        .config = (SMPConfiguration) { F, 0, T, 2, F, 0, F, 0, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 2, 2, 1, 1, 1, 2 },
    }, {
        /* config: -smp cores=4
         * expect: cpus=4,sockets=1,dies=1,cores=4,threads=1,maxcpus=4 */
        .config = (SMPConfiguration) { F, 0, F, 0, F, 0, T, 4, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 4, 1, 1, 4, 1, 4 },
    }, {
        /* config: -smp threads=2
         * expect: cpus=2,sockets=1,dies=1,cores=1,threads=2,maxcpus=2 */
        .config = (SMPConfiguration) { F, 0, F, 0, F, 0, F, 0, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 2, 1, 1, 1, 2, 2 },
    }, {
        /* config: -smp maxcpus=16
         * expect: cpus=16,sockets=1,dies=1,cores=16,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, F, 0, F, 0, F, 0, F, 0, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 1, 1, 16, 1, 16 },
    }, {
        /* config: -smp 8,sockets=2
         * expect: cpus=8,sockets=2,dies=1,cores=4,threads=1,maxcpus=8 */
        .config = (SMPConfiguration) { T, 8, T, 2, F, 0, F, 0, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 4, 1, 8 },
    }, {
        /* config: -smp 8,cores=4
         * expect: cpus=8,sockets=2,dies=1,cores=4,threads=1,maxcpus=8 */
        .config = (SMPConfiguration) { T, 8, F, 0, F, 0, T, 4, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 4, 1, 8 },
    }, {
        /* config: -smp 8,threads=2
         * expect: cpus=8,sockets=1,dies=1,cores=4,threads=2,maxcpus=8 */
        .config = (SMPConfiguration) { T, 8, F, 0, F, 0, F, 0, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 1, 1, 4, 2, 8 },
    }, {
        /* config: -smp 8,maxcpus=16
         * expect: cpus=8,sockets=1,dies=1,cores=16,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { T, 8, F, 0, F, 0, F, 0, F, 0, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 1, 1, 16, 1, 16 },
    }, {
        /* config: -smp sockets=2,cores=4
         * expect: cpus=8,sockets=2,dies=1,cores=4,threads=1,maxcpus=8 */
        .config = (SMPConfiguration) { F, 0, T, 2, F, 0, T, 4, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 4, 1, 8 },
    }, {
        /* config: -smp sockets=2,threads=2
         * expect: cpus=4,sockets=2,dies=1,cores=1,threads=2,maxcpus=4 */
        .config = (SMPConfiguration) { F, 0, T, 2, F, 0, F, 0, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 4, 2, 1, 1, 2, 4 },
    }, {
        /* config: -smp sockets=2,maxcpus=16
         * expect: cpus=16,sockets=2,dies=1,cores=8,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, T, 2, F, 0, F, 0, F, 0, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 1, 8, 1, 16 },
    }, {
        /* config: -smp cores=4,threads=2
         * expect: cpus=8,sockets=1,dies=1,cores=4,threads=2,maxcpus=8 */
        .config = (SMPConfiguration) { F, 0, F, 0, F, 0, T, 4, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 1, 1, 4, 2, 8 },
    }, {
        /* config: -smp cores=4,maxcpus=16
         * expect: cpus=16,sockets=4,dies=1,cores=4,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, F, 0, F, 0, T, 4, F, 0, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 4, 1, 4, 1, 16 },
    }, {
        /* config: -smp threads=2,maxcpus=16
         * expect: cpus=16,sockets=1,dies=1,cores=8,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, F, 0, F, 0, F, 0, T, 2, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 1, 1, 8, 2, 16 },
    }, {
        /* config: -smp 8,sockets=2,cores=4
         * expect: cpus=8,sockets=2,dies=1,cores=4,threads=1,maxcpus=8 */
        .config = (SMPConfiguration) { T, 8, T, 2, F, 0, T, 4, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 4, 1, 8 },
    }, {
        /* config: -smp 8,sockets=2,threads=2
         * expect: cpus=8,sockets=2,dies=1,cores=2,threads=2,maxcpus=8 */
        .config = (SMPConfiguration) { T, 8, T, 2, F, 0, F, 0, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 2, 2, 8 },
    }, {
        /* config: -smp 8,sockets=2,maxcpus=16
         * expect: cpus=8,sockets=2,dies=1,cores=8,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { T, 8, T, 2, F, 0, F, 0, F, 0, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 8, 1, 16 },
    }, {
        /* config: -smp 8,cores=4,threads=2
         * expect: cpus=8,sockets=1,dies=1,cores=4,threads=2,maxcpus=8 */
        .config = (SMPConfiguration) { T, 8, F, 0, F, 0, T, 4, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 1, 1, 4, 2, 8 },
    }, {
        /* config: -smp 8,cores=4,maxcpus=16
         * expect: cpus=8,sockets=4,dies=1,cores=4,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { T, 8, F, 0, F, 0, T, 4, F, 0, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 4, 1, 4, 1, 16 },
    }, {
        /* config: -smp 8,threads=2,maxcpus=16
         * expect: cpus=8,sockets=1,dies=1,cores=8,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { T, 8, F, 0, F, 0, F, 0, T, 2, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 1, 1, 8, 2, 16 },
    }, {
        /* config: -smp sockets=2,cores=4,threads=2
         * expect: cpus=16,sockets=2,dies=1,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, T, 2, F, 0, T, 4, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 1, 4, 2, 16 },
    }, {
        /* config: -smp sockets=2,cores=4,maxcpus=16
         * expect: cpus=16,sockets=2,dies=1,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, T, 2, F, 0, T, 4, F, 0, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 1, 4, 2, 16 },
    }, {
        /* config: -smp sockets=2,threads=2,maxcpus=16
         * expect: cpus=16,sockets=2,dies=1,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, T, 2, F, 0, F, 0, T, 2, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 1, 4, 2, 16 },
    }, {
        /* config: -smp cores=4,threads=2,maxcpus=16
         * expect: cpus=16,sockets=2,dies=1,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, F, 0, F, 0, T, 4, T, 2, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 1, 4, 2, 16 },
    }, {
        /* config: -smp 8,sockets=2,cores=4,threads=1
         * expect: cpus=8,sockets=2,dies=1,cores=4,threads=1,maxcpus=8 */
        .config = (SMPConfiguration) { T, 8, T, 2, F, 0, T, 4, T, 1, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 4, 1, 8 },
    }, {
        /* config: -smp 8,sockets=2,cores=4,maxcpus=16
         * expect: cpus=8,sockets=2,dies=1,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { T, 8, T, 2, F, 0, T, 4, F, 0, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 4, 2, 16 },
    }, {
        /* config: -smp 8,sockets=2,threads=2,maxcpus=16
         * expect: cpus=8,sockets=2,dies=1,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { T, 8, T, 2, F, 0, F, 0, T, 2, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 4, 2, 16 },
    }, {
        /* config: -smp 8,cores=4,threads=2,maxcpus=16
         * expect: cpus=8,sockets=2,dies=1,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { T, 8, F, 0, F, 0, T, 4, T, 2, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 4, 2, 16 },
    }, {
        /* config: -smp sockets=2,cores=4,threads=2,maxcpus=16
         * expect: -smp 16,sockets=2,dies=1,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, T, 2, F, 0, T, 4, T, 2, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 1, 4, 2, 16 },
    }, {
        /* config: -smp 8,sockets=2,cores=4,threads=2,maxcpus=16
         * expect: cpus=8,sockets=2,dies=1,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { T, 8, T, 2, F, 0, T, 4, T, 2, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 4, 2, 16 },
    }, {
        /* config: -smp 8,sockets=2,dies=1,cores=4,threads=2,maxcpus=16
         * expect: cpus=8,sockets=2,dies=1,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { T, 8, T, 2, T, 1, T, 4, T, 2, T, 16 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 1, 4, 2, 16 },
    }, {
        /* config: -smp 0
         * expect: error, "anything=0" is not allowed */
        .config = (SMPConfiguration) { T, 0, F, 0, F, 0, F, 0, F, 0, F, 0 },
        .should_be_valid = false,
    }, {
        /* config: -smp 8,sockets=0
         * expect: error, "anything=0" is not allowed */
        .config = (SMPConfiguration) { T, 8, T, 0, F, 0, F, 0, F, 0, F, 0 },
        .should_be_valid = false,
    }, {
        /* config: -smp 8,sockets=2,dies=0
         * expect: error, "anything=0" is not allowed */
        .config = (SMPConfiguration) { T, 0, T, 2, T, 0, F, 0, F, 0, F, 0 },
        .should_be_valid = false,
    }, {
        /* config: -smp 8,sockets=2,dies=1,cores=0
         * expect: error, "anything=0" is not allowed */
        .config = (SMPConfiguration) { T, 8, T, 2, T, 1, T, 0, F, 0, F, 0 },
        .should_be_valid = false,
    }, {
        /* config: -smp 8,sockets=2,dies=1,cores=4,threads=0
         * expect: error, "anything=0" is not allowed */
        .config = (SMPConfiguration) { T, 8, T, 2, T, 1, T, 4, T, 0, F, 0 },
        .should_be_valid = false,
    }, {
        /* config: -smp 8,sockets=2,dies=1,cores=4,threads=2,maxcpus=0
         * expect: error, "anything=0" is not allowed */
        .config = (SMPConfiguration) { T, 8, T, 2, T, 1, T, 4, T, 2, T, 0 },
        .should_be_valid = false,
    }, {
        /* config: -smp 8,dies=2
         * expect: error, multi-dies not supported */
        .config = (SMPConfiguration) { T, 8, F, 0, T, 2, F, 0, F, 0, F, 0 },
        .should_be_valid = false,
    }, {
        /* config: -smp 8,sockets=2,cores=8
         * expect: error, sum (16) != max_cpus (8) */
        .config = (SMPConfiguration) { T, 8, T, 2, F, 0, T, 4, T, 2, F, 0 },
        .should_be_valid = false,
    }, {
        /* config: -smp 8,sockets=2,cores=5,threads=2,maxcpus=16
         * expect: error, sum (20) != max_cpus (16) */
        .config = (SMPConfiguration) { F, 0, T, 3, F, 0, T, 5, T, 1, T, 16 },
        .should_be_valid = false,
    }, {
        /* config: -smp 16,maxcpus=12
         * expect: error, sum (12) < smp_cpus (16) */
        .config = (SMPConfiguration) { T, 16, F, 0, F, 0, F, 0, F, 0, T, 12 },
        .should_be_valid = false,
    },
};

static struct SMPTestData prefer_cores_support_dies[] = {
    {
        /* config: -smp dies=2
         * expect: cpus=2,sockets=1,dies=2,cores=1,threads=1,maxcpus=2 */
        .config = (SMPConfiguration) { F, 0, F, 0, T, 2, F, 0, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 2, 1, 2, 1, 1, 2 },
    }, {
        /* config: -smp 16,dies=2
         * expect: cpus=16,sockets=1,dies=2,cores=8,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { T, 16, F, 0, T, 2, F, 0, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 1, 2, 8, 1, 16 },
    }, {
        /* config: -smp sockets=2,dies=2
         * expect: cpus=4,sockets=2,dies=2,cores=1,threads=1,maxcpus=4 */
        .config = (SMPConfiguration) { F, 0, T, 2, T, 2, F, 0, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 4, 2, 2, 1, 1, 4 },
    }, {
        /* config: -smp dies=2,cores=4
         * expect: cpus=8,sockets=1,dies=2,cores=4,threads=1,maxcpus=8 */
        .config = (SMPConfiguration) { F, 0, F, 0, T, 2, T, 4, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 1, 2, 4, 1, 8 },
    }, {
        /* config: -smp dies=2,threads=2
         * expect: cpus=4,sockets=1,dies=2,cores=1,threads=2,maxcpus=4 */
        .config = (SMPConfiguration) { F, 0, F, 0, T, 2, F, 0, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 4, 1, 2, 1, 2, 4 },
    }, {
        /* config: -smp dies=2,maxcpus=32
         * expect: cpus=32,sockets=1,dies=2,cores=16,threads=1,maxcpus=32 */
        .config = (SMPConfiguration) { F, 0, F, 0, T, 2, F, 0, F, 0, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 32, 1, 2, 16, 1, 32 },
    }, {
        /* config: -smp 16,sockets=2,dies=2
         * expect: cpus=16,sockets=2,dies=2,cores=4,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { T, 16, T, 2, T, 2, F, 0, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 4, 1, 16 },
    }, {
        /* config: -smp 16,dies=2,cores=4
         * expect: cpus=16,sockets=2,dies=2,cores=4,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { T, 16, F, 0, T, 2, T, 4, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 4, 1, 16 },
    }, {
        /* config: -smp 16,dies=2,threads=2
         * expect: cpus=16,sockets=1,dies=2,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { T, 16, F, 0, T, 2, F, 0, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 1, 2, 4, 2, 16 },
    }, {
        /* config: -smp 16,dies=2,maxcpus=32
         * expect: cpus=16,sockets=1,dies=2,cores=16,threads=1,maxcpus=32 */
        .config = (SMPConfiguration) { T, 16, F, 0, T, 2, F, 0, F, 0, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 1, 2, 16, 1, 32 },
    }, {
        /* config: -smp sockets=2,dies=2,cores=4
         * expect: cpus=16,sockets=2,dies=2,cores=4,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, T, 2, T, 2, T, 4, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 4, 1, 16 },
    }, {
        /* config: -smp sockets=2,dies=2,threads=2
         * expect: cpus=8,sockets=2,dies=2,cores=1,threads=2,maxcpus=8 */
        .config = (SMPConfiguration) { F, 0, T, 2, T, 2, F, 0, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 8, 2, 2, 1, 2, 8 },
    }, {
        /* config: -smp sockets=2,dies=2,maxcpus=32
         * expect: cpus=32,sockets=2,dies=2,cores=8,threads=1,maxcpus=32 */
        .config = (SMPConfiguration) { F, 0, T, 2, T, 2, F, 0, F, 0, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 32, 2, 2, 8, 1, 32 },
    }, {
        /* config: -smp dies=2,cores=4,threads=2
         * expect: cpus=16,sockets=1,dies=2,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { F, 0, F, 0, T, 2, T, 4, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 1, 2, 4, 2, 16 },
    }, {
        /* config: -smp dies=2,cores=4,maxcpus=32
         * expect: cpus=32,sockets=4,dies=2,cores=4,threads=1,maxcpus=32 */
        .config = (SMPConfiguration) { F, 0, F, 0, T, 2, T, 4, F, 0, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 32, 4, 2, 4, 1, 32 },
    }, {
        /* config: -smp dies=2,threads=2,maxcpus=32
         * expect: cpus=32,sockets=1,dies=2,cores=8,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { F, 0, F, 0, T, 2, F, 0, T, 2, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 32, 1, 2, 8, 2, 32 },
    }, {
        /* config: -smp 16,sockets=2,dies=2,cores=4
         * expect: cpus=16,sockets=2,dies=2,cores=4,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { T, 16, T, 2, T, 2, T, 4, F, 0, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 4, 1, 16 },
    }, {
        /* config: -smp 16,sockets=2,dies=2,threads=2
         * expect: cpus=16,sockets=2,dies=2,cores=2,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { T, 16, T, 2, T, 2, F, 0, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 2, 2, 16 },
    }, {
        /* config: -smp 16,sockets=2,dies=2,maxcpus=32
         * expect: cpus=16,sockets=2,dies=2,cores=8,threads=1,maxcpus=32 */
        .config = (SMPConfiguration) { T, 16, T, 2, T, 2, F, 0, F, 0, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 8, 1, 32 },
    }, {
        /* config: -smp 16,dies=2,cores=4,threads=2
         * expect: cpus=16,sockets=1,dies=2,cores=4,threads=2,maxcpus=16 */
        .config = (SMPConfiguration) { T, 16, F, 0, T, 2, T, 4, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 1, 2, 4, 2, 16 },
    }, {
        /* config: -smp 16,dies=2,cores=4,maxcpus=32
         * expect: cpus=16,sockets=4,dies=2,cores=4,threads=1,maxcpus=32 */
        .config = (SMPConfiguration) { T, 16, F, 0, T, 2, T, 4, F, 0, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 4, 2, 4, 1, 32 },
    }, {
        /* config: -smp 16,dies=2,threads=2,maxcpus=32
         * expect: cpus=16,sockets=1,dies=2,cores=8,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { T, 16, F, 0, T, 2, F, 0, T, 2, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 1, 2, 8, 2, 32 },
    }, {
        /* config: -smp sockets=2,dies=2,cores=4,threads=2
         * expect: cpus=32,sockets=2,dies=2,cores=4,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { F, 0, T, 2, T, 2, T, 4, T, 2, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 32, 2, 2, 4, 2, 32 },
    }, {
        /* config: -smp sockets=2,dies=2,cores=4,maxcpus=32
         * expect: cpus=32,sockets=2,dies=2,cores=4,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { F, 0, T, 2, T, 2, T, 4, F, 0, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 32, 2, 2, 4, 2, 32 },
    }, {
        /* config: -smp sockets=2,dies=2,threads=2,maxcpus=32
         * expect: cpus=32,sockets=2,dies=2,cores=4,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { F, 0, T, 2, T, 2, F, 0, T, 2, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 32, 2, 2, 4, 2, 32 },
    }, {
        /* config: -smp dies=2,cores=4,threads=2,maxcpus=32
         * expect: cpus=32,sockets=2,dies=2,cores=4,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { F, 0, F, 0, T, 2, T, 4, T, 2, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 32, 2, 2, 4, 2, 32 },
    }, {
        /* config: -smp 16,sockets=2,dies=2,cores=4,threads=1
         * expect: cpus=16,sockets=2,dies=2,cores=4,threads=1,maxcpus=16 */
        .config = (SMPConfiguration) { T, 16, T, 2, T, 2, T, 4, T, 1, F, 0 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 4, 1, 16 },
    }, {
        /* config: -smp 16,sockets=2,dies=2,cores=4,maxcpus=32
         * expect: cpus=16,sockets=2,dies=2,cores=4,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { T, 16, T, 2, T, 2, T, 4, F, 0, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 4, 2, 32 },
    }, {
        /* config: -smp 16,sockets=2,dies=2,threads=2,maxcpus=32
         * expect: cpus=16,sockets=2,dies=2,cores=4,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { T, 16, T, 2, T, 2, F, 0, T, 2, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 4, 2, 32 },
    }, {
        /* config: -smp 16,dies=2,cores=4,threads=2,maxcpus=32
         * expect: cpus=16,sockets=2,dies=2,cores=4,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { T, 16, F, 0, T, 2, T, 4, T, 2, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 4, 2, 32 },
    }, {
        /* config: -smp sockets=2,dies=2,cores=4,threads=2,maxcpus=32
         * expect: -smp 32,sockets=2,dies=2,cores=4,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { F, 0, T, 2, T, 2, T, 4, T, 2, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 32, 2, 2, 4, 2, 32 },
    }, {
        /* config: -smp 16,sockets=2,dies=2,cores=4,threads=2,maxcpus=32
         * expect: cpus=16,sockets=2,dies=2,cores=4,threads=2,maxcpus=32 */
        .config = (SMPConfiguration) { T, 16, T, 2, T, 2, T, 4, T, 2, T, 32 },
        .should_be_valid = true,
        .expect = (CpuTopology) { 16, 2, 2, 4, 2, 32 },
    },
};

static char *get_config_info(SMPConfiguration *config)
{
    return g_strdup_printf(
        "(SMPConfiguration) {\n"
        "    .has_cpus    = %5s, cpus    = %ld,\n"
        "    .has_sockets = %5s, sockets = %ld,\n"
        "    .has_dies    = %5s, dies    = %ld,\n"
        "    .has_cores   = %5s, cores   = %ld,\n"
        "    .has_threads = %5s, threads = %ld,\n"
        "    .has_maxcpus = %5s, maxcpus = %ld,\n"
        "}",
        config->has_cpus ? "true" : "false", config->cpus,
        config->has_sockets ? "true" : "false", config->sockets,
        config->has_dies ? "true" : "false", config->dies,
        config->has_cores ? "true" : "false", config->cores,
        config->has_threads ? "true" : "false", config->threads,
        config->has_maxcpus ? "true" : "false", config->maxcpus);
}

static char *get_topo_info(CpuTopology *topo)
{
    return g_strdup_printf(
        "(CpuTopology) {\n"
        "    .cpus     = %u,\n"
        "    .sockets  = %u,\n"
        "    .dies     = %u,\n"
        "    .cores    = %u,\n"
        "    .threads  = %u,\n"
        "    .max_cpus = %u,\n"
        "}",
        topo->cpus, topo->sockets, topo->dies,
        topo->cores, topo->threads, topo->max_cpus);
}

static void check_smp_parse(MachineState *ms, SMPTestData *data)
{
    SMPConfiguration *config = &data->config;
    CpuTopology *expect = &data->expect;
    g_autofree char *config_info = NULL;
    g_autofree char *expect_info = NULL;
    g_autofree char *result_info = NULL;
    Error *err = NULL;

    /* call the generic parser smp_parse() in hw/core/machine-smp.c */
    smp_parse(ms, config, &err);

    if (data->should_be_valid) {
        if ((err == NULL) &&
            (ms->smp.cpus == expect->cpus) &&
            (ms->smp.sockets == expect->sockets) &&
            (ms->smp.dies == expect->dies) &&
            (ms->smp.cores == expect->cores) &&
            (ms->smp.threads == expect->threads) &&
            (ms->smp.max_cpus == expect->max_cpus)) {
            return;
        }

        config_info = get_config_info(config);
        expect_info = get_topo_info(expect);

        if (err != NULL) {
            g_printerr("Check smp_parse failed:\n"
                       "config: %s\n"
                       "expect: %s\n"
                       "should_be_valid: yes\n\n"
                       "result_is_valid: no\n"
                       "error_msg: %s\n",
                       config_info, expect_info, error_get_pretty(err));
            error_free(err);
        } else {
            result_info = get_topo_info(&ms->smp);
            g_printerr("Check smp_parse failed:\n"
                       "config: %s\n"
                       "expect: %s\n"
                       "should_be_valid: yes\n\n"
                       "result_is_valid: yes\n"
                       "result: %s\n",
                       config_info, expect_info, result_info);
        }
    } else {
        if (err != NULL) {
            error_free(err);
            return;
        }

        config_info = get_config_info(config);
        result_info = get_topo_info(&ms->smp);

        g_printerr("Check smp_parse failed:\n"
                   "config: %s\n"
                   "should_be_valid: no\n\n"
                   "result_is_valid: yes\n"
                   "result: %s\n",
                   config_info, result_info);
    }

    abort();
}

static void smp_prefer_sockets_test(void)
{
    Object *obj = object_new(TYPE_MACHINE);
    MachineState *ms = MACHINE(obj);
    MachineClass *mc = MACHINE_GET_CLASS(obj);
    int i;

    /* make sure that we have created the object */
    g_assert_nonnull(ms);
    g_assert_nonnull(mc);

    mc->smp_prefer_sockets = true;

    /* test cases when multi-dies are not supported */
    mc->smp_dies_supported = false;
    for (i = 0; i < ARRAY_SIZE(prefer_sockets); i++) {
        check_smp_parse(ms, &prefer_sockets[i]);
    }

    /* test cases when multi-dies are supported */
    mc->smp_dies_supported = true;
    for (i = 0; i < ARRAY_SIZE(prefer_sockets_support_dies); i++) {
        check_smp_parse(ms, &prefer_sockets_support_dies[i]);
    }

    object_unref(obj);
}

static void smp_prefer_cores_test(void)
{
    Object *obj = object_new(TYPE_MACHINE);
    MachineState *ms = MACHINE(obj);
    MachineClass *mc = MACHINE_GET_CLASS(obj);
    int i;

    /* make sure that we have created the object */
    g_assert_nonnull(ms);
    g_assert_nonnull(mc);

    mc->smp_prefer_sockets = false;

    /* test cases when multi-dies are not supported */
    mc->smp_dies_supported = false;
    for (i = 0; i < ARRAY_SIZE(prefer_cores); i++) {
        check_smp_parse(ms, &prefer_cores[i]);
    }

    /* test cases when multi-dies are supported */
    mc->smp_dies_supported = true;
    for (i = 0; i < ARRAY_SIZE(prefer_cores_support_dies); i++) {
        check_smp_parse(ms, &prefer_cores_support_dies[i]);
    }

    object_unref(obj);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    module_call_init(MODULE_INIT_QOM);
    type_register_static(&smp_machine_info);

    g_test_add_func("/test-smp-parse/prefer_sockets", smp_prefer_sockets_test);
    g_test_add_func("/test-smp-parse/prefer_cores", smp_prefer_cores_test);

    g_test_run();

    return 0;
}
