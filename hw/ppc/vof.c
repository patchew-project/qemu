/*
 * QEMU PowerPC Virtual Open Firmware.
 *
 * This implements client interface from OpenFirmware IEEE1275 on the QEMU
 * side to leave only a very basic firmware in the VM.
 *
 * Copyright (c) 2020 IBM Corporation.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include <sys/ioctl.h>
#include "exec/ram_addr.h"
#include "exec/address-spaces.h"
#include "qemu/timer.h"
#include "qemu/range.h"
#include "hw/ppc/vof.h"
#include "hw/ppc/fdt.h"
#include "sysemu/runstate.h"
#include "qom/qom-qobject.h"
#include "trace.h"

#include <libfdt.h>

/*
 * OF 1275 "nextprop" description suggests is it 32 bytes max but
 * LoPAPR defines "ibm,query-interrupt-source-number" which is 33 chars long.
 */
#define OF_PROPNAME_LEN_MAX 64

typedef struct {
    uint64_t start;
    uint64_t size;
} OfClaimed;

typedef struct {
    char *path; /* the path used to open the instance */
    uint32_t phandle;
} OfInstance;

#define VOF_MEM_READ(pa, buf, size) \
    address_space_read_full(&address_space_memory, \
    (pa), MEMTXATTRS_UNSPECIFIED, (buf), (size))
#define VOF_MEM_WRITE(pa, buf, size) \
    address_space_write(&address_space_memory, \
    (pa), MEMTXATTRS_UNSPECIFIED, (buf), (size))

static int readstr(hwaddr pa, char *buf, int size)
{
    if (VOF_MEM_READ(pa, buf, size) != MEMTX_OK) {
        return -1;
    }
    if (strnlen(buf, size) == size) {
        buf[size - 1] = '\0';
        trace_vof_error_str_truncated(buf, size);
        return -1;
    }
    return 0;
}

static bool cmpservice(const char *s, unsigned nargs, unsigned nret,
                       const char *s1, unsigned nargscheck, unsigned nretcheck)
{
    if (strcmp(s, s1)) {
        return false;
    }
    if ((nargscheck && (nargs != nargscheck)) ||
        (nretcheck && (nret != nretcheck))) {
        trace_vof_error_param(s, nargscheck, nretcheck, nargs, nret);
        return false;
    }

    return true;
}

static void prop_format(char *tval, int tlen, const void *prop, int len)
{
    int i;
    const unsigned char *c;
    char *t;
    const char bin[] = "...";

    for (i = 0, c = prop; i < len; ++i, ++c) {
        if (*c == '\0' && i == len - 1) {
            strncpy(tval, prop, tlen - 1);
            return;
        }
        if (*c < 0x20 || *c >= 0x80) {
            break;
        }
    }

    for (i = 0, c = prop, t = tval; i < len; ++i, ++c) {
        if (t >= tval + tlen - sizeof(bin) - 1 - 2 - 1) {
            strcpy(t, bin);
            return;
        }
        if (i && i % 4 == 0 && i != len - 1) {
            strcat(t, " ");
            ++t;
        }
        t += sprintf(t, "%02X", *c & 0xFF);
    }
}

static uint32_t vof_finddevice(const void *fdt, uint32_t nodeaddr)
{
    char fullnode[1024];
    uint32_t ret = -1;
    int offset;

    if (readstr(nodeaddr, fullnode, sizeof(fullnode))) {
        return (uint32_t) ret;
    }

    offset = fdt_path_offset(fdt, fullnode);
    if (offset >= 0) {
        ret = fdt_get_phandle(fdt, offset);
    }
    trace_vof_finddevice(fullnode, ret);
    return (uint32_t) ret;
}

static uint32_t vof_getprop(const void *fdt, uint32_t nodeph, uint32_t pname,
                            uint32_t valaddr, uint32_t vallen)
{
    char propname[OF_PROPNAME_LEN_MAX + 1];
    uint32_t ret = 0;
    int proplen = 0;
    const void *prop;
    char trval[64] = "";
    int nodeoff = fdt_node_offset_by_phandle(fdt, nodeph);

    if (readstr(pname, propname, sizeof(propname))) {
        return -1;
    }
    if (strcmp(propname, "name") == 0) {
        prop = fdt_get_name(fdt, nodeoff, &proplen);
        proplen += 1;
    } else {
        prop = fdt_getprop(fdt, nodeoff, propname, &proplen);
    }

    if (prop) {
        int cb = MIN(proplen, vallen);

        if (VOF_MEM_WRITE(valaddr, prop, cb) != MEMTX_OK) {
            ret = -1;
        } else {
            /*
             * OF1275 says:
             * "Size is either the actual size of the property, or -1 if name
             * does not exist", hence returning proplen instead of cb.
             */
            ret = proplen;
            prop_format(trval, sizeof(trval), prop, ret);
        }
    } else {
        ret = -1;
    }
    trace_vof_getprop(nodeph, propname, ret, trval);

    return ret;
}

static uint32_t vof_getproplen(const void *fdt, uint32_t nodeph, uint32_t pname)
{
    char propname[OF_PROPNAME_LEN_MAX + 1];
    uint32_t ret = 0;
    int proplen = 0;
    const void *prop;
    int nodeoff = fdt_node_offset_by_phandle(fdt, nodeph);

    if (readstr(pname, propname, sizeof(propname))) {
        return -1;
    }
    if (strcmp(propname, "name") == 0) {
        prop = fdt_get_name(fdt, nodeoff, &proplen);
        proplen += 1;
    } else {
        prop = fdt_getprop(fdt, nodeoff, propname, &proplen);
    }

    if (prop) {
        ret = proplen;
    } else {
        ret = -1;
    }
    trace_vof_getproplen(nodeph, propname, ret);

    return ret;
}

static uint32_t vof_setprop(void *fdt, Vof *vof,
                            uint32_t nodeph, uint32_t pname,
                            uint32_t valaddr, uint32_t vallen)
{
    char propname[OF_PROPNAME_LEN_MAX + 1];
    uint32_t ret = -1;
    int offset;
    char trval[64] = "";

    if (readstr(pname, propname, sizeof(propname))) {
        return -1;
    }
    /*
     * We only allow changing properties which we know how to update in QEMU
     * OR
     * the ones which we know that they need to survive during "quiesce".
     */
    if (vallen == sizeof(uint32_t)) {
        uint32_t val32 = ldl_be_phys(first_cpu->as, valaddr);

        if ((strcmp(propname, "linux,rtas-base") == 0) ||
            (strcmp(propname, "linux,rtas-entry") == 0)) {
            /* These need to survive quiesce so let them store in the FDT */
        } else if (strcmp(propname, "linux,initrd-start") == 0) {
            vof->initrd_base = val32;
        } else if (strcmp(propname, "linux,initrd-end") == 0) {
            vof->initrd_size = val32 - vof->initrd_base;
        } else {
            goto trace_exit;
        }
    } else if (vallen == sizeof(uint64_t)) {
        uint64_t val64 = ldq_be_phys(first_cpu->as, valaddr);

        if (strcmp(propname, "linux,initrd-start") == 0) {
            vof->initrd_base = val64;
        } else if (strcmp(propname, "linux,initrd-end") == 0) {
            vof->initrd_size = val64 - vof->initrd_base;
        } else {
            goto trace_exit;
        }
    } else if (strcmp(propname, "bootargs") == 0) {
        char val[1024];

        if (readstr(valaddr, val, sizeof(val))) {
            goto trace_exit;
        }
        g_free(vof->bootargs);
        vof->bootargs = g_strdup(val);
    } else {
        goto trace_exit;
    }

    offset = fdt_node_offset_by_phandle(fdt, nodeph);
    if (offset >= 0) {
        uint8_t data[vallen];

        if ((VOF_MEM_READ(valaddr, data, vallen) == MEMTX_OK) &&
            !fdt_setprop(fdt, offset, propname, data, vallen)) {
            ret = vallen;
            prop_format(trval, sizeof(trval), data, ret);
        }
    }

trace_exit:
    trace_vof_setprop(nodeph, propname, trval, ret);

    return ret;
}

static uint32_t vof_nextprop(const void *fdt, uint32_t phandle,
                             uint32_t prevaddr, uint32_t nameaddr)
{
    int offset = fdt_node_offset_by_phandle(fdt, phandle);
    char prev[OF_PROPNAME_LEN_MAX + 1];
    const char *tmp;

    if (readstr(prevaddr, prev, sizeof(prev))) {
        return -1;
    }
    for (offset = fdt_first_property_offset(fdt, offset);
         offset >= 0;
         offset = fdt_next_property_offset(fdt, offset)) {

        if (!fdt_getprop_by_offset(fdt, offset, &tmp, NULL)) {
            return 0;
        }
        if (prev[0] == '\0' || strcmp(prev, tmp) == 0) {
            if (prev[0] != '\0') {
                offset = fdt_next_property_offset(fdt, offset);
                if (offset < 0) {
                    return 0;
                }
            }
            if (!fdt_getprop_by_offset(fdt, offset, &tmp, NULL)) {
                return 0;
            }

            if (VOF_MEM_WRITE(nameaddr, tmp, strlen(tmp) + 1) != MEMTX_OK) {
                return -1;
            }
            return 1;
        }
    }

    return 0;
}

static uint32_t vof_peer(const void *fdt, uint32_t phandle)
{
    int ret;

    if (phandle == 0) {
        ret = fdt_path_offset(fdt, "/");
    } else {
        ret = fdt_next_subnode(fdt, fdt_node_offset_by_phandle(fdt, phandle));
    }

    if (ret < 0) {
        ret = 0;
    } else {
        ret = fdt_get_phandle(fdt, ret);
    }

    return ret;
}

static uint32_t vof_child(const void *fdt, uint32_t phandle)
{
    int ret = fdt_first_subnode(fdt, fdt_node_offset_by_phandle(fdt, phandle));

    if (ret < 0) {
        ret = 0;
    } else {
        ret = fdt_get_phandle(fdt, ret);
    }

    return ret;
}

static uint32_t vof_parent(const void *fdt, uint32_t phandle)
{
    int ret = fdt_parent_offset(fdt, fdt_node_offset_by_phandle(fdt, phandle));

    if (ret < 0) {
        ret = 0;
    } else {
        ret = fdt_get_phandle(fdt, ret);
    }

    return ret;
}

static uint32_t vof_do_open(void *fdt, Vof *vof, const char *path)
{
    int offset;
    uint32_t ret = 0;
    OfInstance *inst = NULL;

    if (vof->of_instance_last == 0xFFFFFFFF) {
        /* We do not recycle ihandles yet */
        goto trace_exit;
    }

    offset = fdt_path_offset(fdt, path);
    if (offset < 0) {
        trace_vof_error_unknown_path(path);
        goto trace_exit;
    }

    inst = g_new0(OfInstance, 1);
    inst->phandle = fdt_get_phandle(fdt, offset);
    g_assert(inst->phandle);
    ++vof->of_instance_last;

    inst->path = g_strdup(path);
    g_hash_table_insert(vof->of_instances,
                        GINT_TO_POINTER(vof->of_instance_last),
                        inst);
    ret = vof->of_instance_last;

trace_exit:
    trace_vof_open(path, inst ? inst->phandle : 0, ret);

    return ret;
}

uint32_t vof_client_open_store(void *fdt, Vof *vof, const char *nodename,
                               const char *prop, const char *path)
{
    int node = fdt_path_offset(fdt, nodename);
    uint32_t inst = vof_do_open(fdt, vof, path);

    return fdt_setprop_cell(fdt, node, prop, inst);
}

static uint32_t vof_open(void *fdt, Vof *vof, uint32_t pathaddr)
{
    char path[256];

    if (readstr(pathaddr, path, sizeof(path))) {
        return -1;
    }

    return vof_do_open(fdt, vof, path);
}

static void vof_close(Vof *vof, uint32_t ihandle)
{
    if (!g_hash_table_remove(vof->of_instances, GINT_TO_POINTER(ihandle))) {
        trace_vof_error_unknown_ihandle_close(ihandle);
    }
}

static uint32_t vof_instance_to_package(Vof *vof, uint32_t ihandle)
{
    gpointer instp = g_hash_table_lookup(vof->of_instances,
                                         GINT_TO_POINTER(ihandle));
    uint32_t ret = -1;

    if (instp) {
        ret = ((OfInstance *)instp)->phandle;
    }
    trace_vof_instance_to_package(ihandle, ret);

    return ret;
}

static uint32_t vof_package_to_path(const void *fdt, uint32_t phandle,
                                    uint32_t buf, uint32_t len)
{
    uint32_t ret = -1;
    char tmp[256] = "";

    if (!fdt_get_path(fdt, fdt_node_offset_by_phandle(fdt, phandle), tmp,
                      sizeof(tmp))) {
        tmp[sizeof(tmp) - 1] = 0;
        ret = MIN(len, strlen(tmp) + 1);
        if (VOF_MEM_WRITE(buf, tmp, ret) != MEMTX_OK) {
            ret = -1;
        }
    }

    trace_vof_package_to_path(phandle, tmp, ret);

    return ret;
}

static uint32_t vof_instance_to_path(void *fdt, Vof *vof, uint32_t ihandle,
                                     uint32_t buf, uint32_t len)
{
    uint32_t ret = -1;
    uint32_t phandle = vof_instance_to_package(vof, ihandle);
    char tmp[256] = "";

    if (phandle != -1) {
        if (!fdt_get_path(fdt, fdt_node_offset_by_phandle(fdt, phandle),
                          tmp, sizeof(tmp))) {
            tmp[sizeof(tmp) - 1] = 0;
            ret = MIN(len, strlen(tmp) + 1);
            if (VOF_MEM_WRITE(buf, tmp, ret) != MEMTX_OK) {
                ret = -1;
            }
        }
    }
    trace_vof_instance_to_path(ihandle, phandle, tmp, ret);

    return ret;
}

static void vof_claimed_dump(GArray *claimed)
{
#ifdef DEBUG
    int i;
    OfClaimed c;

    for (i = 0; i < claimed->len; ++i) {
        c = g_array_index(claimed, OfClaimed, i);
        error_printf("CLAIMED %lx..%lx size=%ld\n", c.start, c.start + c.size,
                     c.size);
    }
#endif
}

static bool vof_claim_avail(GArray *claimed, uint64_t virt, uint64_t size)
{
    int i;
    OfClaimed c;

    for (i = 0; i < claimed->len; ++i) {
        c = g_array_index(claimed, OfClaimed, i);
        if (ranges_overlap(c.start, c.size, virt, size)) {
            return false;
        }
    }

    return true;
}

static void vof_claim_add(GArray *claimed, uint64_t virt, uint64_t size)
{
    OfClaimed newclaim;

    newclaim.start = virt;
    newclaim.size = size;
    g_array_append_val(claimed, newclaim);
}

static gint of_claimed_compare_func(gconstpointer a, gconstpointer b)
{
    return ((OfClaimed *)a)->start - ((OfClaimed *)b)->start;
}

static void vof_dt_memory_available(void *fdt, GArray *claimed, uint64_t base)
{
    int i, n, offset, proplen = 0;
    uint64_t *mem0_reg;
    struct { uint64_t start, size; } *avail;

    if (!fdt || !claimed) {
        return;
    }

    offset = fdt_path_offset(fdt, "/memory@0");
    _FDT(offset);

    mem0_reg = (uint64_t *) fdt_getprop(fdt, offset, "reg", &proplen);
    if (!mem0_reg || proplen != 2 * sizeof(uint64_t)) {
        return;
    }

    g_array_sort(claimed, of_claimed_compare_func);
    vof_claimed_dump(claimed);

    avail = g_malloc0(sizeof(uint64_t) * 2 * claimed->len);
    for (i = 0, n = 0; i < claimed->len; ++i) {
        OfClaimed c = g_array_index(claimed, OfClaimed, i);

        avail[n].start = c.start + c.size;
        if (i < claimed->len - 1) {
            OfClaimed cn = g_array_index(claimed, OfClaimed, i + 1);

            avail[n].size = cn.start - avail[n].start;
        } else {
            avail[n].size = be64_to_cpu(mem0_reg[1]) - avail[n].start;
        }

        if (avail[n].size) {
#ifdef DEBUG
            error_printf("AVAIL %lx..%lx size=%ld\n", avail[n].start,
                         avail[n].start + avail[n].size, avail[n].size);
#endif
            avail[n].start = cpu_to_be64(avail[n].start);
            avail[n].size = cpu_to_be64(avail[n].size);
            ++n;
        }
    }
    _FDT((fdt_setprop(fdt, offset, "available", avail,
                      sizeof(uint64_t) * 2 * n)));
    g_free(avail);
}

/*
 * OF1275:
 * "Allocates size bytes of memory. If align is zero, the allocated range
 * begins at the virtual address virt. Otherwise, an aligned address is
 * automatically chosen and the input argument virt is ignored".
 *
 * In other words, exactly one of @virt and @align is non-zero.
 */
uint64_t vof_claim(void *fdt, Vof *vof, uint64_t virt, uint64_t size,
                   uint64_t align)
{
    uint64_t ret;

    if (size == 0) {
        ret = -1;
    } else if (align == 0) {
        if (!vof_claim_avail(vof->claimed, virt, size)) {
            ret = -1;
        } else {
            ret = virt;
        }
    } else {
        vof->claimed_base = QEMU_ALIGN_UP(vof->claimed_base, align);
        while (1) {
            if (vof->claimed_base >= vof->top_addr) {
                error_report("Out of RMA memory for the OF client");
                return -1;
            }
            if (vof_claim_avail(vof->claimed, vof->claimed_base, size)) {
                break;
            }
            vof->claimed_base += size;
        }
        ret = vof->claimed_base;
    }

    if (ret != -1) {
        vof->claimed_base = MAX(vof->claimed_base, ret + size);
        vof_claim_add(vof->claimed, ret, size);
        /* The client reads "/memory@0/available" to know where it can claim */
        vof_dt_memory_available(fdt, vof->claimed, vof->claimed_base);
    }
    trace_vof_claim(virt, size, align, ret);

    return ret;
}

static uint32_t vof_release(void *fdt, Vof *vof, uint64_t virt, uint64_t size)
{
    uint32_t ret = -1;
    int i;
    GArray *claimed = vof->claimed;
    OfClaimed c;

    for (i = 0; i < claimed->len; ++i) {
        c = g_array_index(claimed, OfClaimed, i);
        if (c.start == virt && c.size == size) {
            g_array_remove_index(claimed, i);
            vof_dt_memory_available(fdt, vof->claimed, vof->claimed_base);
            ret = 0;
            break;
        }
    }

    trace_vof_release(virt, size, ret);

    return ret;
}

static void vof_instantiate_rtas(void)
{
    error_report("The firmware should have instantiated RTAS");
    exit(1);
}

static uint32_t vof_call_method(Vof *vof, uint32_t methodaddr,
                                uint32_t ihandle,
                                uint32_t param1, uint32_t param2,
                                uint32_t param3, uint32_t param4,
                                uint32_t *ret2)
{
    uint32_t ret = -1;
    char method[256] = "";
    OfInstance *inst;

    if (!ihandle) {
        goto trace_exit;
    }

    inst = (OfInstance *) g_hash_table_lookup(vof->of_instances,
                                              GINT_TO_POINTER(ihandle));
    if (!inst) {
        goto trace_exit;
    }

    if (readstr(methodaddr, method, sizeof(method))) {
        goto trace_exit;
    }

    if (strcmp(inst->path, "/") == 0) {
        if (strcmp(method, "ibm,client-architecture-support") == 0) {
            Object *cas_if = object_dynamic_cast(
                    qdev_get_machine(), TYPE_CLIENT_ARCHITECTURE_SUPPORT);

            if (cas_if) {
                ClientArchitectureSupportClass *casc =
                    CLIENT_ARCHITECTURE_SUPPORT_GET_CLASS(cas_if);

                ret = casc->cas(first_cpu, param1);
            }

            *ret2 = 0;
        }
    } else if (strcmp(inst->path, "/rtas") == 0) {
        if (strcmp(method, "instantiate-rtas") == 0) {
            vof_instantiate_rtas();
            ret = 0;
            *ret2 = param1; /* rtas-base */
        }
    } else {
        trace_vof_error_unknown_method(method);
    }

trace_exit:
    trace_vof_method(ihandle, method, param1, ret, *ret2);

    return ret;
}

static uint32_t vof_call_interpret(uint32_t cmdaddr, uint32_t param1,
                                   uint32_t param2, uint32_t *ret2)
{
    uint32_t ret = -1;
    char cmd[256] = "";

    /* No interpret implemented */
    readstr(cmdaddr, cmd, sizeof(cmd));
    trace_vof_interpret(cmd, param1, param2, ret, *ret2);

    return ret;
}

static void vof_quiesce(void *fdt, Vof *vof)
{
    Object *cas_if = object_dynamic_cast(
        qdev_get_machine(), TYPE_CLIENT_ARCHITECTURE_SUPPORT);

    int rc = fdt_pack(fdt);

    assert(rc == 0);

    if (cas_if) {
        ClientArchitectureSupportClass *casc =
            CLIENT_ARCHITECTURE_SUPPORT_GET_CLASS(cas_if);

        casc->quiesce();
    }

    vof_claimed_dump(vof->claimed);
}

uint32_t vof_client_call(void *fdt, Vof *vof, const char *service,
                         uint32_t *args, unsigned nargs,
                         uint32_t *rets, unsigned nrets)
{
    uint32_t ret = 0;

    /* @nrets includes the value which this function returns */
#define cmpserv(s, a, r) \
    cmpservice(service, nargs, nrets, (s), (a), (r))

    if (cmpserv("finddevice", 1, 1)) {
        ret = vof_finddevice(fdt, args[0]);
    } else if (cmpserv("getprop", 4, 1)) {
        ret = vof_getprop(fdt, args[0], args[1], args[2], args[3]);
    } else if (cmpserv("getproplen", 2, 1)) {
        ret = vof_getproplen(fdt, args[0], args[1]);
    } else if (cmpserv("setprop", 4, 1)) {
        ret = vof_setprop(fdt, vof, args[0], args[1], args[2], args[3]);
    } else if (cmpserv("nextprop", 3, 1)) {
        ret = vof_nextprop(fdt, args[0], args[1], args[2]);
    } else if (cmpserv("peer", 1, 1)) {
        ret = vof_peer(fdt, args[0]);
    } else if (cmpserv("child", 1, 1)) {
        ret = vof_child(fdt, args[0]);
    } else if (cmpserv("parent", 1, 1)) {
        ret = vof_parent(fdt, args[0]);
    } else if (cmpserv("open", 1, 1)) {
        ret = vof_open(fdt, vof, args[0]);
    } else if (cmpserv("close", 1, 0)) {
        vof_close(vof, args[0]);
    } else if (cmpserv("instance-to-package", 1, 1)) {
        ret = vof_instance_to_package(vof, args[0]);
    } else if (cmpserv("package-to-path", 3, 1)) {
        ret = vof_package_to_path(fdt, args[0], args[1], args[2]);
    } else if (cmpserv("instance-to-path", 3, 1)) {
        ret = vof_instance_to_path(fdt, vof, args[0], args[1], args[2]);
    } else if (cmpserv("claim", 3, 1)) {
        ret = vof_claim(fdt, vof, args[0], args[1], args[2]);
    } else if (cmpserv("release", 2, 0)) {
        ret = vof_release(fdt, vof, args[0], args[1]);
    } else if (cmpserv("call-method", 0, 0)) {
        ret = vof_call_method(vof, args[0], args[1], args[2], args[3], args[4],
                              args[5], rets);
    } else if (cmpserv("interpret", 0, 0)) {
        ret = vof_call_interpret(args[0], args[1], args[2], rets);
    } else if (cmpserv("milliseconds", 0, 1)) {
        ret = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    } else if (cmpserv("quiesce", 0, 0)) {
        vof_quiesce(fdt, vof);
    } else if (cmpserv("exit", 0, 0)) {
        error_report("Stopped as the VM requested \"exit\"");
        vm_stop(RUN_STATE_PAUSED); /* Or qemu_system_guest_panicked(NULL); ? */
    } else {
        trace_vof_error_unknown_service(service, nargs, nrets);
        ret = -1;
    }

    return ret;
}

static void of_instance_free(gpointer data)
{
    OfInstance *inst = (OfInstance *) data;

    g_free(inst->path);
    g_free(inst);
}

void vof_cleanup(Vof *vof)
{
    if (vof->claimed) {
        g_array_unref(vof->claimed);
    }
    if (vof->of_instances) {
        g_hash_table_unref(vof->of_instances);
    }
}

void vof_build_dt(void *fdt, Vof *vof, uint32_t top_addr)
{
    uint32_t phandle;
    int i, offset, proplen = 0;
    const void *prop;
    bool found = false;
    GArray *phandles = g_array_new(false, false, sizeof(uint32_t));

    vof_cleanup(vof);

    vof->claimed = g_array_new(false, false, sizeof(OfClaimed));
    vof->of_instances = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                              NULL, of_instance_free);
    vof->top_addr = top_addr;

    /* Find all predefined phandles */
    for (offset = fdt_next_node(fdt, -1, NULL);
         offset >= 0;
         offset = fdt_next_node(fdt, offset, NULL)) {
        prop = fdt_getprop(fdt, offset, "phandle", &proplen);
        if (prop && proplen == sizeof(uint32_t)) {
            phandle = fdt32_ld(prop);
            g_array_append_val(phandles, phandle);
        }
    }

    /* Assign phandles skipping the predefined ones */
    for (offset = fdt_next_node(fdt, -1, NULL), phandle = 1;
         offset >= 0;
         offset = fdt_next_node(fdt, offset, NULL), ++phandle) {

        prop = fdt_getprop(fdt, offset, "phandle", &proplen);
        if (prop) {
            continue;
        }
        /* Check if the current phandle is not allocated already */
        for ( ; ; ++phandle) {
            for (i = 0, found = false; i < phandles->len; ++i) {
                if (phandle == g_array_index(phandles, uint32_t, i)) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                break;
            }
        }
        _FDT(fdt_setprop_cell(fdt, offset, "phandle", phandle));
    }
    g_array_unref(phandles);

    vof_dt_memory_available(fdt, vof->claimed, vof->claimed_base);
}
