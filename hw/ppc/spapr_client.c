#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "hw/ppc/spapr.h"
#include "hw/loader.h"
#include "trace.h"

struct prom_args {
        __be32 service;
        __be32 nargs;
        __be32 nret;
        __be32 args[10];
};

#define CLI_PH_MASK     0x0FFFFFFF
#define CLI_INST_PREFIX 0x20000000

#define readstr(pa, buf) cpu_physical_memory_read((pa), (buf), sizeof(buf))

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
        trace_spapr_client_error_param(s, nargscheck, nretcheck, nargs, nret);
        return false;
    }

    return true;
}

static uint32_t client_finddevice(const void *fdt, uint32_t nodeaddr)
{
    char node[256];
    int ret;

    readstr(nodeaddr, node);
    ret = fdt_path_offset(fdt, node);
    if (ret >= 0) {
        ret = fdt_get_phandle(fdt, ret);
    }

    return (uint32_t) ret;
}

static uint32_t client_getprop(const void *fdt, uint32_t nodeph, uint32_t pname,
            uint32_t valaddr, uint32_t vallen)
{
    char propname[64];
    uint32_t ret = 0;
    int proplen = 0;
    const void *prop;

    readstr(pname, propname);

    prop = fdt_getprop_namelen(fdt, fdt_node_offset_by_phandle(fdt, nodeph),
                               propname, strlen(propname), &proplen);
    if (prop) {
        int cb = MIN(proplen, vallen);

        cpu_physical_memory_write(valaddr, prop, cb);
        ret = cb;
    } else if (strncmp(propname, "stdout", 6) == 0 && vallen == sizeof(ret)) {
        ret = cpu_to_be32(1);
        cpu_physical_memory_write(valaddr, &ret, MIN(vallen, sizeof(ret)));
    } else {
        ret = -1;
    }

    return ret;
}

static uint32_t client_getproplen(const void *fdt, uint32_t nodeph,
                                  uint32_t pname)
{
    char propname[64];
    uint32_t ret = 0;
    int proplen = 0;
    const void *prop;

    readstr(pname, propname);

    prop = fdt_getprop_namelen(fdt, fdt_node_offset_by_phandle(fdt, nodeph),
                               propname, strlen(propname), &proplen);
    if (prop) {
        ret = proplen;
    } else if (strncmp(propname, "stdout", 6) == 0) {
        ret = 4;
    } else {
        ret = -1;
    }

    return ret;
}

static uint32_t client_peer(const void *fdt, uint32_t phandle)
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

static uint32_t client_child(const void *fdt, uint32_t phandle)
{
    int ret = fdt_first_subnode(fdt, fdt_node_offset_by_phandle(fdt, phandle));

    if (ret < 0) {
        ret = 0;
    } else {
        ret = fdt_get_phandle(fdt, ret);
    }

    return ret;
}

static uint32_t client_parent(const void *fdt, uint32_t phandle)
{
    int ret = fdt_parent_offset(fdt, fdt_node_offset_by_phandle(fdt, phandle));

    if (ret < 0) {
        ret = 0;
    } else {
        ret = fdt_get_phandle(fdt, ret);
    }

    return ret;
}

static uint32_t client_open(uint32_t phandle)
{
    uint32_t ret = (phandle & CLI_PH_MASK) | CLI_INST_PREFIX;

    return ret;
}

static uint32_t client_instance_to_path(uint32_t instance, uint32_t buf,
                                        uint32_t len)
{
    return 0;
}

static uint32_t client_package_to_path(const void *fdt, uint32_t phandle,
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

static uint32_t client_write(uint32_t instance, uint32_t buf, uint32_t len)
{
    char tmp[len + 1]; /* TODO: do a loop as len might be enourmous*/

    readstr(buf, tmp);
    tmp[len] = 0;
    printf("%s", tmp);

    return len;
}

static uint32_t client_claim(uint32_t virt, uint32_t size, uint32_t align)
{
    uint32_t ret;
    static uint32_t cur = 0xe0000000;

    if (align == 0) {
        if (rom_intersect(virt, size)) {
            ret = -1;
        } else {
            ret = virt;
        }
    } else {
        cur = (cur + align - 1) & ~(align - 1);
        ret = cur;
        cur += align;
    }
    trace_spapr_client_claim(virt, size, align, ret);

    return ret;
}

static uint32_t client_nextprop(const void *fdt, uint32_t phandle,
                                uint32_t prevaddr, uint32_t nameaddr)
{
    int namelen = 0;
    int offset = fdt_node_offset_by_phandle(fdt, phandle);
    char prev[256];
    const struct fdt_property *prop;
    const char *tmp;

    readstr(prevaddr, prev);
    for (offset = fdt_first_property_offset(fdt, offset);
         (offset >= 0);
         (offset = fdt_next_property_offset(fdt, offset))) {

        prop = fdt_get_property_by_offset(fdt, offset, &namelen);
        if (!prop) {
            return 0;
        }

        tmp = fdt_get_string(fdt, fdt32_ld(&prop->nameoff), &namelen);
        if (prev[0] == 0 ||
            strncmp(prev, tmp, MAX(namelen, strlen(prev))) == 0) {

            if (prev[0]) {
                offset = fdt_next_property_offset(fdt, offset);
                if (offset < 0) {
                    return 0;
                }
            }
            prop = fdt_get_property_by_offset(fdt, offset, &namelen);
            if (!prop) {
                return 0;
            }

            tmp = fdt_get_string(fdt, fdt32_ld(&prop->nameoff), &namelen);
            cpu_physical_memory_write(nameaddr, tmp, namelen + 1);
            return 1;
        }
    }

    return 0;
}

static uint32_t client_call_method(SpaprMachineState *sm, uint32_t methodaddr,
                                   uint32_t param1, uint32_t param2,
                                   uint32_t *ret2)
{
    uint32_t ret = 0;
    char method[256];

    readstr(methodaddr, method);
    if (strncmp(method, "ibm,client-architecture-support", 31) == 0) {

#define FDT_MAX_SIZE            0x100000
        ret = do_client_architecture_support(POWERPC_CPU(first_cpu), sm, param2,
                                             0, FDT_MAX_SIZE);
        *ret2 = 0;
    } else if (strncmp(method, "instantiate-rtas", 16) == 0) {
        uint32_t rtasbase = param2;

        spapr_instantiate_rtas(sm, rtasbase);
        *ret2 = rtasbase;
    } else {
        trace_spapr_client_error_unknown_method(method);
        return -1;
    }

    trace_spapr_client_method(method, param1, param2, ret);

    return ret;
}

static void client_quiesce(SpaprMachineState *sm)
{
}

int spapr_h_client(SpaprMachineState *sm, target_ulong client_args)
{
    struct prom_args args = { 0 };
    char service[64];
    unsigned nargs, nret;
    int i, servicelen;

    cpu_physical_memory_read(client_args, &args, sizeof(args));
    nargs = be32_to_cpu(args.nargs);
    nret = be32_to_cpu(args.nret);
    readstr(be32_to_cpu(args.service), service);
    servicelen = strlen(service);

#define cmpservice(s, a, r) \
        _cmpservice(service, servicelen, nargs, nret, (s), sizeof(s), (a), (r))

    if (cmpservice("finddevice", 1, 1)) {
        args.args[nargs] = client_finddevice(sm->fdt_blob,
                                             be32_to_cpu(args.args[0]));
    } else if (cmpservice("getprop", 4, 1)) {
        args.args[nargs] = client_getprop(sm->fdt_blob,
                                          be32_to_cpu(args.args[0]),
                                          be32_to_cpu(args.args[1]),
                                          be32_to_cpu(args.args[2]),
                                          be32_to_cpu(args.args[3]));
    } else if (cmpservice("getproplen", 2, 1)) {
        args.args[nargs] = client_getproplen(sm->fdt_blob,
                                             be32_to_cpu(args.args[0]),
                                             be32_to_cpu(args.args[1]));
    } else if (cmpservice("instance-to-path", 3, 1)) {
        args.args[nargs] = client_instance_to_path(be32_to_cpu(args.args[0]),
                                                   be32_to_cpu(args.args[1]),
                                                   be32_to_cpu(args.args[2]));
    } else if (cmpservice("package-to-path", 3, 1)) {
        args.args[nargs] = client_package_to_path(sm->fdt_blob,
                                                  be32_to_cpu(args.args[0]),
                                                  be32_to_cpu(args.args[1]),
                                                  be32_to_cpu(args.args[2]));
    } else if (cmpservice("write", 3, 1)) {
        args.args[nargs] = client_write(be32_to_cpu(args.args[0]),
                                        be32_to_cpu(args.args[1]),
                                        be32_to_cpu(args.args[2]));
    } else if (cmpservice("peer", 1, 1)) {
        args.args[nargs] = client_peer(sm->fdt_blob,
                                       be32_to_cpu(args.args[0]));
    } else if (cmpservice("child", 1, 1)) {
        args.args[nargs] = client_child(sm->fdt_blob,
                                        be32_to_cpu(args.args[0]));
    } else if (cmpservice("parent", 1, 1)) {
        args.args[nargs] = client_parent(sm->fdt_blob,
                                         be32_to_cpu(args.args[0]));
    } else if (cmpservice("open", 1, 1)) {
        args.args[nargs] = client_open(be32_to_cpu(args.args[0]));
    } else if (cmpservice("call-method", 3, 2)) {
        uint32_t ret2 = 0;

        args.args[nargs] = client_call_method(sm,
                                              be32_to_cpu(args.args[0]),
                                              be32_to_cpu(args.args[1]),
                                              be32_to_cpu(args.args[2]),
                                              &ret2);
        args.args[nargs + 1] = ret2;
    } else if (cmpservice("claim", 3, 1)) {
        args.args[nargs] = client_claim(be32_to_cpu(args.args[0]),
                                        be32_to_cpu(args.args[1]),
                                        be32_to_cpu(args.args[2]));
    } else if (cmpservice("nextprop", 3, 1)) {
        args.args[nargs] = client_nextprop(sm->fdt_blob,
                                           be32_to_cpu(args.args[0]),
                                           be32_to_cpu(args.args[1]),
                                           be32_to_cpu(args.args[2]));
    } else if (cmpservice("quiesce", 0, 0)) {
        client_quiesce(sm);
    } else if (cmpservice("exit", 0, 0)) {
        error_report("Stopped as the VM requested \"exit\"");
        vm_stop(RUN_STATE_PAUSED);
    } else {
        trace_spapr_client_error_unknown_service(service, nargs, nret);
        args.args[nargs] = -1;
    }

    for (i = 0; i < nret; ++i) {
        args.args[nargs + i] = be32_to_cpu(args.args[nargs + i]);
    }
    cpu_physical_memory_write(client_args, &args, sizeof(args));

    return H_SUCCESS;
}
