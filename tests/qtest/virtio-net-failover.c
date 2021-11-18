#include "qemu/osdep.h"
#include "libqos/libqtest.h"
#include "libqos/pci.h"
#include "libqos/pci-pc.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qjson.h"
#include "libqos/malloc-pc.h"
#include "libqos/virtio-pci.h"
#include "hw/pci/pci.h"

#define ACPI_PCIHP_ADDR_ICH9    0x0cc0
#define PCI_EJ_BASE             0x0008

#define BASE_MACHINE "-M q35 -nodefaults " \
    "-device pcie-root-port,id=root0,addr=0x1,bus=pcie.0,chassis=1 " \
    "-device pcie-root-port,id=root1,addr=0x2,bus=pcie.0,chassis=2 "

#define MAC_PRIMARY "52:54:00:11:11:11"
#define MAC_STANDBY "52:54:00:22:22:22"

static QGuestAllocator guest_malloc;
static QPCIBus *pcibus;

static QTestState *machine_start(const char *args)
{
    QTestState *qts;
    QPCIDevice *dev;

    qts = qtest_init(args);

    pc_alloc_init(&guest_malloc, qts, 0);
    pcibus = qpci_new_pc(qts, &guest_malloc);
    g_assert(qpci_secondary_buses_init(pcibus) == 2);

    dev = qpci_device_find(pcibus, QPCI_DEVFN(1, 0)); /* root0 */
    g_assert_nonnull(dev);

    qpci_device_enable(dev);
    qpci_iomap(dev, 4, NULL);

    g_free(dev);

    dev = qpci_device_find(pcibus, QPCI_DEVFN(2, 0)); /* root1 */
    g_assert_nonnull(dev);

    qpci_device_enable(dev);
    qpci_iomap(dev, 4, NULL);

    g_free(dev);

    return qts;
}

static void machine_stop(QTestState *qts)
{
    qpci_free_pc(pcibus);
    alloc_destroy(&guest_malloc);
    qtest_quit(qts);
}

static void test_error_id(void)
{
    QTestState *qts;
    QDict *resp;
    QDict *err;

    qts = machine_start(BASE_MACHINE
                        "-device virtio-net,bus=root0,id=standby0,failover=on");

    resp = qtest_qmp(qts, "{'execute': 'device_add',"
                          "'arguments': {"
                          "'driver': 'virtio-net',"
                          "'bus': 'root1',"
                          "'failover_pair_id': 'standby0'"
                          "} }");
    g_assert(qdict_haskey(resp, "error"));

    err = qdict_get_qdict(resp, "error");
    g_assert(qdict_haskey(err, "desc"));

    g_assert_cmpstr(qdict_get_str(err, "desc"), ==,
                    "Device with failover_pair_id needs to have id");

    qobject_unref(resp);

    machine_stop(qts);
}

static void test_error_pcie(void)
{
    QTestState *qts;
    QDict *resp;
    QDict *err;

    qts = machine_start(BASE_MACHINE
                        "-device virtio-net,bus=root0,id=standby0,failover=on");

    resp = qtest_qmp(qts, "{'execute': 'device_add',"
                          "'arguments': {"
                          "'driver': 'virtio-net',"
                          "'id': 'primary0',"
                          "'bus': 'pcie.0',"
                          "'failover_pair_id': 'standby0'"
                          "} }");
    g_assert(qdict_haskey(resp, "error"));

    err = qdict_get_qdict(resp, "error");
    g_assert(qdict_haskey(err, "desc"));

    g_assert_cmpstr(qdict_get_str(err, "desc"), ==,
                    "Bus 'pcie.0' does not support hotplugging");

    qobject_unref(resp);

    machine_stop(qts);
}

static QDict *find_device(QDict *bus, const char *name)
{
    const QObject *obj;
    QList *devices;
    QList *list;

    devices = qdict_get_qlist(bus, "devices");
    if (devices == NULL) {
        return NULL;
    }

    list = qlist_copy(devices);
    while ((obj = qlist_pop(list))) {
        QDict *device;

        device = qobject_to(QDict, obj);

        if (qdict_haskey(device, "pci_bridge")) {
            QDict *bridge;
            QDict *bridge_device;

            bridge = qdict_get_qdict(device, "pci_bridge");

            if (qdict_haskey(bridge, "devices")) {
                bridge_device = find_device(bridge, name);
                if (bridge_device) {
                    qobject_unref(list);
                    return bridge_device;
                }
            }
        }

        if (!qdict_haskey(device, "qdev_id")) {
            continue;
        }

        if (strcmp(qdict_get_str(device, "qdev_id"), name) == 0) {
            qobject_ref(device);
            qobject_unref(list);
            return device;
        }
    }
    qobject_unref(list);

    return NULL;
}

static QDict *get_bus(QTestState *qts, int num)
{
    QObject *obj;
    QDict *resp;
    QList *ret;

    resp = qtest_qmp(qts, "{ 'execute': 'query-pci' }");
    g_assert(qdict_haskey(resp, "return"));

    ret = qdict_get_qlist(resp, "return");
    g_assert_nonnull(ret);

    while ((obj = qlist_pop(ret))) {
        QDict *bus;

        bus = qobject_to(QDict, obj);
        if (!qdict_haskey(bus, "bus")) {
            continue;
        }
        if (qdict_get_int(bus, "bus") == num) {
            qobject_ref(bus);
            qobject_unref(resp);
            return bus;
        }
    }
    qobject_unref(resp);

    return NULL;
}

static char *get_mac(QTestState *qts, const char *name)
{
    QDict *resp;
    char *mac;

    resp = qtest_qmp(qts, "{ 'execute': 'qom-get', "
                     "'arguments': { "
                     "'path': %s, "
                     "'property': 'mac' } }", name);

    g_assert(qdict_haskey(resp, "return"));

    mac = g_strdup( qdict_get_str(resp, "return"));

    qobject_unref(resp);

    return mac;
}

static void check_cards(QTestState *qts, bool standby, bool primary)
{
    QDict *device;
    QDict *bus;
    char *mac;

    bus = get_bus(qts, 0);
    device = find_device(bus, "standby0");
    if (standby) {
        g_assert_nonnull(device);
        qobject_unref(device);

        mac = get_mac(qts, "/machine/peripheral/standby0");
        g_assert_cmpstr(mac, ==, MAC_STANDBY);
        g_free(mac);
    } else {
       g_assert_null(device);
    }

    device = find_device(bus, "primary0");
    if (primary) {
        g_assert_nonnull(device);
        qobject_unref(device);

        mac = get_mac(qts, "/machine/peripheral/primary0");
        g_assert_cmpstr(mac, ==, MAC_PRIMARY);
        g_free(mac);
    } else {
       g_assert_null(device);
    }
    qobject_unref(bus);
}

static void test_on(void)
{
    QTestState *qts;

    qts = machine_start(BASE_MACHINE
                        "-netdev user,id=hs0 "
                        "-device virtio-net,bus=root0,id=standby0,"
                        "failover=on,netdev=hs0,mac="MAC_STANDBY" "
                        "-device virtio-net,bus=root1,id=primary0,"
                        "failover_pair_id=standby0,netdev=hs1,mac="MAC_PRIMARY);

    check_cards(qts, true, false); /* standby, no primary */

    machine_stop(qts);
}

static void test_on_mismatch(void)
{
    QTestState *qts;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-device virtio-net,bus=root0,id=standby0,"
                     "failover=on,netdev=hs0,mac="MAC_STANDBY" "
                     "-netdev user,id=hs1 "
                     "-device virtio-net,bus=root1,id=primary0,"
                     "failover_pair_id=standby1,netdev=hs1,mac="MAC_PRIMARY);

    check_cards(qts, true, true); /* standby, primary (but no failover) */

    machine_stop(qts);
}

static void test_off(void)
{
    QTestState *qts;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-device virtio-net,bus=root0,id=standby0,"
                     "failover=off,netdev=hs0,mac="MAC_STANDBY" "
                     "-netdev user,id=hs1 "
                     "-device virtio-net,bus=root1,id=primary0,"
                     "failover_pair_id=standby0,netdev=hs1,mac="MAC_PRIMARY);

    check_cards(qts, true, true); /* standby, primary (but no failover) */

    machine_stop(qts);
}

static void start_virtio_net(QTestState *qts, int bus, int slot)
{
    QVirtioPCIDevice *dev;
    uint64_t features;
    QPCIAddress addr;
    QDict *resp;
    QDict *data;

    addr.devfn = QPCI_DEVFN((bus << 5) + slot, 0);
    dev = virtio_pci_new(pcibus, &addr);
    g_assert_nonnull(dev);
    qvirtio_pci_device_enable(dev);
    qvirtio_start_device(&dev->vdev);
    features = qvirtio_get_features(&dev->vdev);
    features = features & ~(QVIRTIO_F_BAD_FEATURE |
                            (1ull << VIRTIO_RING_F_INDIRECT_DESC) |
                            (1ull << VIRTIO_RING_F_EVENT_IDX));
    qvirtio_set_features(&dev->vdev, features);
    qvirtio_set_driver_ok(&dev->vdev);

    resp = qtest_qmp_eventwait_ref(qts, "FAILOVER_NEGOTIATED");
    g_assert(qdict_haskey(resp, "data"));

    data = qdict_get_qdict(resp, "data");
    g_assert(qdict_haskey(data, "device-id"));
    g_assert_cmpstr(qdict_get_str(data, "device-id"), ==, "standby0");

    qobject_unref(resp);
}

static void test_enabled(void)
{
    QTestState *qts;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-device virtio-net,bus=root0,id=standby0,"
                     "failover=on,netdev=hs0,mac="MAC_STANDBY" "
                     "-netdev user,id=hs1 "
                     "-device virtio-net,bus=root1,id=primary0,"
                     "failover_pair_id=standby0,netdev=hs1,mac="MAC_PRIMARY" "
                     );

    check_cards(qts, true, false); /* standby, no primary */

    start_virtio_net(qts, 1, 0);

    check_cards(qts, true, true); /* standby, primary with failover */

    machine_stop(qts);
}

static void test_hotplug_1(void)
{
    QTestState *qts;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-device virtio-net,bus=root0,id=standby0,"
                     "failover=on,netdev=hs0,mac="MAC_STANDBY" "
                     "-netdev user,id=hs1 "
                     );

    check_cards(qts, true, false); /* no standby, no primary */

    start_virtio_net(qts, 1, 0);

    check_cards(qts, true, false);

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'mac': '"MAC_PRIMARY"'}");

    check_cards(qts, true, true);

    machine_stop(qts);
}

static void test_hotplug_1_reverse(void)
{
    QTestState *qts;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-netdev user,id=hs1 "
                     "-device virtio-net,bus=root1,id=primary0,"
                     "failover_pair_id=standby0,netdev=hs1,mac="MAC_PRIMARY" "
                     );

    check_cards(qts, false, true);

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'on',"
                         "'netdev': 'hs0',"
                         "'mac': '"MAC_STANDBY"'}");

    check_cards(qts, true, true); /* XXX: sounds like a bug */

    start_virtio_net(qts, 1, 0);

    check_cards(qts, true, true);

    machine_stop(qts);
}

static void test_hotplug_2(void)
{
    QTestState *qts;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-netdev user,id=hs1 "
                     );

    check_cards(qts, false, false); /* no standby, no primary */

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'on',"
                         "'netdev': 'hs0',"
                         "'mac': '"MAC_STANDBY"'}");

    check_cards(qts, true, false);

    start_virtio_net(qts, 1, 0);

    check_cards(qts, true, false);

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'mac': '"MAC_PRIMARY"'}");

    check_cards(qts, true, true);

    machine_stop(qts);
}

static void test_hotplug_2_reverse(void)
{
    QTestState *qts;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-netdev user,id=hs1 "
                     );

    check_cards(qts, false, false); /* no standby, no primary */

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'mac': '"MAC_PRIMARY"'}");

    check_cards(qts, false, true);

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'on',"
                         "'netdev': 'hs0',"
                         "'rombar': 0,"
                         "'romfile': '',"
                         "'mac': '"MAC_STANDBY"'}");

    check_cards(qts, true, true); /* XXX: sounds like a bug */

    start_virtio_net(qts, 1, 0);

    check_cards(qts, true, true);

    machine_stop(qts);
}

static void test_outmigrate(gconstpointer opaque)
{
    QTestState *qts;
    QDict *resp, *args, *data;
    g_autofree gchar *uri = g_strdup_printf("exec: cat > %s", (gchar *)opaque);
    struct timeval timeout;

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-netdev user,id=hs1 "
                     );

    check_cards(qts, false, false); /* no standby, no primary */

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'on',"
                         "'netdev': 'hs0',"
                         "'mac': '"MAC_STANDBY"'}");

    check_cards(qts, true, false);

    start_virtio_net(qts, 1, 0);

    check_cards(qts, true, false);

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'rombar': 0,"
                         "'romfile': '',"
                         "'mac': '"MAC_PRIMARY"'}");

    check_cards(qts, true, true);

    args = qdict_from_jsonf_nofail("{}");
    g_assert_nonnull(args);
    qdict_put_str(args, "uri", uri);

    resp = qtest_qmp(qts, "{ 'execute': 'migrate', 'arguments': %p}", args);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    resp = qtest_qmp_eventwait_ref(qts, "UNPLUG_PRIMARY");
    g_assert(qdict_haskey(resp, "data"));

    data = qdict_get_qdict(resp, "data");
    g_assert(qdict_haskey(data, "device-id"));
    g_assert_cmpstr(qdict_get_str(data, "device-id"), ==, "primary0");

    qobject_unref(resp);

    /*
     * The migration cannot start if the card is not ejected,
     * so we check it cannot end ("STOP") before the card is ejected
     */
    /* 10s is enough for ACPI, PCIe native would need at least 30s */
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    resp = qtest_qmp_eventwait_timeout(qts, &timeout, "STOP");
    g_assert_null(resp);

    qtest_outl(qts, ACPI_PCIHP_ADDR_ICH9 + PCI_EJ_BASE, 1);

    qtest_qmp_eventwait(qts, "STOP");
    /*
     * in fact, the card is ejected from the point of view of kernel
     * but not really from QEMU to be able to hotplug it back if
     * migration fails. So we can't check that:
     *   check_cards(qts, true, false);
     */

    machine_stop(qts);
}

static void test_inmigrate(gconstpointer opaque)
{
    QTestState *qts;
    QDict *resp, *args;
    g_autofree gchar *uri = g_strdup_printf("exec: cat %s", (gchar *)opaque);

    qts = machine_start(BASE_MACHINE
                     "-netdev user,id=hs0 "
                     "-netdev user,id=hs1 "
                     "-incoming defer "
                     );

    check_cards(qts, false, false); /* no standby, no primary */

    qtest_qmp_device_add(qts, "virtio-net", "standby0",
                         "{'bus': 'root0',"
                         "'failover': 'on',"
                         "'netdev': 'hs0',"
                         "'mac': '"MAC_STANDBY"'}");

    check_cards(qts, true, false);

    qtest_qmp_device_add(qts, "virtio-net", "primary0",
                         "{'bus': 'root1',"
                         "'failover_pair_id': 'standby0',"
                         "'netdev': 'hs1',"
                         "'rombar': 0,"
                         "'romfile': '',"
                         "'mac': '"MAC_PRIMARY"'}");

    check_cards(qts, true, false);

    args = qdict_from_jsonf_nofail("{}");
    g_assert_nonnull(args);
    qdict_put_str(args, "uri", uri);

    resp = qtest_qmp(qts, "{ 'execute': 'migrate-incoming', 'arguments': %p}",
                     args);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    qtest_qmp_eventwait(qts, "MIGRATION");
    qtest_qmp_eventwait(qts, "FAILOVER_NEGOTIATED");

    check_cards(qts, true, true);

    qtest_qmp_eventwait(qts, "RESUME");

    machine_stop(qts);
}

int main(int argc, char **argv)
{
    gchar *tmpfile = g_strdup_printf("/tmp/failover_test_migrate-%u-%u",
                                     getpid(), g_test_rand_int());
    const char *arch;
    int ret;

    g_test_init(&argc, &argv, NULL);

    arch = qtest_get_arch();
    if (strcmp(arch, "i386") && strcmp(arch, "x86_64")) {
        g_test_message("Skipping test for non-x86");
        return g_test_run();
    }

    qtest_add_func("failover-virtio-net/params/error/id", test_error_id);
    qtest_add_func("failover-virtio-net/params/error/pcie", test_error_pcie);
    qtest_add_func("failover-virtio-net/params/error/on", test_on);
    qtest_add_func("failover-virtio-net/params/error/on_mismatch",
                   test_on_mismatch);
    qtest_add_func("failover-virtio-net/params/error/off", test_off);
    qtest_add_func("failover-virtio-net/params/error/enabled", test_enabled);
    qtest_add_func("failover-virtio-net/params/error/hotplug_1",
                   test_hotplug_1);
    qtest_add_func("failover-virtio-net/params/error/hotplug_1_reverse",
                   test_hotplug_1_reverse);
    qtest_add_func("failover-virtio-net/params/error/hotplug_2",
                   test_hotplug_2);
    qtest_add_func("failover-virtio-net/params/error/hotplug_2_reverse",
                   test_hotplug_2_reverse);
    qtest_add_data_func("failover-virtio-net/params/error/outmigrate",
                   tmpfile, test_outmigrate);
    qtest_add_data_func("failover-virtio-net/params/error/inmigrate",
                   tmpfile, test_inmigrate);

    ret = g_test_run();

    unlink(tmpfile);
    g_free(tmpfile);

    return ret;
}
