/*
 * ips rate limiting plugin.
 *
 * This plugin can be used to restrict the execution of a system to a
 * particular number of Instructions Per Second (ips). This controls
 * time as seen by the guest so while wall-clock time may be longer
 * from the guests point of view time will pass at the normal rate.
 *
 * This uses the new plugin API which allows the plugin to control
 * system time.
 *
 * Copyright (c) 2023 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <glib.h>
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

/* how many times do we update time per sec */
#define NUM_TIME_UPDATE_PER_SEC 10
#define NSEC_IN_ONE_SEC (1000 * 1000 * 1000)

static GMutex global_state_lock;

static uint64_t insn_per_second = 1000 * 1000; /* ips per core, per second */
static uint64_t insn_quantum; /* trap every N instructions */
static bool precise_execution; /* count every instruction */
static int64_t start_time_ns; /* time (ns since epoch) first vCPU started */
static int64_t virtual_time_ns; /* last set virtual time */

static const void *time_handle;

typedef enum {
    UNKNOWN = 0,
    EXECUTING,
    IDLE,
    FINISHED
} vCPUState;

typedef struct {
    uint64_t counter;
    uint64_t track_insn;
    vCPUState state;
    /* timestamp when vCPU entered state */
    int64_t last_state_time;
} vCPUTime;

struct qemu_plugin_scoreboard *vcpus;

/* return epoch time in ns */
static int64_t now_ns(void)
{
    return g_get_real_time() * 1000;
}

static uint64_t num_insn_during(int64_t elapsed_ns)
{
    double num_secs = elapsed_ns / (double) NSEC_IN_ONE_SEC;
    return num_secs * (double) insn_per_second;
}

static int64_t time_for_insn(uint64_t num_insn)
{
    double num_secs = (double) num_insn / (double) insn_per_second;
    return num_secs * (double) NSEC_IN_ONE_SEC;
}

static int64_t uptime_ns(void)
{
    int64_t now = now_ns();
    g_assert(now >= start_time_ns);
    return now - start_time_ns;
}

static void vcpu_set_state(vCPUTime *vcpu, vCPUState new_state)
{
    vcpu->last_state_time = now_ns();
    vcpu->state = new_state;
}

static void update_system_time(vCPUTime *vcpu)
{
    /* flush remaining instructions */
    vcpu->counter += vcpu->track_insn;
    vcpu->track_insn = 0;

    int64_t uptime = uptime_ns();
    uint64_t expected_insn = num_insn_during(uptime);

    if (vcpu->counter >= expected_insn) {
        /* this vcpu ran faster than expected, so it has to sleep */
        uint64_t insn_advance = vcpu->counter - expected_insn;
        uint64_t time_advance_ns = time_for_insn(insn_advance);
        int64_t sleep_us = time_advance_ns / 1000;
        g_usleep(sleep_us);
    }

    /* based on number of instructions, what should be the new time? */
    int64_t new_virtual_time = time_for_insn(vcpu->counter);

    g_mutex_lock(&global_state_lock);

    /* Time only moves forward. Another vcpu might have updated it already. */
    if (new_virtual_time > virtual_time_ns) {
        qemu_plugin_update_ns(time_handle, new_virtual_time);
        virtual_time_ns = new_virtual_time;
    }

    g_mutex_unlock(&global_state_lock);
}

static void set_start_time()
{
    g_mutex_lock(&global_state_lock);
    if (!start_time_ns) {
        start_time_ns = now_ns();
    }
    g_mutex_unlock(&global_state_lock);
}

static void vcpu_init(qemu_plugin_id_t id, unsigned int cpu_index)
{
    vCPUTime *vcpu = qemu_plugin_scoreboard_find(vcpus, cpu_index);
    /* ensure start time is set first */
    set_start_time();
    /* start counter from absolute time reference */
    vcpu->counter = num_insn_during(uptime_ns());
    vcpu_set_state(vcpu, EXECUTING);
}

static void vcpu_idle(qemu_plugin_id_t id, unsigned int cpu_index)
{
    vCPUTime *vcpu = qemu_plugin_scoreboard_find(vcpus, cpu_index);
    vcpu_set_state(vcpu, IDLE);
}

static void vcpu_resume(qemu_plugin_id_t id, unsigned int cpu_index)
{
    vCPUTime *vcpu = qemu_plugin_scoreboard_find(vcpus, cpu_index);
    g_assert(vcpu->state == IDLE);
    int64_t idle_time = now_ns() - vcpu->last_state_time;
    /* accumulate expected number of instructions */
    vcpu->counter += num_insn_during(idle_time);
    vcpu_set_state(vcpu, EXECUTING);
}

static void vcpu_exit(qemu_plugin_id_t id, unsigned int cpu_index)
{
    vCPUTime *vcpu = qemu_plugin_scoreboard_find(vcpus, cpu_index);
    vcpu_set_state(vcpu, FINISHED);
    update_system_time(vcpu);
    vcpu->counter = 0;
}

static void every_insn_quantum(unsigned int cpu_index, void *udata)
{
    vCPUTime *vcpu = qemu_plugin_scoreboard_find(vcpus, cpu_index);
    g_assert(vcpu->track_insn >= insn_quantum);
    update_system_time(vcpu);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n_insns = qemu_plugin_tb_n_insns(tb);
    qemu_plugin_u64 track_insn =
        qemu_plugin_scoreboard_u64_in_struct(vcpus, vCPUTime, track_insn);
    if (precise_execution) {
        /* count (and eventually trap) on every instruction */
        for (int idx = 0; idx < qemu_plugin_tb_n_insns(tb); ++idx) {
            struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, idx);
            qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
                insn, QEMU_PLUGIN_INLINE_ADD_U64, track_insn, 1);
            qemu_plugin_register_vcpu_insn_exec_cond_cb(
                insn, every_insn_quantum,
                QEMU_PLUGIN_CB_NO_REGS, QEMU_PLUGIN_COND_GE,
                track_insn, insn_quantum, NULL);
        }
    } else {
        /* count (and eventually trap) once per tb */
        qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
            tb, QEMU_PLUGIN_INLINE_ADD_U64, track_insn, n_insns);
        qemu_plugin_register_vcpu_tb_exec_cond_cb(
            tb, every_insn_quantum,
            QEMU_PLUGIN_CB_NO_REGS, QEMU_PLUGIN_COND_GE,
            track_insn, insn_quantum, NULL);
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *udata)
{
    qemu_plugin_scoreboard_free(vcpus);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv)
{
    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);
        if (g_strcmp0(tokens[0], "ips") == 0) {
            insn_per_second = g_ascii_strtoull(tokens[1], NULL, 10);
            if (!insn_per_second && errno) {
                fprintf(stderr, "%s: couldn't parse %s (%s)\n",
                        __func__, tokens[1], g_strerror(errno));
                return -1;
            }

        } else if (g_strcmp0(tokens[0], "precise") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1],
                                        &precise_execution)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    vcpus = qemu_plugin_scoreboard_new(sizeof(vCPUTime));
    insn_quantum = insn_per_second / NUM_TIME_UPDATE_PER_SEC;

    time_handle = qemu_plugin_request_time_control();
    g_assert(time_handle);

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init);
    qemu_plugin_register_vcpu_idle_cb(id, vcpu_idle);
    qemu_plugin_register_vcpu_resume_cb(id, vcpu_resume);
    qemu_plugin_register_vcpu_exit_cb(id, vcpu_exit);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    return 0;
}
