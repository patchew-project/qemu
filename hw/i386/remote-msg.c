#include "qemu/osdep.h"
#include "qemu-common.h"

#include "hw/i386/remote.h"
#include "io/channel.h"
#include "io/mpqemu-link.h"
#include "qapi/error.h"
#include "sysemu/runstate.h"
#include "io/channel-util.h"
#include "hw/pci/pci.h"
#include "exec/memattrs.h"
#include "hw/i386/remote-memory.h"
#include "hw/remote/iohub.h"

static void process_connect_dev_msg(MPQemuMsg *msg, QIOChannel *com,
                                    Error **errp);
static void process_config_write(QIOChannel *ioc, PCIDevice *dev,
                                 MPQemuMsg *msg);
static void process_config_read(QIOChannel *ioc, PCIDevice *dev,
                                MPQemuMsg *msg);
static void process_bar_write(QIOChannel *ioc, MPQemuMsg *msg, Error **errp);
static void process_bar_read(QIOChannel *ioc, MPQemuMsg *msg, Error **errp);
static void process_get_pci_info_msg(QIOChannel *ioc, MPQemuMsg *msg,
                                     PCIDevice *pci_dev);

gboolean mpqemu_process_msg(QIOChannel *ioc, GIOCondition cond,
                            gpointer opaque)
{
    DeviceState *dev = (DeviceState *)opaque;
    PCIDevice *pci_dev = PCI_DEVICE(dev);
    Error *local_err = NULL;
    MPQemuMsg msg = { 0 };

    if (cond & G_IO_HUP) {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }

    if (cond & (G_IO_ERR | G_IO_NVAL)) {
        error_setg(&local_err, "Error %d while processing message from proxy \
                   in remote process pid=%d", errno, getpid());
        return FALSE;
    }

    if (mpqemu_msg_recv(&msg, ioc) < 0) {
        return FALSE;
    }

    if (!mpqemu_msg_valid(&msg)) {
        error_report("Received invalid message from proxy \
                     in remote process pid=%d", getpid());
        return TRUE;
    }

    switch (msg.cmd) {
    case CONNECT_DEV:
        process_connect_dev_msg(&msg, ioc, &local_err);
        break;
    case PCI_CONFIG_WRITE:
        process_config_write(ioc, pci_dev, &msg);
        break;
    case PCI_CONFIG_READ:
        process_config_read(ioc, pci_dev, &msg);
        break;
    case BAR_WRITE:
        process_bar_write(ioc, &msg, &local_err);
        break;
    case BAR_READ:
        process_bar_read(ioc, &msg, &local_err);
        break;
    case SYNC_SYSMEM:
        remote_sysmem_reconfig(&msg, &local_err);
        break;
    case SET_IRQFD:
        process_set_irqfd_msg(pci_dev, &msg);
        break;
    case GET_PCI_INFO:
        process_get_pci_info_msg(ioc, &msg, pci_dev);
        break;
    default:
        error_setg(&local_err, "Unknown command (%d) received from proxy \
                   in remote process pid=%d", msg.cmd, getpid());
    }

    if (msg.data2) {
        free(msg.data2);
    }

    if (local_err) {
        error_report_err(local_err);
        return FALSE;
    }

    return TRUE;
}

static void process_connect_dev_msg(MPQemuMsg *msg, QIOChannel *com,
                                    Error **errp)
{
    char *devid = (char *)msg->data2;
    QIOChannel *dioc = NULL;
    DeviceState *dev = NULL;
    MPQemuMsg ret = { 0 };
    int rc = 0;

    g_assert(devid && (devid[msg->size - 1] == '\0'));

    dev = qdev_find_recursive(sysbus_get_default(), devid);
    if (!dev || !object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        rc = 0xff;
        goto exit;
    }

    dioc = qio_channel_new_fd(msg->fds[0], errp);

    qio_channel_add_watch(dioc, G_IO_IN | G_IO_HUP, mpqemu_process_msg,
                          (void *)dev, NULL);

exit:
    ret.cmd = RET_MSG;
    ret.bytestream = 0;
    ret.data1.u64 = rc;
    ret.size = sizeof(ret.data1);

    mpqemu_msg_send(&ret, com);
}

static void process_config_write(QIOChannel *ioc, PCIDevice *dev,
                                 MPQemuMsg *msg)
{
    struct conf_data_msg *conf = (struct conf_data_msg *)msg->data2;
    MPQemuMsg ret = { 0 };

    if (conf->addr >= PCI_CFG_SPACE_EXP_SIZE) {
        error_report("Bad address received when writing PCI config, pid %d",
                     getpid());
        ret.data1.u64 = UINT64_MAX;
    } else {
        pci_default_write_config(dev, conf->addr, conf->val, conf->l);
    }

    ret.cmd = RET_MSG;
    ret.bytestream = 0;
    ret.size = sizeof(ret.data1);

    mpqemu_msg_send(&ret, ioc);
}

static void process_config_read(QIOChannel *ioc, PCIDevice *dev,
                                MPQemuMsg *msg)
{
    struct conf_data_msg *conf = (struct conf_data_msg *)msg->data2;
    MPQemuMsg ret = { 0 };

    if (conf->addr >= PCI_CFG_SPACE_EXP_SIZE) {
        error_report("Bad address received when reading PCI config, pid %d",
                     getpid());
        ret.data1.u64 = UINT64_MAX;
    } else {
        ret.data1.u64 = pci_default_read_config(dev, conf->addr, conf->l);
    }

    ret.cmd = RET_MSG;
    ret.bytestream = 0;
    ret.size = sizeof(ret.data1);

    mpqemu_msg_send(&ret, ioc);
}

static void process_bar_write(QIOChannel *ioc, MPQemuMsg *msg, Error **errp)
{
    BarAccessMsg *bar_access = &msg->data1.bar_access;
    AddressSpace *as =
        bar_access->memory ? &address_space_memory : &address_space_io;
    MPQemuMsg ret = { 0 };
    MemTxResult res;

    if (!is_power_of_2(bar_access->size) ||
       (bar_access->size > sizeof(uint64_t))) {
        ret.data1.u64 = UINT64_MAX;
        goto fail;
    }

    res = address_space_rw(as, bar_access->addr, MEMTXATTRS_UNSPECIFIED,
                           (void *)&bar_access->val, bar_access->size,
                           true);

    if (res != MEMTX_OK) {
        error_setg(errp, "Could not perform address space write operation,"
                   " inaccessible address: %lx in pid %d.",
                   bar_access->addr, getpid());
        ret.data1.u64 = -1;
    }

fail:
    ret.cmd = RET_MSG;
    ret.size = sizeof(ret.data1);

    mpqemu_msg_send(&ret, ioc);
}

static void process_bar_read(QIOChannel *ioc, MPQemuMsg *msg, Error **errp)
{
    BarAccessMsg *bar_access = &msg->data1.bar_access;
    MPQemuMsg ret = { 0 };
    AddressSpace *as;
    MemTxResult res;
    uint64_t val = 0;

    as = bar_access->memory ? &address_space_memory : &address_space_io;

    if (!is_power_of_2(bar_access->size) ||
       (bar_access->size > sizeof(uint64_t))) {
        val = UINT64_MAX;
        goto fail;
    }

    res = address_space_rw(as, bar_access->addr, MEMTXATTRS_UNSPECIFIED,
                           (void *)&val, bar_access->size, false);

    if (res != MEMTX_OK) {
        error_setg(errp, "Could not perform address space read operation,"
                   " inaccessible address: %lx in pid %d.",
                   bar_access->addr, getpid());
        val = UINT64_MAX;
        goto fail;
    }

    switch (bar_access->size) {
    case 8:
        /* Nothing to do as val is already 8 bytes long */
        break;
    case 4:
        val = *((uint32_t *)&val);
        break;
    case 2:
        val = *((uint16_t *)&val);
        break;
    case 1:
        val = *((uint8_t *)&val);
        break;
    default:
        error_setg(errp, "Invalid PCI BAR read size in pid %d",
                   getpid());
        val = (uint64_t)-1;
    }

fail:
    ret.cmd = RET_MSG;
    ret.data1.u64 = val;
    ret.size = sizeof(ret.data1);

    mpqemu_msg_send(&ret, ioc);
}

static void process_get_pci_info_msg(QIOChannel *ioc, MPQemuMsg *msg,
                                     PCIDevice *pci_dev)
{
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(pci_dev);
    MPQemuMsg ret = { 0 };

    ret.cmd = RET_MSG;

    ret.data1.ret_pci_info.vendor_id = pc->vendor_id;
    ret.data1.ret_pci_info.device_id = pc->device_id;
    ret.data1.ret_pci_info.class_id = pc->class_id;
    ret.data1.ret_pci_info.subsystem_id = pc->subsystem_id;

    ret.size = sizeof(ret.data1);

    mpqemu_msg_send(&ret, ioc);
}
