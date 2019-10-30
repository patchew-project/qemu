/*
 * virtio-net Fuzzing Target
 *
 * Copyright Red Hat Inc., 2019
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "fuzz.h"
#include "fork_fuzz.h"
#include "qos_fuzz.h"
#include "tests/libqtest.h"
#include "tests/libqos/virtio-net.h"


static void virtio_net_fuzz_multi(QTestState *s,
        const unsigned char *Data, size_t Size)
{
    typedef struct vq_action {
        uint8_t queue;
        uint8_t length;
        uint8_t write;
        uint8_t next;
        bool kick;
    } vq_action;

    uint64_t req_addr[10];
    int reqi = 0;
    uint32_t free_head = 0;

    QGuestAllocator *t_alloc = fuzz_qos_alloc;

    QVirtioNet *net_if = fuzz_qos_obj;
    QVirtioDevice *dev = net_if->vdev;
    QVirtQueue *q;
    vq_action vqa;
    int iters = 0;
    while (true) {
        if (Size < sizeof(vqa)) {
            break;
        }
        memcpy(&vqa, Data, sizeof(vqa));
        vqa = *((vq_action *)Data);
        Data += sizeof(vqa);
        Size -= sizeof(vqa);

        q = net_if->queues[vqa.queue % 3];

        vqa.length = vqa.length >= Size ? Size :  vqa.length;

        req_addr[reqi] = guest_alloc(t_alloc, vqa.length);
        qtest_memwrite(s, req_addr[reqi], Data, vqa.length);
        if (iters == 0) {
            free_head = qvirtqueue_add(s, q, req_addr[reqi], vqa.length,
                    vqa.write, vqa.next);
        } else {
            qvirtqueue_add(s, q,
                    req_addr[reqi], vqa.length, vqa.write , vqa.next);
        }
        iters++;
        reqi++;
        if (iters == 10) {
            break;
        }
        Data += vqa.length;
        Size -= vqa.length;
    }
    if (iters) {
        qvirtqueue_kick(s, dev, q, free_head);
        qtest_clock_step_next(s);
        for (int i = 0; i < reqi; i++) {
            guest_free(t_alloc, req_addr[i]);
        }
    }
}

static int *sv;

static void *virtio_net_test_setup_socket(GString *cmd_line, void *arg)
{
    if (!sv) {
        sv = g_new(int, 2);
        int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sv);
        fcntl(sv[0], F_SETFL, O_NONBLOCK);
        g_assert_cmpint(ret, !=, -1);
    }
    g_string_append_printf(cmd_line, " -netdev socket,fd=%d,id=hs0 ", sv[1]);
    return arg;
}

static void virtio_net_fork_fuzz(QTestState *s,
        const unsigned char *Data, size_t Size)
{
    if (fork() == 0) {
        virtio_net_fuzz_multi(s, Data, Size);
        flush_events(s);
        _Exit(0);
    } else {
        wait(NULL);
    }
}

static void register_virtio_net_fuzz_targets(void)
{
    fuzz_add_qos_target(&(FuzzTarget){
                .name = "virtio-net-fork-fuzz",
                .description = "Fuzz the virtio-net virtual queues, forking"
                                "for each fuzz run",
                .pre_vm_init = &counter_shm_init,
                .pre_fuzz = &qos_init_path,
                .fuzz = virtio_net_fork_fuzz,},
                "virtio-net",
                &(QOSGraphTestOptions){.before = virtio_net_test_setup_socket}
                );
}

fuzz_target_init(register_virtio_net_fuzz_targets);
