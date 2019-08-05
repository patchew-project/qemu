#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "exec/memory.h"
#include "sysemu/sysemu.h"
#include "qemu/main-loop.h"

#include "hw/virtio/virtio-net.h"
#include "hw/virtio/virtio.h"
#include "tests/libqos/virtio-net.h"
#include "fuzzer_hooks.h"
#include "snapshotting.h"

#include "fuzz.h"
#include "qos_fuzz.h"

#define MAX_INPUT_BUFFERS 10

typedef struct vq_action {
    uint8_t queue;
    uint8_t length;
    uint8_t write;
    uint8_t next;
    bool kick;
} vq_action;

static void virtio_net_ctrl_fuzz(const unsigned char *Data, size_t Size)
{
    uint64_t req_addr[10];
    int reqi = 0;
    uint32_t free_head;

    QGuestAllocator *t_alloc = qos_alloc;

    QVirtioNet *net_if = qos_obj;
    QVirtioDevice *dev = net_if->vdev;
    QVirtQueue *q;
    vq_action vqa;
    int iters = 0;
    while (true) {
        if (Size < sizeof(vqa)) {
            break;
        }
        vqa = *((vq_action *)Data);
        Data += sizeof(vqa);
        Size -= sizeof(vqa);

        q = net_if->queues[2];

        vqa.length = vqa.length >= Size ? Size :  vqa.length;

        req_addr[reqi] = guest_alloc(t_alloc, vqa.length);
        memwrite(req_addr[reqi], Data, vqa.length);
        if (iters == 0) {
            free_head = qvirtqueue_add(q, req_addr[reqi], vqa.length,
                    vqa.write, vqa.next);
        } else {
            qvirtqueue_add(q, req_addr[reqi], vqa.length, vqa.write , vqa.next);
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
        qvirtqueue_kick(dev, q, free_head);
        /* qtest_clock_step_next(s); */
        main_loop_wait(false);
        for (int i = 0; i < reqi; i++) {
            guest_free(t_alloc, req_addr[i]);
        }
    }
    qtest_clear_rxbuf(s);
    qos_object_queue_destroy(qos_obj);
}

static void virtio_net_ctrl_fuzz_multi(const unsigned char *Data, size_t Size)
{
    uint64_t req_addr[10];
    int reqi = 0;
    uint32_t free_head;

    QGuestAllocator *t_alloc = qos_alloc;

    QVirtioNet *net_if = qos_obj;
    QVirtioDevice *dev = net_if->vdev;
    QVirtQueue *q;
    vq_action vqa;
    int iters = 0;
    while (Size >= sizeof(vqa)) {
        vqa = *((vq_action *)Data);
        Data += sizeof(vqa);
        Size -= sizeof(vqa);
        if (vqa.kick && free_head) {
            qvirtqueue_kick(dev, q, free_head);
            qtest_clock_step_next(s);
            main_loop_wait(false);
            for (int i = 0; i < reqi; i++) {
                guest_free(t_alloc, req_addr[i]);
            }
            reqi = 0;
        } else {
            q = net_if->queues[2];

            vqa.length = vqa.length >= Size ? Size :  vqa.length;

            req_addr[reqi] = guest_alloc(t_alloc, vqa.length);
            memwrite(req_addr[reqi], Data, vqa.length);
            if (iters == 0) {
                free_head = qvirtqueue_add(q, req_addr[reqi], vqa.length,
                        vqa.write, vqa.next);
            } else {
                qvirtqueue_add(q, req_addr[reqi], vqa.length, vqa.write,
                        vqa.next) ;
            }
            iters++;
            reqi++;
            if (iters == 10) {
                break;
            }
            Data += vqa.length;
            Size -= vqa.length;
        }
    }
    qtest_clear_rxbuf(s);
    qos_object_queue_destroy(qos_obj);
}

int *sv;
static void virtio_net_tx_fuzz(const unsigned char *Data, size_t Size)
{
    uint64_t req_addr[10];
    int reqi = 0;
    uint32_t free_head;

    QGuestAllocator *t_alloc = qos_alloc;

    QVirtioNet *net_if = qos_obj;
    QVirtioDevice *dev = net_if->vdev;
    QVirtQueue *q;
    vq_action vqa;
    int iters = 0;
    while (true) {
        if (Size < sizeof(vqa)) {
            break;
        }
        vqa = *((vq_action *)Data);
        Data += sizeof(vqa);
        Size -= sizeof(vqa);

        q = net_if->queues[1];

        vqa.length = vqa.length >= Size ? Size :  vqa.length;

        req_addr[reqi] = guest_alloc(t_alloc, vqa.length);
        memwrite(req_addr[reqi], Data, vqa.length);
        if (iters == 0) {
            free_head = qvirtqueue_add(q, req_addr[reqi], vqa.length,
                    vqa.write, vqa.next);
        } else {
            qvirtqueue_add(q, req_addr[reqi], vqa.length, vqa.write, vqa.next);
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
        qvirtqueue_kick(dev, q, free_head);
        qtest_clock_step_next(s);
        main_loop_wait(false);
        for (int i = 0; i < reqi; i++) {
            guest_free(t_alloc, req_addr[i]);
        }
    }
    qtest_clear_rxbuf(s);
    qos_object_queue_destroy(qos_obj);
}

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

static void fuzz_fork(const unsigned char *Data, size_t Size)
{
    if (fork() == 0) {
        main_loop_wait(false);
        virtio_net_ctrl_fuzz(Data, Size);
        counter_shm_store();
        _Exit(0);
    } else {
        wait(NULL);
        counter_shm_load();
    }
}

static void fork_pre_main(void)
{
    qos_setup();
    counter_shm_init();
}

static void register_virtio_net_fuzz_targets(void)
{
    QOSGraphTestOptions opts = {
        .before = virtio_net_test_setup_socket,
    };
    FuzzTarget fuzz_opts = {
        .pre_main = qos_setup,
        .pre_save_state = NULL,
        .save_state = NULL,
        .reset = &reboot,
        .pre_fuzz = &qos_init_path,
        .fuzz = &virtio_net_ctrl_fuzz,
        .post_fuzz = NULL,
    };
    fuzz_add_qos_target("virtio-net-ctrl-fuzz", "virtio-net ctrl virtqueue \
            fuzzer", "virtio-net", &opts, &fuzz_opts);

    fuzz_opts.fuzz = &virtio_net_ctrl_fuzz_multi;
    fuzz_add_qos_target("virtio-net-ctrl-multi-fuzz", "virtio-net ctrl\
             virtqueue  fuzzer with multiple kicks", "virtio-net", &opts,
            &fuzz_opts);

    fuzz_opts.fuzz = &virtio_net_tx_fuzz;
    fuzz_add_qos_target("virtio-net-tx-fuzz", "virtio-net tx virtqueue fuzzer",
            "virtio-net", &opts, &fuzz_opts);

    fuzz_opts.pre_main = &fork_pre_main;
    fuzz_opts.pre_save_state = &qos_init_path;
    fuzz_opts.reset = NULL;
    fuzz_opts.pre_fuzz = NULL;
    fuzz_opts.fuzz = &fuzz_fork;
    fuzz_add_qos_target("virtio-net-fork", "virtio-net tx virtqueue",
            "virtio-net", &opts, &fuzz_opts);

}

fuzz_target_init(register_virtio_net_fuzz_targets);
