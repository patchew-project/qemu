/*
 * Inter-VM Shared Memory Flat Device qtests
 *
 * SPDX-FileCopyrightText: 2023 Linaro Ltd.
 * SPDX-FileContributor: Gustavo Romero <gustavo.romero@linaro.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "ivshmem-utils.h"

#define IVSHMEM_FLAT_MMR_ADDR 0x400FF000
#define IVSHMEM_FLAT_SHM_ADDR 0x40100000
#define SHM_SIZE 131072 /* 128k */

static ServerThread thread;

uint32_t *shm_ptr;
char *shm_rel_path;
char *server_socket_path;

static void cleanup(void)
{
    if (shm_ptr) {
        munmap(shm_ptr, SHM_SIZE);
        shm_ptr = NULL;
    }

    if (shm_rel_path) {
        shm_unlink(shm_rel_path);
        shm_rel_path = NULL;
    }

    if (server_socket_path) {
        unlink(server_socket_path);
        server_socket_path = NULL;
    }
}

static void abort_handler(void *data)
{
    test_ivshmem_server_stop(&thread);
    cleanup();
}

/*
 * Check if exactly 1 positive pulse (low->high->low) on 'irq' qtest IRQ line
 * happens. N.B.: 'irq' must be intercepted using qtest_irq_intercept_* before
 * this function can be used on it. It returns 0 when pulse is detected,
 * otherwise 1.
 */
static int test_ivshmem_flat_irq_positive_pulse(QTestState *qts, int irq)
{
    uint64_t num_raises = 0;
    uint64_t num_lows = 0;
    int attempts = 0;

    while (attempts < 5) {
        num_raises = qtest_get_irq_raised_counter(qts, 0);
        if (num_raises) {
            num_lows = qtest_get_irq_lowered_counter(qts, 0);
            /* Check for exactly 1 raise and 1 low IRQ event */
            if (num_raises == num_lows && num_lows == 1) {
                return 0; /* Pulse detected */
            }
        }

	g_usleep(10000);
	attempts++;
    }

    g_message("%s: Timeout expired", __func__);
    return 1;
}

static inline uint32_t read_reg(QTestState *qts, enum Reg reg)
{
    uint32_t v;

    qtest_memread(qts, IVSHMEM_FLAT_MMR_ADDR + reg, &v, sizeof(v));

    return v;
}

static inline void write_reg(QTestState *qts, enum Reg reg, uint32_t v)
{
    qtest_memwrite(qts, IVSHMEM_FLAT_MMR_ADDR + reg, &v, sizeof(v));
}

/*
 * Setup a test VM with ivshmem-flat device attached, IRQ properly set, and
 * connected to the ivshmem-server.
 */
static QTestState *setup_vm(void)
{
    QTestState *qts;
    const char *cmd_line;

    /*
     * x-bus-address-{iomem,shmem} are just random addresses that don't conflict
     * with any other address in the lm3s6965evb machine. shmem-size used is
     * much smaller than the ivshmem server default (4 MiB) to save memory
     * resources when testing.
     */
    cmd_line = g_strdup_printf("-machine lm3s6965evb "
                               "-chardev socket,path=%s,id=ivshm "
                               "-device ivshmem-flat,chardev=ivshm,"
                               "x-irq-qompath='/machine/soc/v7m/nvic/unnamed-gpio-in[0]',"
                               "x-bus-address-iomem=%#x,"
                               "x-bus-address-shmem=%#x,"
                               "shmem-size=%d",
                               server_socket_path,
                               IVSHMEM_FLAT_MMR_ADDR,
                               IVSHMEM_FLAT_SHM_ADDR,
                               SHM_SIZE);

    qts = qtest_init(cmd_line);

    return qts;
}

static void test_ivshmem_flat_irq(void)
{
    QTestState *vm_state;
    uint16_t own_id;

    vm_state = setup_vm();

    qtest_irq_intercept_out_named(vm_state,
                                  "/machine/peripheral-anon/device[0]",
                                  "sysbus-irq");

    /* IVPOSTION has the device's own ID distributed by the ivshmem-server. */
    own_id = read_reg(vm_state, IVPOSITION);

    /* Make device notify itself. */
    write_reg(vm_state, DOORBELL, (own_id << 16) | 0 /* vector 0 */);

    /*
     * Check intercepted device's IRQ output line. 'sysbus-irq' was associated
     * to qtest IRQ 0 when intercepted and after self notification qtest IRQ 0
     * must be toggled by the device. The test fails if no toggling is detected.
     */
    g_assert(test_ivshmem_flat_irq_positive_pulse(vm_state,
                                                  0 /* qtest IRQ */) == 0);

    qtest_quit(vm_state);
}

static void test_ivshmem_flat_shm_write(void)
{
    QTestState *vm_state;
    int num_elements, i;
    uint32_t  *data;

    vm_state = setup_vm();

    /* Prepare test data with random values. */
    data = g_malloc(SHM_SIZE);
    num_elements = SHM_SIZE / sizeof(*data);
    for (i = 0; i < num_elements; i++) {
        data[i] = g_test_rand_int();
    }

    /*
     * Write test data to VM address IVSHMEM_FLAT_SHM_ADDR, where the shared
     * memory region is located.
     */
    qtest_memwrite(vm_state, IVSHMEM_FLAT_SHM_ADDR, data, SHM_SIZE);

    /*
     * Since the shared memory fd is mmapped into this test process VMA at
     * shm_ptr, every byte written by the VM in its shared memory region should
     * also be available in the test process via shm_ptr. Thus, data in shm_ptr
     * is compared back against the original test data.
     */
    for (i = 0; i < num_elements; i++) {
        g_assert_cmpint(shm_ptr[i], ==, data[i]);
    }

    qtest_quit(vm_state);
}

static void test_ivshmem_flat_shm_read(void)
{
    QTestState *vm_state;
    int num_elements, i;
    uint32_t  *data;
    uint32_t v;

    vm_state = setup_vm();

    /* Prepare test data with random values. */
    data = g_malloc(SHM_SIZE);
    num_elements = SHM_SIZE / sizeof(*data);
    for (i = 0; i < num_elements; i++) {
        data[i] = g_test_rand_int();
    }

    /*
     * Copy test data to the shared memory region so it can be read from the VM
     * (IVSHMEM_FLAT_SHM_ADDR location).
     */
    memcpy(shm_ptr, data, SHM_SIZE);

    /* Check data */
    for (i = 0; i < num_elements; i++) {
        qtest_memread(vm_state, IVSHMEM_FLAT_SHM_ADDR + i * sizeof(v), &v,
                      sizeof(v));
        g_assert_cmpint(v, ==, data[i]);
    }

    qtest_quit(vm_state);
}

static void test_ivshmem_flat_shm_pair(void)
{
    QTestState *vm0_state, *vm1_state;
    uint16_t vm0_peer_id, vm1_peer_id;
    int num_elements, i;
    uint32_t  *data;
    uint32_t v;

    vm0_state = setup_vm();
    vm1_state = setup_vm();

    /* Get peer ID for the VM so it can be used for one notify each other. */
    vm0_peer_id = read_reg(vm0_state, IVPOSITION);
    vm1_peer_id = read_reg(vm1_state, IVPOSITION);

    /* Observe vm1 IRQ output line first. */
    qtest_irq_intercept_out_named(vm1_state,
                                  "/machine/peripheral-anon/device[0]",
                                  "sysbus-irq");

    /* Notify (interrupt) VM1 from VM0. */
    write_reg(vm0_state, DOORBELL, (vm1_peer_id << 16) | 0 /* vector 0 */);

    /* Check if VM1 IRQ output line is toggled after notification from VM0. */
    g_assert(test_ivshmem_flat_irq_positive_pulse(vm1_state,
                                                  0 /* qtest IRQ */) == 0);

    /* Secondly, observe VM0 IRQ output line first. */
    qtest_irq_intercept_out_named(vm0_state,
                                  "/machine/peripheral-anon/device[0]",
                                  "sysbus-irq");

    /* ... and do the opposite: notify (interrupt) VM0 from VM1. */
    write_reg(vm1_state, DOORBELL, (vm0_peer_id << 16) | 0 /* vector 0 */);

    /* Check if VM0 IRQ output line is toggled after notification from VM0. */
    g_assert(test_ivshmem_flat_irq_positive_pulse(vm0_state,
                                                  0 /* qtest IRQ */) == 0);

    /* Prepare test data with random values. */
    data = g_malloc(SHM_SIZE);
    num_elements = SHM_SIZE / sizeof(*data);
    for (i = 0; i < num_elements; i++) {
        data[i] = g_test_rand_int();
    }

    /* Write test data on VM0. */
    qtest_memwrite(vm0_state, IVSHMEM_FLAT_SHM_ADDR, data, SHM_SIZE);

    /* Check test data on VM1. */
    for (i = 0; i < num_elements; i++) {
        qtest_memread(vm1_state, IVSHMEM_FLAT_SHM_ADDR + i * sizeof(v), &v,
                      sizeof(v));
        g_assert_cmpint(v, ==, data[i]);
    }

    /* Prepare new test data with random values. */
    for (i = 0; i < num_elements; i++) {
        data[i] = g_test_rand_int();
    }

    /* Write test data on VM1. */
    qtest_memwrite(vm1_state, IVSHMEM_FLAT_SHM_ADDR, data, SHM_SIZE);

    /* Check test data on VM0. */
    for (i = 0; i < num_elements; i++) {
        qtest_memread(vm0_state, IVSHMEM_FLAT_SHM_ADDR + i * sizeof(v), &v,
                      sizeof(v));
        g_assert_cmpint(v, ==, data[i]);
    }

    qtest_quit(vm0_state);
    qtest_quit(vm1_state);
}

int main(int argc, char *argv[])
{
    int shm_fd, r;

    g_test_init(&argc, &argv, NULL);

    if (!qtest_has_machine("lm3s6965evb")) {
        g_test_skip("Machine Stellaris (lm3s6965evb) not found, "
                    "skipping ivshmem-flat device test.");
        return 0;
    }

    /* If test fails, stop server, cleanup socket and shm files. */
    qtest_add_abrt_handler(abort_handler, NULL);

    shm_rel_path = mktempshm(SHM_SIZE, &shm_fd);
    g_assert(shm_rel_path);

    /*
     * Map shm to this test's VMA so it's possible to read/write from/to it. For
     * VMs with the ivhsmem-flat device attached, this region will also be
     * mapped in their own memory layout, at IVSHMEM_FLAT_SHM_ADDR (default).
     */
    shm_ptr = mmap(0, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    g_assert(shm_ptr != MAP_FAILED);

    server_socket_path = mktempsocket();
    /* It never fails, so no assert(). */

    /*
     * Currently, ivshmem-flat device only supports notification via 1 vector,
     * i.e. vector 0.
     */
    test_ivshmem_server_start(&thread, server_socket_path, shm_rel_path, 1);

    /* Register tests. */
    qtest_add_func("/ivshmem-flat/irq", test_ivshmem_flat_irq);
    qtest_add_func("/ivshmem-flat/shm-write", test_ivshmem_flat_shm_write);
    qtest_add_func("/ivshmem-flat/shm-read", test_ivshmem_flat_shm_read);
    qtest_add_func("/ivshmem-flat/pair", test_ivshmem_flat_shm_pair);

    r = g_test_run();

    test_ivshmem_server_stop(&thread);
    cleanup();

    return r;
}
