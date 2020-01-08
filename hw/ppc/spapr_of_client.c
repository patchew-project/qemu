#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_vio.h"
#include "trace.h"

#define OF_PROPNAME_LEN_MAX 32

/* Defined as Big Endian */
struct prom_args {
    uint32_t service;
    uint32_t nargs;
    uint32_t nret;
    uint32_t args[10];
};

static void readstr(hwaddr pa, char *buf, int size)
{
    cpu_physical_memory_read(pa, buf, size - 1);
    buf[size - 1] = 0;
}

static bool _cmpservice(const char *s, size_t len,
                        unsigned nargs, unsigned nret,
                        const char *s1, size_t len1,
                        unsigned nargscheck, unsigned nretcheck)
{
    if (strncmp(s, s1, MAX(len, len1))) {
        return false;
    }
    if (nargscheck == 0 && nretcheck == 0) {
        return true;
    }
    if (nargs != nargscheck || nret != nretcheck) {
        trace_spapr_of_client_error_param(s, nargscheck, nretcheck, nargs,
                                          nret);
        return false;
    }

    return true;
}

static uint32_t of_client_finddevice(const void *fdt, uint32_t nodeaddr)
{
    char node[256];
    int ret;

    readstr(nodeaddr, node, sizeof(node));
    ret = fdt_path_offset(fdt, node);
    if (ret >= 0) {
        ret = fdt_get_phandle(fdt, ret);
    }

    return (uint32_t) ret;
}

static uint32_t of_client_getprop(const void *fdt, uint32_t nodeph,
                                  uint32_t pname, uint32_t valaddr,
                                  uint32_t vallen)
{
    char propname[OF_PROPNAME_LEN_MAX + 1];
    uint32_t ret = 0;
    int proplen = 0;
    const void *prop;

    readstr(pname, propname, sizeof(propname));
    prop = fdt_getprop_namelen(fdt, fdt_node_offset_by_phandle(fdt, nodeph),
                               propname, strlen(propname), &proplen);
    if (prop) {
        int cb = MIN(proplen, vallen);

        cpu_physical_memory_write(valaddr, prop, cb);
        ret = cb;
    } else {
        ret = -1;
    }
    trace_spapr_of_client_getprop(nodeph, propname, ret);

    return ret;
}

static uint32_t of_client_getproplen(const void *fdt, uint32_t nodeph,
                                     uint32_t pname)
{
    char propname[OF_PROPNAME_LEN_MAX + 1];
    uint32_t ret = 0;
    int proplen = 0;
    const void *prop;

    readstr(pname, propname, sizeof(propname));

    prop = fdt_getprop_namelen(fdt, fdt_node_offset_by_phandle(fdt, nodeph),
                               propname, strlen(propname), &proplen);
    if (prop) {
        ret = proplen;
    } else {
        ret = -1;
    }

    return ret;
}

static uint32_t of_client_setprop(SpaprMachineState *spapr,
                                  uint32_t nodeph, uint32_t pname,
                                  uint32_t valaddr, uint32_t vallen)
{
    char propname[OF_PROPNAME_LEN_MAX + 1];
    uint32_t ret = -1;
    int offset;

    readstr(pname, propname, sizeof(propname));
    if (vallen == sizeof(uint32_t)) {
        uint32_t val32 = ldl_be_phys(first_cpu->as, valaddr);

        if ((strncmp(propname, "linux,rtas-base", 15) == 0) ||
            (strncmp(propname, "linux,rtas-entry", 16) == 0)) {
            spapr->rtas_base = val32;
        } else if (strncmp(propname, "linux,initrd-start", 18) == 0) {
            spapr->initrd_base = val32;
        } else if (strncmp(propname, "linux,initrd-end", 16) == 0) {
            spapr->initrd_size = val32 - spapr->initrd_base;
        } else {
            goto trace_exit;
        }
    } else if (vallen == sizeof(uint64_t)) {
        uint64_t val64 = ldq_be_phys(first_cpu->as, valaddr);

        if (strncmp(propname, "linux,initrd-start", 18) == 0) {
            spapr->initrd_base = val64;
        } else if (strncmp(propname, "linux,initrd-end", 16) == 0) {
            spapr->initrd_size = val64 - spapr->initrd_base;
        } else {
            goto trace_exit;
        }
    } else {
        goto trace_exit;
    }

    offset = fdt_node_offset_by_phandle(spapr->fdt_blob, nodeph);
    if (offset >= 0) {
        uint8_t data[vallen];

        cpu_physical_memory_read(valaddr, data, vallen);
        if (!fdt_setprop(spapr->fdt_blob, offset, propname, data, vallen)) {
            ret = vallen;
        }
    }

trace_exit:
    trace_spapr_of_client_setprop(nodeph, propname, ret);

    return ret;
}

static uint32_t of_client_nextprop(const void *fdt, uint32_t phandle,
                                   uint32_t prevaddr, uint32_t nameaddr)
{
    int offset = fdt_node_offset_by_phandle(fdt, phandle);
    char prev[OF_PROPNAME_LEN_MAX + 1];
    const char *tmp;

    readstr(prevaddr, prev, sizeof(prev));
    for (offset = fdt_first_property_offset(fdt, offset);
         offset >= 0;
         offset = fdt_next_property_offset(fdt, offset)) {

        if (!fdt_getprop_by_offset(fdt, offset, &tmp, NULL)) {
            return 0;
        }
        if (prev[0] == '\0' || strncmp(prev, tmp, sizeof(prev) - 1) == 0) {
            if (prev[0] != '\0') {
                offset = fdt_next_property_offset(fdt, offset);
                if (offset < 0) {
                    return 0;
                }
            }
            if (!fdt_getprop_by_offset(fdt, offset, &tmp, NULL)) {
                return 0;
            }
            cpu_physical_memory_write(nameaddr, tmp, strlen(tmp) + 1);
            return 1;
        }
    }

    return 0;
}

static uint32_t of_client_peer(const void *fdt, uint32_t phandle)
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

static uint32_t of_client_child(const void *fdt, uint32_t phandle)
{
    int ret = fdt_first_subnode(fdt, fdt_node_offset_by_phandle(fdt, phandle));

    if (ret < 0) {
        ret = 0;
    } else {
        ret = fdt_get_phandle(fdt, ret);
    }

    return ret;
}

static uint32_t of_client_parent(const void *fdt, uint32_t phandle)
{
    int ret = fdt_parent_offset(fdt, fdt_node_offset_by_phandle(fdt, phandle));

    if (ret < 0) {
        ret = 0;
    } else {
        ret = fdt_get_phandle(fdt, ret);
    }

    return ret;
}

uint32_t spapr_of_client_open(SpaprMachineState *spapr, uint32_t phandle)
{
    g_assert(spapr->of_instance_last != 0xFFFFFFFF);
    ++spapr->of_instance_last;
    g_hash_table_insert(spapr->of_instances,
                        GINT_TO_POINTER(spapr->of_instance_last),
                        GINT_TO_POINTER(phandle));

    return spapr->of_instance_last;
}

static void of_client_close(SpaprMachineState *spapr, uint32_t ihandle)
{
    if (!g_hash_table_remove(spapr->of_instances, GINT_TO_POINTER(ihandle))) {
        trace_spapr_of_client_error_unknown_ihandle_close(ihandle);
    }
}

static uint32_t of_client_instance_to_package(SpaprMachineState *spapr,
                                              uint32_t ihandle)
{
    gpointer phandle = g_hash_table_lookup(spapr->of_instances,
                                           GINT_TO_POINTER(ihandle));

    if (!phandle) {
        return -1;
    }

    return GPOINTER_TO_INT(phandle);
}

static uint32_t of_client_package_to_path(const void *fdt, uint32_t phandle,
                                          uint32_t buf, uint32_t len)
{
    char tmp[256];

    if (0 == fdt_get_path(fdt, fdt_node_offset_by_phandle(fdt, phandle), tmp,
                          sizeof(tmp))) {
        tmp[sizeof(tmp) - 1] = 0;
        cpu_physical_memory_write(buf, tmp, MIN(len, strlen(tmp)));
    }
    return len;
}

static uint32_t of_client_instance_to_path(SpaprMachineState *spapr,
                                           uint32_t ihandle, uint32_t buf,
                                           uint32_t len)
{
    uint32_t phandle = of_client_instance_to_package(spapr, ihandle);

    if (phandle != -1) {
        return of_client_package_to_path(spapr->fdt_blob, phandle, buf, len);
    }

    return 0;
}

static uint32_t of_client_write(SpaprMachineState *spapr, uint32_t ihandle,
                                uint32_t buf, uint32_t len)
{
    uint32_t phandle = of_client_instance_to_package(spapr, ihandle);
    SpaprVioDevice *sdev = vty_lookup_phandle(spapr, phandle);
    char tmp[65];
    int toread, toprint, cb = MIN(len, 1024);

    while (cb > 0) {
        toread = MIN(cb + 1, sizeof(tmp));
        readstr(buf, tmp, toread);
        toprint = strlen(tmp);
        if (sdev) {
            vty_putchars(sdev, (uint8_t *) tmp, toprint);
        } else {
            /* We put stdout ihandle to /chosen/stdout, this is fallback */
            printf("DBG[%d]%s", ihandle, tmp);
        }
        buf += toprint;
        cb -= toprint;
    }

    return len;
}

static bool of_client_claim_avail(GArray *claimed, uint64_t virt, uint64_t size)
{
    int i;
    SpaprOfClaimed *c;

    for (i = 0; i < claimed->len; ++i) {
        c = &g_array_index(claimed, SpaprOfClaimed, i);
        if ((c->start <= virt && virt < c->start + c->size) ||
            (virt <= c->start && c->start < virt + size)) {
            return false;
        }
    }

    return true;
}

static void of_client_claim_add(GArray *claimed, uint64_t virt, uint64_t size)
{
    SpaprOfClaimed newclaim;

    newclaim.start = virt;
    newclaim.size = size;
    g_array_append_val(claimed, newclaim);
}

uint64_t spapr_do_of_client_claim(SpaprMachineState *spapr, uint64_t virt,
                                  uint64_t size, uint64_t align)
{
    uint32_t ret;

    if (align == 0) {
        if (!of_client_claim_avail(spapr->claimed, virt, size)) {
            return -1;
        }
        ret = virt;
    } else {
        align = pow2ceil(align);
        spapr->claimed_base = (spapr->claimed_base + align - 1) & ~(align - 1);
        while (1) {
            if (spapr->claimed_base >= spapr->rma_size) {
                perror("Out of memory");
                return -1;
            }
            if (of_client_claim_avail(spapr->claimed, spapr->claimed_base,
                                      size)) {
                break;
            }
            spapr->claimed_base += size;
        }
        ret = spapr->claimed_base;
    }

    spapr->claimed_base = MAX(spapr->claimed_base, ret + size);
    of_client_claim_add(spapr->claimed, virt, size);
    trace_spapr_of_client_claim(virt, size, align, ret);

    return ret;
}

static uint32_t of_client_claim(SpaprMachineState *spapr, uint32_t virt,
                                uint32_t size, uint32_t align)
{
    if (align) {
        return -1;
    }
    if (!of_client_claim_avail(spapr->claimed, virt, size)) {
        return -1;
    }

    spapr->claimed_base = MAX(spapr->claimed_base, virt + size);
    of_client_claim_add(spapr->claimed, virt, size);
    trace_spapr_of_client_claim(virt, size, align, virt);

    return virt;
}

static uint32_t of_client_call_method(SpaprMachineState *spapr,
                                      uint32_t methodaddr, uint32_t param1,
                                      uint32_t param2, uint32_t *ret2)
{
    uint32_t ret = 0;
    char method[256];

    readstr(methodaddr, method, sizeof(method));
    if (strncmp(method, "ibm,client-architecture-support", 31) == 0) {

#define FDT_MAX_SIZE            0x100000
        ret = do_client_architecture_support(POWERPC_CPU(first_cpu), spapr,
                                             param2, FDT_MAX_SIZE);
        *ret2 = 0;
    } else if (strncmp(method, "instantiate-rtas", 16) == 0) {
        uint32_t rtasbase = param2;

        spapr_instantiate_rtas(spapr, rtasbase);
        *ret2 = rtasbase;
    } else {
        trace_spapr_of_client_error_unknown_method(method);
        return -1;
    }

    trace_spapr_of_client_method(method, param1, param2, ret);

    return ret;
}

static void of_client_quiesce(SpaprMachineState *spapr)
{
}

int spapr_h_client(SpaprMachineState *spapr, target_ulong of_client_args)
{
    struct prom_args args = { 0 };
    char service[64];
    unsigned nargs, nret;
    int i, servicelen;

    cpu_physical_memory_read(of_client_args, &args, sizeof(args));
    nargs = be32_to_cpu(args.nargs);
    nret = be32_to_cpu(args.nret);
    readstr(be32_to_cpu(args.service), service, sizeof(service));
    servicelen = strlen(service);

#define cmpservice(s, a, r) \
    _cmpservice(service, servicelen, nargs, nret, (s), sizeof(s), (a), (r))

    if (cmpservice("finddevice", 1, 1)) {
        args.args[nargs] = of_client_finddevice(spapr->fdt_blob,
                                                be32_to_cpu(args.args[0]));
    } else if (cmpservice("getprop", 4, 1)) {
        args.args[nargs] = of_client_getprop(spapr->fdt_blob,
                                             be32_to_cpu(args.args[0]),
                                             be32_to_cpu(args.args[1]),
                                             be32_to_cpu(args.args[2]),
                                             be32_to_cpu(args.args[3]));
    } else if (cmpservice("getproplen", 2, 1)) {
        args.args[nargs] = of_client_getproplen(spapr->fdt_blob,
                                                be32_to_cpu(args.args[0]),
                                                be32_to_cpu(args.args[1]));
    } else if (cmpservice("setprop", 4, 1)) {
        args.args[nargs] = of_client_setprop(spapr,
                                             be32_to_cpu(args.args[0]),
                                             be32_to_cpu(args.args[1]),
                                             be32_to_cpu(args.args[2]),
                                             be32_to_cpu(args.args[3]));
    } else if (cmpservice("nextprop", 3, 1)) {
        args.args[nargs] = of_client_nextprop(spapr->fdt_blob,
                                              be32_to_cpu(args.args[0]),
                                              be32_to_cpu(args.args[1]),
                                              be32_to_cpu(args.args[2]));
    } else if (cmpservice("peer", 1, 1)) {
        args.args[nargs] = of_client_peer(spapr->fdt_blob,
                                          be32_to_cpu(args.args[0]));
    } else if (cmpservice("child", 1, 1)) {
        args.args[nargs] = of_client_child(spapr->fdt_blob,
                                           be32_to_cpu(args.args[0]));
    } else if (cmpservice("parent", 1, 1)) {
        args.args[nargs] = of_client_parent(spapr->fdt_blob,
                                            be32_to_cpu(args.args[0]));
    } else if (cmpservice("open", 1, 1)) {
        args.args[nargs] = spapr_of_client_open(spapr,
                                                be32_to_cpu(args.args[0]));
    } else if (cmpservice("close", 1, 0)) {
        of_client_close(spapr, be32_to_cpu(args.args[0]));
    } else if (cmpservice("instance-to-package", 1, 1)) {
        args.args[nargs] =
            of_client_instance_to_package(spapr,
                                          be32_to_cpu(args.args[0]));
    } else if (cmpservice("package-to-path", 3, 1)) {
        args.args[nargs] = of_client_package_to_path(spapr->fdt_blob,
                                                     be32_to_cpu(args.args[0]),
                                                     be32_to_cpu(args.args[1]),
                                                     be32_to_cpu(args.args[2]));
    } else if (cmpservice("instance-to-path", 3, 1)) {
        args.args[nargs] =
            of_client_instance_to_path(spapr,
                                       be32_to_cpu(args.args[0]),
                                       be32_to_cpu(args.args[1]),
                                       be32_to_cpu(args.args[2]));
    } else if (cmpservice("write", 3, 1)) {
        args.args[nargs] = of_client_write(spapr,
                                           be32_to_cpu(args.args[0]),
                                           be32_to_cpu(args.args[1]),
                                           be32_to_cpu(args.args[2]));
    } else if (cmpservice("claim", 3, 1)) {
        args.args[nargs] = of_client_claim(spapr,
                                           be32_to_cpu(args.args[0]),
                                           be32_to_cpu(args.args[1]),
                                           be32_to_cpu(args.args[2]));
    } else if (cmpservice("call-method", 3, 2)) {
        args.args[nargs] = of_client_call_method(spapr,
                                                 be32_to_cpu(args.args[0]),
                                                 be32_to_cpu(args.args[1]),
                                                 be32_to_cpu(args.args[2]),
                                                 &args.args[nargs + 1]);
    } else if (cmpservice("quiesce", 0, 0)) {
        of_client_quiesce(spapr);
    } else if (cmpservice("exit", 0, 0)) {
        error_report("Stopped as the VM requested \"exit\"");
        vm_stop(RUN_STATE_PAUSED);
    } else {
        trace_spapr_of_client_error_unknown_service(service, nargs, nret);
        args.args[nargs] = -1;
    }

    for (i = 0; i < nret; ++i) {
        args.args[nargs + i] = be32_to_cpu(args.args[nargs + i]);
    }
    cpu_physical_memory_write(of_client_args, &args, sizeof(args));

    return H_SUCCESS;
}
