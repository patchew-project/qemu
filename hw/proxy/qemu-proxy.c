/*
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "io/mpqemu-link.h"
#include "exec/memory.h"
#include "exec/cpu-common.h"
#include "exec/address-spaces.h"
#include "hw/pci/pci.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qstring.h"
#include "sysemu/runstate.h"
#include "hw/proxy/qemu-proxy.h"
#include "hw/proxy/memory-sync.h"
#include "qom/object.h"
#include "qemu/event_notifier.h"
#include "sysemu/kvm.h"
#include "util/event_notifier-posix.c"
#include "hw/boards.h"
#include "include/qemu/log.h"
#include "io/channel.h"
#include "migration/qemu-file-types.h"
#include "qapi/error.h"
#include "io/channel-util.h"
#include "migration/qemu-file-channel.h"
#include "migration/qemu-file.h"
#include "migration/migration.h"
#include "migration/vmstate.h"

QEMUTimer *hb_timer;
static void pci_proxy_dev_realize(PCIDevice *dev, Error **errp);
static void setup_irqfd(PCIProxyDev *dev);
static void pci_dev_exit(PCIDevice *dev);
static void start_broadcast_timer(void);
static void stop_broadcast_timer(void);
static void childsig_handler(int sig, siginfo_t *siginfo, void *ctx);
static void broadcast_init(void);
static int config_op_send(PCIProxyDev *dev, uint32_t addr, uint32_t *val, int l,
                          unsigned int op);

#define PAGE_SIZE qemu_real_host_page_size
uint8_t *mig_data;

static void childsig_handler(int sig, siginfo_t *siginfo, void *ctx)
{
    /* TODO: Add proper handler. */
    printf("Child (pid %d) is dead? Signal is %d, Exit code is %d.\n",
           siginfo->si_pid, siginfo->si_signo, siginfo->si_code);
}

static void remote_ping_handler(void *opaque)
{
    PCIProxyDev *pdev = opaque;

    if (!event_notifier_test_and_clear(&pdev->en_ping)) {
        /*
         * TODO: Is retry needed? Add the handling of the
         * non-responsive process. How its done in case
         * of managed process?
         */
        printf("No reply from remote process, pid %d\n", pdev->remote_pid);
        event_notifier_cleanup(&pdev->en_ping);
    }
}

static void broadcast_msg(void)
{
    MPQemuMsg msg;
    PCIProxyDev *entry;

    QLIST_FOREACH(entry, &proxy_dev_list.devices, next) {
        if (event_notifier_get_fd(&entry->en_ping) == -1) {
            continue;
        }

        memset(&msg, 0, sizeof(MPQemuMsg));

        msg.num_fds = 1;
        msg.cmd = PROXY_PING;
        msg.bytestream = 0;
        msg.size = 0;
        msg.fds[0] = event_notifier_get_fd(&entry->en_ping);

        mpqemu_msg_send(&msg, entry->mpqemu_link->com);
    }
}

static void broadcast_init(void)
{
    PCIProxyDev *entry;

    QLIST_FOREACH(entry, &proxy_dev_list.devices, next) {
        event_notifier_init(&entry->en_ping, 0);
        qemu_set_fd_handler(event_notifier_get_fd(&entry->en_ping),
                            remote_ping_handler, NULL, entry);
    }
}

#define NOP_INTERVAL 1000000

static void remote_ping(void *opaque)
{
    broadcast_msg();
    timer_mod(hb_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + NOP_INTERVAL);
}

static void start_broadcast_timer(void)
{
    hb_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                            remote_ping,
                                            &proxy_dev_list);
    timer_mod(hb_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + NOP_INTERVAL);

}

static void stop_broadcast_timer(void)
{
    timer_del(hb_timer);
    timer_free(hb_timer);
}

static void set_sigchld_handler(void)
{
    struct sigaction sa_sigterm;
    memset(&sa_sigterm, 0, sizeof(sa_sigterm));
    sa_sigterm.sa_sigaction = childsig_handler;
    sa_sigterm.sa_flags = SA_SIGINFO | SA_NOCLDWAIT | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_sigterm, NULL);
}

static void probe_pci_info(PCIDevice *dev)
{
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(dev);
    DeviceClass *dc = DEVICE_CLASS(pc);
    PCIProxyDev *pdev = PCI_PROXY_DEV(dev);
    MPQemuLinkState *mpqemu_link = pdev->mpqemu_link;
    MPQemuMsg msg, ret;
    uint32_t orig_val, new_val, class;
    uint8_t type;
    int i, size;
    char *name;

    memset(&msg, 0, sizeof(MPQemuMsg));
    msg.bytestream = 0;
    msg.size = 0;
    msg.cmd = GET_PCI_INFO;
    mpqemu_msg_send(&msg, mpqemu_link->com);

    mpqemu_msg_recv(&ret, mpqemu_link->com);

    pc->vendor_id = ret.data1.ret_pci_info.vendor_id;
    pc->device_id = ret.data1.ret_pci_info.device_id;
    pc->class_id = ret.data1.ret_pci_info.class_id;
    pc->subsystem_id = ret.data1.ret_pci_info.subsystem_id;

    config_op_send(pdev, 11, &class, 1, PCI_CONFIG_READ);
    switch (class) {
    case PCI_BASE_CLASS_BRIDGE:
        set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
        break;
    case PCI_BASE_CLASS_STORAGE:
        set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
        break;
    case PCI_BASE_CLASS_NETWORK:
        set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
        break;
    case PCI_BASE_CLASS_INPUT:
        set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
        break;
    case PCI_BASE_CLASS_DISPLAY:
        set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
        break;
    case PCI_BASE_CLASS_PROCESSOR:
        set_bit(DEVICE_CATEGORY_CPU, dc->categories);
        break;
    default:
        set_bit(DEVICE_CATEGORY_MISC, dc->categories);
        break;
    }

    for (i = 0; i < 6; i++) {
        config_op_send(pdev, 0x10 + (4 * i), &orig_val, 4, PCI_CONFIG_READ);
        new_val = 0xffffffff;
        config_op_send(pdev, 0x10 + (4 * i), &new_val, 4, PCI_CONFIG_WRITE);
        config_op_send(pdev, 0x10 + (4 * i), &new_val, 4, PCI_CONFIG_READ);
        size = (~(new_val & 0xFFFFFFF0)) + 1;
        config_op_send(pdev, 0x10 + (4 * i), &orig_val, 4, PCI_CONFIG_WRITE);
        type = (new_val & 0x1) ?
                   PCI_BASE_ADDRESS_SPACE_IO : PCI_BASE_ADDRESS_SPACE_MEMORY;

        if (size) {
            pdev->region[i].dev = pdev;
            pdev->region[i].present = true;
            if (type == PCI_BASE_ADDRESS_SPACE_MEMORY) {
                pdev->region[i].memory = true;
            }
            name = g_strdup_printf("bar-region-%d", i);
            memory_region_init_io(&pdev->region[i].mr, OBJECT(pdev),
                                  &proxy_default_ops, &pdev->region[i],
                                  name, size);
            pci_register_bar(dev, i, type, &pdev->region[i].mr);
            g_free(name);
        }
    }
}

static void proxy_ready(PCIDevice *dev)
{
    PCIProxyDev *pdev = PCI_PROXY_DEV(dev);

    setup_irqfd(pdev);
    probe_pci_info(dev);
    set_sigchld_handler();
    broadcast_init();
    start_broadcast_timer();
}

static int set_remote_opts(PCIDevice *dev, QDict *qdict, unsigned int cmd)
{
    QString *qstr;
    MPQemuMsg msg;
    PCIProxyDev *pdev;
    const char *str;
    uint32_t reply = 0;
    int rc = -EINVAL;
    int wait;

    pdev = PCI_PROXY_DEV(dev);

    qstr = qobject_to_json(QOBJECT(qdict));
    str = qstring_get_str(qstr);

    memset(&msg, 0, sizeof(MPQemuMsg));

    msg.data2 = (uint8_t *)(str);
    msg.cmd = cmd;
    msg.bytestream = 1;
    msg.size = qstring_get_length(qstr) + 1;


    wait = eventfd(0, EFD_NONBLOCK);
    msg.num_fds = 1;
    msg.fds[0] = wait;

    mpqemu_msg_send(&msg, pdev->mpqemu_link->com);

    reply = (uint32_t)wait_for_remote(wait);
    close(wait);

    /* TODO: Add proper handling if remote did not set options. */
    if (reply == REMOTE_OK) {
        rc = 0;
    }

    qobject_unref(qstr);

    return rc;
}

static int add_argv(char *opts_str, char **argv, int argc)
{
    int max_args = 64;

    if (argc < max_args - 1) {
        argv[argc++] = opts_str;
        argv[argc] = 0;
    } else {
        return 0;
    }

    return argc;
}

static int make_argv(char *opts_str, char **argv, int argc)
{
    int max_args = 64;

    char *p2 = strtok(opts_str, " ");
    while (p2 && argc < max_args - 1) {
        argv[argc++] = p2;
        p2 = strtok(0, " ");
    }
    argv[argc] = 0;

    return argc;
}

static int remote_spawn(PCIProxyDev *pdev, const char *opts,
                        const char *exec_name, Error **errp)
{
    pid_t rpid;
    int fd[2], mmio[2];
    Error *local_error = NULL;
    char *argv[64];
    int argc = 0;
    char *sfd;
    char *exec_dir;
    int rc = -EINVAL;
    struct timeval timeout = {.tv_sec = 10, .tv_usec = 0};

    if (pdev->managed) {
        /* Child is forked by external program (such as libvirt). */
        error_setg(errp, "Remote processed is managed and launched by external program");
        return rc;
    }

    if (!exec_name) {
        error_setg(errp, "The remote exec name is NULL.");
        return rc;
    }

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) ||
        socketpair(AF_UNIX, SOCK_STREAM, 0, mmio)) {
        error_setg(errp, "Unable to create unix socket.");
        return rc;
    }
    exec_dir = g_strdup_printf("%s/%s", qemu_get_exec_dir(), exec_name);
    argc = add_argv(exec_dir, argv, argc);
    sfd = g_strdup_printf("%d", fd[1]);
    argc = add_argv(sfd, argv, argc);
    sfd = g_strdup_printf("%d", mmio[1]);
    argc = add_argv(sfd, argv, argc);
    argc = make_argv((char *)opts, argv, argc);

    /* TODO: Restrict the forked process' permissions and capabilities. */
    rpid = qemu_fork(&local_error);

    if (rpid == -1) {
        error_setg(errp, "Unable to spawn emulation program.");
        close(fd[0]);
        close(fd[1]);
        close(mmio[0]);
        close(mmio[1]);
        return rc;
    }

    if (rpid == 0) {
        close(fd[0]);
        close(mmio[0]);

        rc = execv(argv[0], (char *const *)argv);
        exit(1);
    }
    pdev->remote_pid = rpid;
    pdev->socket = fd[0];
    pdev->mmio_sock = mmio[0];

    rc = setsockopt(mmio[0], SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
                   sizeof(timeout));
    if (rc < 0) {
        close(fd[0]);
        close(mmio[0]);

        error_setg(errp, "Unable to set timeout for socket");

        return rc;
    }

    return 0;
}

static int get_proxy_sock(PCIDevice *dev)
{
    PCIProxyDev *pdev;

    pdev = PCI_PROXY_DEV(dev);

    return pdev->socket;
}

static void set_proxy_sock(PCIDevice *dev, int socket)
{
    PCIProxyDev *pdev;

    pdev = PCI_PROXY_DEV(dev);

    pdev->socket = socket;
    pdev->managed = true;
}

static int config_op_send(PCIProxyDev *dev, uint32_t addr, uint32_t *val, int l,
                          unsigned int op)
{
    MPQemuMsg msg;
    struct conf_data_msg conf_data;
    int wait;

    memset(&msg, 0, sizeof(MPQemuMsg));
    conf_data.addr = addr;
    conf_data.val = (op == PCI_CONFIG_WRITE) ? *val : 0;
    conf_data.l = l;

    msg.data2 = (uint8_t *)&conf_data;
    if (!msg.data2) {
        return -ENOMEM;
    }

    msg.size = sizeof(conf_data);
    msg.cmd = op;
    msg.bytestream = 1;

    if (op == PCI_CONFIG_WRITE) {
        msg.num_fds = 0;
    } else {
        /* TODO: Dont create fd each time for send. */
        wait = GET_REMOTE_WAIT;
        msg.num_fds = 1;
        msg.fds[0] = wait;
    }

    mpqemu_msg_send(&msg, dev->mpqemu_link->com);

    if (op == PCI_CONFIG_READ) {
        *val = (uint32_t)wait_for_remote(wait);
        PUT_REMOTE_WAIT(wait);
    }

    return 0;
}

static uint32_t pci_proxy_read_config(PCIDevice *d, uint32_t addr, int len)
{
    uint32_t val;

    (void)pci_default_read_config(d, addr, len);

    config_op_send(PCI_PROXY_DEV(d), addr, &val, len, PCI_CONFIG_READ);

    return val;
}

static void pci_proxy_write_config(PCIDevice *d, uint32_t addr, uint32_t val,
                                   int l)
{
    pci_default_write_config(d, addr, val, l);

    config_op_send(PCI_PROXY_DEV(d), addr, &val, l, PCI_CONFIG_WRITE);
}

static void proxy_device_reset(DeviceState *dev)
{
    PCIProxyDev *pdev = PCI_PROXY_DEV(dev);
    MPQemuMsg msg;
    int wait = -1;

    memset(&msg, 0, sizeof(MPQemuMsg));

    msg.bytestream = 0;
    msg.size = sizeof(msg.data1);
    msg.cmd = DEVICE_RESET;

    wait = eventfd(0, EFD_CLOEXEC);
    msg.num_fds = 1;
    msg.fds[0] = wait;

    mpqemu_msg_send(&msg, pdev->mpqemu_link->com);

    wait_for_remote(wait);
    close(wait);
}

static void pci_proxy_dev_inst_init(Object *obj)
{
    PCIProxyDev *dev = PCI_PROXY_DEV(obj);

    dev->mem_init = false;
}

typedef struct {
    QEMUFile *rem;
    PCIProxyDev *dev;
} proxy_mig_data;

static void *proxy_mig_out(void *opaque)
{
    proxy_mig_data *data = opaque;
    PCIProxyDev *dev = data->dev;
    uint8_t byte;
    uint64_t data_size = PAGE_SIZE;

    mig_data = g_malloc(data_size);

    while (true) {
        byte = qemu_get_byte(data->rem);

        if (qemu_file_get_error(data->rem)) {
            break;
        }

        mig_data[dev->migsize++] = byte;
        if (dev->migsize == data_size) {
            data_size += PAGE_SIZE;
            mig_data = g_realloc(mig_data, data_size);
        }
    }

    return NULL;
}

static int proxy_pre_save(void *opaque)
{
    PCIProxyDev *pdev = opaque;
    proxy_mig_data *mig_data;
    QEMUFile *f_remote;
    MPQemuMsg msg = {0};
    QemuThread thread;
    Error *err = NULL;
    QIOChannel *ioc;
    uint64_t size;
    int fd[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd)) {
        return -1;
    }

    ioc = qio_channel_new_fd(fd[0], &err);
    if (err) {
        error_report_err(err);
        return -1;
    }

    qio_channel_set_name(QIO_CHANNEL(ioc), "PCIProxyDevice-mig");

    f_remote = qemu_fopen_channel_input(ioc);

    pdev->migsize = 0;

    mig_data = g_malloc0(sizeof(proxy_mig_data));
    mig_data->rem = f_remote;
    mig_data->dev = pdev;

    qemu_thread_create(&thread, "Proxy MIG_OUT", proxy_mig_out, mig_data,
                       QEMU_THREAD_DETACHED);

    msg.cmd = START_MIG_OUT;
    msg.bytestream = 0;
    msg.num_fds = 2;
    msg.fds[0] = fd[1];
    msg.fds[1] = GET_REMOTE_WAIT;

    mpqemu_msg_send(&msg, pdev->mpqemu_link->com);
    size = wait_for_remote(msg.fds[1]);
    PUT_REMOTE_WAIT(msg.fds[1]);

    assert(size != ULLONG_MAX);

    /*
     * migsize is being update by a separate thread. Using volatile to
     * instruct the compiler to fetch the value of this variable from
     * memory during every read
     */
    while (*((volatile uint64_t *)&pdev->migsize) < size) {
    }

    qemu_file_shutdown(f_remote);

    qemu_fclose(f_remote);
    close(fd[1]);

    return 0;
}

static int proxy_post_save(void *opaque)
{
    MigrationState *ms = migrate_get_current();
    PCIProxyDev *pdev = opaque;
    uint64_t pos = 0;

    while (pos < pdev->migsize) {
        qemu_put_byte(ms->to_dst_file, mig_data[pos]);
        pos++;
    }

    qemu_fflush(ms->to_dst_file);

    return 0;
}

static int proxy_post_load(void *opaque, int version_id)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    PCIProxyDev *pdev = opaque;
    QEMUFile *f_remote;
    MPQemuMsg msg = {0};
    Error *err = NULL;
    QIOChannel *ioc;
    uint64_t size;
    uint8_t byte;
    int fd[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd)) {
        return -1;
    }

    ioc = qio_channel_new_fd(fd[0], &err);
    if (err) {
        error_report_err(err);
        return -1;
    }

    qio_channel_set_name(QIO_CHANNEL(ioc), "proxy-migration-channel");

    f_remote = qemu_fopen_channel_output(ioc);

    msg.cmd = START_MIG_IN;
    msg.bytestream = 0;
    msg.num_fds = 1;
    msg.fds[0] = fd[1];

    mpqemu_msg_send(&msg, pdev->mpqemu_link->com);

    size = pdev->migsize;

    while (size) {
        byte = qemu_get_byte(mis->from_src_file);
        qemu_put_byte(f_remote, byte);
        size--;
    }

    qemu_fflush(f_remote);
    qemu_fclose(f_remote);

    close(fd[1]);

    return 0;
}

const VMStateDescription vmstate_pci_proxy_device = {
    .name = "PCIProxyDevice",
    .version_id = 2,
    .minimum_version_id = 1,
    .pre_save = proxy_pre_save,
    .post_save = proxy_post_save,
    .post_load = proxy_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_dev, PCIProxyDev),
        VMSTATE_UINT64(migsize, PCIProxyDev),
        VMSTATE_END_OF_LIST()
    }
};

static void pci_proxy_dev_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize = pci_proxy_dev_realize;
    k->exit = pci_dev_exit;
    k->config_read = pci_proxy_read_config;
    k->config_write = pci_proxy_write_config;

    dc->reset = proxy_device_reset;
    dc->vmsd = &vmstate_pci_proxy_device;
}

static const TypeInfo pci_proxy_dev_type_info = {
    .name          = TYPE_PCI_PROXY_DEV,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIProxyDev),
    .instance_init = pci_proxy_dev_inst_init,
    .class_size    = sizeof(PCIProxyDevClass),
    .class_init    = pci_proxy_dev_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pci_proxy_dev_register_types(void)
{
    type_register_static(&pci_proxy_dev_type_info);
}

type_init(pci_proxy_dev_register_types)

static void proxy_intx_update(PCIDevice *pci_dev)
{
    PCIProxyDev *dev = PCI_PROXY_DEV(pci_dev);
    PCIINTxRoute route;
    int pin = pci_get_byte(pci_dev->config + PCI_INTERRUPT_PIN) - 1;

    if (dev->irqfd.fd) {
        dev->irqfd.flags = KVM_IRQFD_FLAG_DEASSIGN;
        (void) kvm_vm_ioctl(kvm_state, KVM_IRQFD, &dev->irqfd);
        memset(&dev->irqfd, 0, sizeof(struct kvm_irqfd));
    }

    route = pci_device_route_intx_to_irq(pci_dev, pin);

    dev->irqfd.fd = event_notifier_get_fd(&dev->intr);
    dev->irqfd.resamplefd = event_notifier_get_fd(&dev->resample);
    dev->irqfd.gsi = route.irq;
    dev->irqfd.flags |= KVM_IRQFD_FLAG_RESAMPLE;
    (void) kvm_vm_ioctl(kvm_state, KVM_IRQFD, &dev->irqfd);
}

static void setup_irqfd(PCIProxyDev *dev)
{
    PCIDevice *pci_dev = PCI_DEVICE(dev);
    MPQemuMsg msg;

    event_notifier_init(&dev->intr, 0);
    event_notifier_init(&dev->resample, 0);

    memset(&msg, 0, sizeof(MPQemuMsg));
    msg.cmd = SET_IRQFD;
    msg.num_fds = 2;
    msg.fds[0] = event_notifier_get_fd(&dev->intr);
    msg.fds[1] = event_notifier_get_fd(&dev->resample);
    msg.data1.set_irqfd.intx =
        pci_get_byte(pci_dev->config + PCI_INTERRUPT_PIN) - 1;
    msg.size = sizeof(msg.data1);

    mpqemu_msg_send(&msg, dev->mpqemu_link->com);

    memset(&dev->irqfd, 0, sizeof(struct kvm_irqfd));

    proxy_intx_update(pci_dev);

    pci_device_set_intx_routing_notifier(pci_dev, proxy_intx_update);
}

static void init_proxy(PCIDevice *dev, char *command, char *exec_name,
                       bool need_spawn, Error **errp)
{
    PCIProxyDev *pdev = PCI_PROXY_DEV(dev);
    Error *local_error = NULL;

    if (!pdev->managed) {
        if (need_spawn) {
            if (remote_spawn(pdev, command, exec_name, &local_error)) {
                error_propagate(errp, local_error);
                return;
            }
        }
    } else {
        pdev->remote_pid = atoi(pdev->rid);
        if (pdev->remote_pid == -1) {
            error_setg(errp, "Remote PID is -1");
            return;
        }
    }

    pdev->mpqemu_link = mpqemu_link_create();

    if (!pdev->mpqemu_link) {
        error_setg(errp, "Failed to create proxy link");
        return;
    }

    mpqemu_init_channel(pdev->mpqemu_link, &pdev->mpqemu_link->com,
                        pdev->socket);

    mpqemu_init_channel(pdev->mpqemu_link, &pdev->mpqemu_link->mmio,
                        pdev->mmio_sock);

    if (!pdev->mem_init) {
        pdev->mem_init = true;
        configure_memory_sync(pdev->sync, pdev->mpqemu_link);
    }
}

static void proxy_vm_state_change(void *opaque, int running, RunState state)
{
    PCIProxyDev *dev = opaque;
    MPQemuMsg msg = { 0 };
    int wait = -1;

    msg.cmd = RUNSTATE_SET;
    msg.bytestream = 0;
    msg.size = sizeof(msg.data1);
    msg.data1.runstate.state = state;

    wait = eventfd(0, EFD_CLOEXEC);
    msg.num_fds = 1;
    msg.fds[0] = wait;

    mpqemu_msg_send(&msg, dev->mpqemu_link->com);

    wait_for_remote(wait);
    close(wait);
}

static void pci_proxy_dev_realize(PCIDevice *device, Error **errp)
{
    PCIProxyDev *dev = PCI_PROXY_DEV(device);
    PCIProxyDevClass *k = PCI_PROXY_DEV_GET_CLASS(dev);
    uint8_t *pci_conf = device->config;
    Error *local_err = NULL;

    pci_conf[PCI_LATENCY_TIMER] = 0xff;
    pci_conf[PCI_INTERRUPT_PIN] = 0x01;

    if (k->realize) {
        k->realize(dev, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
        }
    }

    dev->vmcse = qemu_add_vm_change_state_handler(proxy_vm_state_change, dev);

    dev->set_proxy_sock = set_proxy_sock;
    dev->get_proxy_sock = get_proxy_sock;
    dev->init_proxy = init_proxy;
    dev->sync = REMOTE_MEM_SYNC(object_new(TYPE_MEMORY_LISTENER));

    dev->set_remote_opts = set_remote_opts;
    dev->proxy_ready = proxy_ready;
}

static void pci_dev_exit(PCIDevice *pdev)
{
    PCIProxyDev *entry, *sentry;
    PCIProxyDev *dev = PCI_PROXY_DEV(pdev);

    stop_broadcast_timer();

    QLIST_FOREACH_SAFE(entry, &proxy_dev_list.devices, next, sentry) {
        if (entry->remote_pid == dev->remote_pid) {
            QLIST_REMOVE(entry, next);
        }
    }

    if (!QLIST_EMPTY(&proxy_dev_list.devices)) {
        start_broadcast_timer();
    }

    qemu_del_vm_change_state_handler(dev->vmcse);
}

static void send_bar_access_msg(PCIProxyDev *dev, MemoryRegion *mr,
                                bool write, hwaddr addr, uint64_t *val,
                                unsigned size, bool memory)
{
    MPQemuLinkState *mpqemu_link = dev->mpqemu_link;
    MPQemuMsg msg, ret;

    memset(&msg, 0, sizeof(MPQemuMsg));
    memset(&ret, 0, sizeof(MPQemuMsg));

    msg.bytestream = 0;
    msg.size = sizeof(msg.data1);
    msg.data1.bar_access.addr = mr->addr + addr;
    msg.data1.bar_access.size = size;
    msg.data1.bar_access.memory = memory;

    if (write) {
        msg.cmd = BAR_WRITE;
        msg.data1.bar_access.val = *val;
    } else {
        msg.cmd = BAR_READ;
    }

    mpqemu_msg_send(&msg, mpqemu_link->mmio);

    if (write) {
        return;
    }

    mpqemu_msg_recv(&ret, mpqemu_link->mmio);

    *val = ret.data1.mmio_ret.val;
}

void proxy_default_bar_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned size)
{
    ProxyMemoryRegion *pmr = opaque;

    send_bar_access_msg(pmr->dev, &pmr->mr, true, addr, &val, size,
                        pmr->memory);
}

uint64_t proxy_default_bar_read(void *opaque, hwaddr addr, unsigned size)
{
    ProxyMemoryRegion *pmr = opaque;
    uint64_t val;

    send_bar_access_msg(pmr->dev, &pmr->mr, false, addr, &val, size,
                        pmr->memory);

     return val;
}

const MemoryRegionOps proxy_default_ops = {
    .read = proxy_default_bar_read,
    .write = proxy_default_bar_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};
