/*
 * Bluetooth command line options.
 *
 * Copyright (C) 2008 Andrzej Zaborowski
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "sysemu/bt.h"
#include "net/net.h"
#include "hw/bt.h"

static int nb_hcis;
static int cur_hci;
static struct HCIInfo *hci_table[MAX_NICS];

struct HCIInfo *qemu_next_hci(void)
{
    if (cur_hci == nb_hcis) {
        return &null_hci;
    }

    return hci_table[cur_hci++];
}

static int bt_hci_parse(const char *str)
{
    struct HCIInfo *hci;
    bdaddr_t bdaddr;

    if (nb_hcis >= MAX_NICS) {
        error_report("too many bluetooth HCIs (max %i)", MAX_NICS);
        return -1;
    }

    hci = hci_init(str);
    if (!hci) {
        return -1;
    }

    bdaddr.b[0] = 0x52;
    bdaddr.b[1] = 0x54;
    bdaddr.b[2] = 0x00;
    bdaddr.b[3] = 0x12;
    bdaddr.b[4] = 0x34;
    bdaddr.b[5] = 0x56 + nb_hcis;
    hci->bdaddr_set(hci, bdaddr.b);

    hci_table[nb_hcis++] = hci;

    return 0;
}

static void bt_vhci_add(int vlan_id)
{
    struct bt_scatternet_s *vlan = qemu_find_bt_vlan(vlan_id);

    if (!vlan->slave) {
        warn_report("adding a VHCI to an empty scatternet %i", vlan_id);
    }

    bt_vhci_init(bt_new_hci(vlan));
}

static struct bt_device_s *bt_device_add(const char *opt)
{
    struct bt_scatternet_s *vlan;
    int vlan_id = 0;
    char *endp = strstr(opt, ",vlan=");
    int len = (endp ? endp - opt : strlen(opt)) + 1;
    char devname[10];

    pstrcpy(devname, MIN(sizeof(devname), len), opt);

    if (endp) {
        if (qemu_strtoi(endp + 6, (const char **)&endp, 0, &vlan_id) < 0) {
            error_report("unrecognised bluetooth vlan Id");
            return 0;
        }
    }

    vlan = qemu_find_bt_vlan(vlan_id);

    if (!vlan->slave) {
        warn_report("adding a slave device to an empty scatternet %i",
                    vlan_id);
    }

    if (!strcmp(devname, "keyboard")) {
        return bt_keyboard_init(vlan);
    }

    error_report("unsupported bluetooth device '%s'", devname);
    return 0;
}

int bt_parse(const char *opt)
{
    const char *endp, *p;
    int vlan;

    if (strstart(opt, "hci", &endp)) {
        if (!*endp || *endp == ',') {
            if (*endp) {
                if (!strstart(endp, ",vlan=", 0)) {
                    opt = endp + 1;
                }
            }

            return bt_hci_parse(opt);
       }
    } else if (strstart(opt, "vhci", &endp)) {
        if (!*endp || *endp == ',') {
            if (*endp) {
                if (strstart(endp, ",vlan=", &p)) {
                    if (qemu_strtoi(p, &endp, 0, &vlan) < 0) {
                        error_report("bad scatternet '%s'", p);
                        return 1;
                    }
                } else {
                    error_report("bad parameter '%s'", endp + 1);
                    return 1;
                }
            } else {
                vlan = 0;
            }

            bt_vhci_add(vlan);
            return 0;
        }
    } else if (strstart(opt, "device:", &endp)) {
        return !bt_device_add(endp);
    }

    error_report("bad bluetooth parameter '%s'", opt);
    return 1;
}
