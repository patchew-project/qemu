#include "qemu/osdep.h"
#include "qemu-common.h"
#include <sys/ioctl.h>
#include <termios.h>
#include "qapi/error.h"
#include "exec/memory.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_vio.h"
#include "hw/ppc/fdt.h"
#include "hw/block/block.h"
#include "sysemu/block-backend.h"
#include "sysemu/sysemu.h"
#include "chardev/char-fe.h"
#include "qom/qom-qobject.h"
#include "elf.h"
#include "hw/ppc/ppc.h"
#include "hw/loader.h"
#include "trace.h"

#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

struct gpt_header {
    char signature[8];
    char revision[4];
    uint32_t header_size;
    uint32_t crc;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    char guid[16];
    uint64_t partition_entries_lba;
    uint32_t nr_partition_entries;
    uint32_t size_partition_entry;
    uint32_t crc_partitions;
};

#define GPT_SIGNATURE "EFI PART"
#define GPT_REVISION "\0\0\1\0" /* revision 1.0 */

struct gpt_entry {
    char partition_type_guid[16];
    char unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    char name[72];                /* UTF-16LE */
};

#define GPT_MIN_PARTITIONS 128
#define GPT_PT_ENTRY_SIZE 128
#define SECTOR_SIZE 512

static int find_prep_partition_on_gpt(BlockBackend *blk, uint8_t *lba01,
                                      uint64_t *offset, uint64_t *size)
{
    unsigned i, partnum, partentrysize;
    int ret;
    struct gpt_header *hdr = (struct gpt_header *) (lba01 + SECTOR_SIZE);
    const char *prep_uuid = "9e1a2d38-c612-4316-aa26-8b49521e5a8b";

    if (memcmp(hdr, "EFI PART", 8)) {
        return -1;
    }

    partnum = le32_to_cpu(hdr->nr_partition_entries);
    partentrysize = le32_to_cpu(hdr->size_partition_entry);

    if (partentrysize < 128 || partentrysize > 512) {
        return -1;
    }

    for (i = 0; i < partnum; ++i) {
        uint8_t partdata[partentrysize];
        struct gpt_entry *entry = (struct gpt_entry *) partdata;
        unsigned long first, last;
        QemuUUID parttype;
        char *uuid;

        ret = blk_pread(blk, 2 * SECTOR_SIZE + i * partentrysize,
                        partdata, sizeof(partdata));
        if (ret < 0) {
            return ret;
        } else if (!ret) {
            return -1;
        }

        memcpy(parttype.data, entry->partition_type_guid, 16);
        parttype = qemu_uuid_bswap(parttype);
        first = le64_to_cpu(entry->first_lba);
        last = le64_to_cpu(entry->last_lba);

        uuid = qemu_uuid_unparse_strdup(&parttype);
        if (!strcmp(uuid, prep_uuid)) {
            *offset = first * SECTOR_SIZE;
            *size = (last - first) * SECTOR_SIZE;
        }
    }

    if (*offset) {
        return 0;
    }

    return -1;
}

struct partition_record {
    uint8_t bootable;
    uint8_t start_head;
    uint32_t start_cylinder;
    uint8_t start_sector;
    uint8_t system;
    uint8_t end_head;
    uint8_t end_cylinder;
    uint8_t end_sector;
    uint32_t start_sector_abs;
    uint32_t nb_sectors_abs;
};

static void read_partition(uint8_t *p, struct partition_record *r)
{
    r->bootable = p[0];
    r->start_head = p[1];
    r->start_cylinder = p[3] | ((p[2] << 2) & 0x0300);
    r->start_sector = p[2] & 0x3f;
    r->system = p[4];
    r->end_head = p[5];
    r->end_cylinder = p[7] | ((p[6] << 2) & 0x300);
    r->end_sector = p[6] & 0x3f;
    r->start_sector_abs = ldl_le_p(p + 8);
    r->nb_sectors_abs   = ldl_le_p(p + 12);
}

static int find_prep_partition(BlockBackend *blk, uint64_t *offset,
                               uint64_t *size)
{
    uint8_t lba01[SECTOR_SIZE * 2];
    int i;
    int ret = -ENOENT;

    ret = blk_pread(blk, 0, lba01, sizeof(lba01));
    if (ret < 0) {
        error_report("error while reading: %s", strerror(-ret));
        exit(EXIT_FAILURE);
    }

    if (lba01[510] != 0x55 || lba01[511] != 0xaa) {
        return find_prep_partition_on_gpt(blk, lba01, offset, size);
    }

    for (i = 0; i < 4; i++) {
        struct partition_record part = { 0 };

        read_partition(&lba01[446 + 16 * i], &part);
        if (!part.system || !part.nb_sectors_abs) {
            continue;
        }

        /* 0xEE == GPT */
        if (part.system == 0xEE) {
            ret = find_prep_partition_on_gpt(blk, lba01, offset, size);
        }
        /* 0x41 == PReP */
        if (part.system == 0x41) {
            *offset = (uint64_t)part.start_sector_abs << 9;
            *size = (uint64_t)part.nb_sectors_abs << 9;
            ret = 0;
        }
    }

    return ret;
}

/*
 * Below is a compiled version of RTAS blob and OF client interface entry point.
 *
 * gcc -nostdlib  -mbig -o spapr-rtas.img spapr-rtas.S
 * objcopy  -O binary -j .text  spapr-rtas.img spapr-rtas.bin
 *
 *   .globl  _start
 *   _start:
 *           mr      4,3
 *           lis     3,KVMPPC_H_RTAS@h
 *           ori     3,3,KVMPPC_H_RTAS@l
 *           sc      1
 *           blr
 * ...
 */
static const uint8_t rtas_blob[] = {
    0x7c, 0x64, 0x1b, 0x78,
    0x3c, 0x60, 0x00, 0x00,
    0x60, 0x63, 0xf0, 0x00,
    0x44, 0x00, 0x00, 0x22,
    0x4e, 0x80, 0x00, 0x20
};

/*
 * ...
 *           mr      4,3
 *           lis     3,KVMPPC_H_OF_CLIENT@h
 *           ori     3,3,KVMPPC_H_OF_CLIENT@l
 *           sc      1
 *           blr
 */
static const uint8_t of_client_blob[] = {
    0x7c, 0x64, 0x1b, 0x78,
    0x3c, 0x60, 0x00, 0x00,
    0x60, 0x63, 0xf0, 0x05,
    0x44, 0x00, 0x00, 0x22,
    0x4e, 0x80, 0x00, 0x20
};

typedef struct {
    DeviceState *dev;
    CharBackend *cbe;
    BlockBackend *blk;
    uint64_t blk_pos;
    uint16_t blk_physical_block_size;
    char *path; /* the path used to open the instance */
    uint32_t phandle;
} SpaprOfInstance;

/*
 * OF 1275 "nextprop" description suggests is it 32 bytes max but
 * LoPAPR defines "ibm,query-interrupt-source-number" which is 33 chars long.
 */
#define OF_PROPNAME_LEN_MAX 64

/* Copied from SLOF, and 4K is definitely not enough for GRUB */
#define OF_STACK_SIZE       0x8000

/* Defined as Big Endian */
struct prom_args {
    uint32_t service;
    uint32_t nargs;
    uint32_t nret;
    uint32_t args[10];
};

static void readstr(hwaddr pa, char *buf, int size)
{
    cpu_physical_memory_read(pa, buf, size);
    if (buf[size - 1] != '\0') {
        buf[size - 1] = '\0';
        if (strlen(buf) == size - 1) {
            trace_spapr_of_client_error_str_truncated(buf, size);
        }
    }
}

static bool cmpservice(const char *s, size_t len,
                       unsigned nargs, unsigned nret,
                       const char *s1, size_t len1,
                       unsigned nargscheck, unsigned nretcheck)
{
    if (strcmp(s, s1)) {
        return false;
    }
    if ((nargscheck && (nargs != nargscheck)) ||
        (nretcheck && (nret != nretcheck))) {
        trace_spapr_of_client_error_param(s, nargscheck, nretcheck, nargs,
                                          nret);
        return false;
    }

    return true;
}

static void split_path(const char *fullpath, char **node, char **unit,
                       char **part)
{
    const char *c, *p = NULL, *u = NULL;

    *node = *unit = *part = NULL;

    if (fullpath[0] == '\0') {
        *node = g_strdup(fullpath);
        return;
    }

    for (c = fullpath + strlen(fullpath) - 1; c > fullpath; --c) {
        if (*c == '/') {
            break;
        }
        if (*c == ':') {
            p = c + 1;
            continue;
        }
        if (*c == '@') {
            u = c + 1;
            continue;
        }
    }

    if (p && u && p < u) {
        p = NULL;
    }

    if (u && p) {
        *node = g_strndup(fullpath, u - fullpath - 1);
        *unit = g_strndup(u, p - u - 1);
        *part = g_strdup(p);
    } else if (!u && p) {
        *node = g_strndup(fullpath, p - fullpath - 1);
        *part = g_strdup(p);
    } else if (!p && u) {
        *node = g_strndup(fullpath, u - fullpath - 1);
        *unit = g_strdup(u);
    } else {
        *node = g_strdup(fullpath);
    }
}

static void prop_format(char *tval, int tlen, const void *prop, int len)
{
    int i;
    const char *c;
    char *t;
    const char bin[] = "...";

    for (i = 0, c = prop; i < len; ++i, ++c) {
        if (*c == '\0' && i == len - 1) {
            strncpy(tval, prop, tlen - 1);
            return;
        }
        if (*c < 0x20 || *c >= 0x80) {
            /* Not reliably printable string so assume it is binary */
            break;
        }
    }

    /* Accidentally the binary will look like big endian (which it is) */
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

static int of_client_fdt_path_offset(const void *fdt, const char *node,
                                     const char *unit)
{
    int offset;

    offset = fdt_path_offset(fdt, node);

    if (offset < 0 && unit) {
        char *tmp = g_strdup_printf("%s@%s", node, unit);

        offset = fdt_path_offset(fdt, tmp);
        g_free(tmp);
    }

    return offset;
}

static uint32_t of_client_finddevice(const void *fdt, uint32_t nodeaddr)
{
    char *node, *unit, *part;
    char fullnode[1024];
    uint32_t ret = -1;
    int offset;

    readstr(nodeaddr, fullnode, sizeof(fullnode));

    split_path(fullnode, &node, &unit, &part);
    offset = of_client_fdt_path_offset(fdt, node, unit);
    if (offset >= 0) {
        ret = fdt_get_phandle(fdt, offset);
    }
    trace_spapr_of_client_finddevice(fullnode, ret);
    g_free(node);
    g_free(unit);
    g_free(part);
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
    char trval[64] = "";
    int nodeoff = fdt_node_offset_by_phandle(fdt, nodeph);

    readstr(pname, propname, sizeof(propname));
    if (strcmp(propname, "name") == 0) {
        prop = fdt_get_name(fdt, nodeoff, &proplen);
        proplen += 1;
    } else {
        prop = fdt_getprop(fdt, nodeoff, propname, &proplen);
    }

    if (prop) {
        int cb = MIN(proplen, vallen);

        cpu_physical_memory_write(valaddr, prop, cb);
        /*
         * OF1275 says:
         * "Size is either the actual size of the property, or –1 if name
         * does not exist", hence returning proplen instead of cb.
         */
        ret = proplen;
        prop_format(trval, sizeof(trval), prop, ret);
    } else {
        ret = -1;
    }
    trace_spapr_of_client_getprop(nodeph, propname, ret, trval);

    return ret;
}

static uint32_t of_client_getproplen(const void *fdt, uint32_t nodeph,
                                     uint32_t pname)
{
    char propname[OF_PROPNAME_LEN_MAX + 1];
    uint32_t ret = 0;
    int proplen = 0;
    const void *prop;
    int nodeoff = fdt_node_offset_by_phandle(fdt, nodeph);

    readstr(pname, propname, sizeof(propname));
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
    trace_spapr_of_client_getproplen(nodeph, propname, ret);

    return ret;
}

static uint32_t of_client_setprop(SpaprMachineState *spapr,
                                  uint32_t nodeph, uint32_t pname,
                                  uint32_t valaddr, uint32_t vallen)
{
    char propname[OF_PROPNAME_LEN_MAX + 1];
    uint32_t ret = -1;
    int offset;
    char trval[64] = "";

    readstr(pname, propname, sizeof(propname));
    /*
     * We only allow changing properties which we know how to update on
     * the QEMU side.
     */
    if (vallen == sizeof(uint32_t)) {
        uint32_t val32 = ldl_be_phys(first_cpu->as, valaddr);

        if ((strcmp(propname, "linux,rtas-base") == 0) ||
            (strcmp(propname, "linux,rtas-entry") == 0)) {
            spapr->rtas_base = val32;
        } else if (strcmp(propname, "linux,initrd-start") == 0) {
            spapr->initrd_base = val32;
        } else if (strcmp(propname, "linux,initrd-end") == 0) {
            spapr->initrd_size = val32 - spapr->initrd_base;
        } else {
            goto trace_exit;
        }
    } else if (vallen == sizeof(uint64_t)) {
        uint64_t val64 = ldq_be_phys(first_cpu->as, valaddr);

        if (strcmp(propname, "linux,initrd-start") == 0) {
            spapr->initrd_base = val64;
        } else if (strcmp(propname, "linux,initrd-end") == 0) {
            spapr->initrd_size = val64 - spapr->initrd_base;
        } else {
            goto trace_exit;
        }
    } else if (strcmp(propname, "bootargs") == 0) {
        char val[1024];

        readstr(valaddr, val, sizeof(val));
        g_free(spapr->bootargs);
        spapr->bootargs = g_strdup(val);
    } else {
        goto trace_exit;
    }

    offset = fdt_node_offset_by_phandle(spapr->fdt_blob, nodeph);
    if (offset >= 0) {
        uint8_t data[vallen];

        cpu_physical_memory_read(valaddr, data, vallen);
        if (!fdt_setprop(spapr->fdt_blob, offset, propname, data, vallen)) {
            ret = vallen;
            prop_format(trval, sizeof(trval), data, ret);
        }
    }

trace_exit:
    trace_spapr_of_client_setprop(nodeph, propname, trval, ret);

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

static DeviceState *of_client_find_qom_dev(BusState *bus, const char *path)
{
    BusChild *kid;

    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        const char *p = qdev_get_fw_dev_path(kid->child);
        BusState *child;

        if (p && strcmp(path, p) == 0) {
            return kid->child;
        }
        QLIST_FOREACH(child, &kid->child->child_bus, sibling) {
            DeviceState *d = of_client_find_qom_dev(child, path);

            if (d) {
                return d;
            }
        }
    }
    return NULL;
}

static uint32_t spapr_of_client_open(SpaprMachineState *spapr, const char *path)
{
    int offset;
    uint32_t ret = 0;
    SpaprOfInstance *inst = NULL;
    char *node, *unit, *part;

    if (spapr->of_instance_last == 0xFFFFFFFF) {
        /* We do not recycle ihandles yet */
        goto trace_exit;
    }

    split_path(path, &node, &unit, &part);
    if (part && strcmp(part, "0")) {
        error_report("Error: Do not do partitions now");
        g_free(part);
        part = NULL;
    }

    offset = of_client_fdt_path_offset(spapr->fdt_blob, node, unit);
    if (offset < 0) {
        trace_spapr_of_client_error_unknown_path(path);
        goto trace_exit;
    }

    inst = g_new0(SpaprOfInstance, 1);
    inst->phandle = fdt_get_phandle(spapr->fdt_blob, offset);
    g_assert(inst->phandle);
    ++spapr->of_instance_last;

    inst->dev = of_client_find_qom_dev(sysbus_get_default(), node);
    if (!inst->dev) {
        char *tmp = g_strdup_printf("%s@%s", node, unit);
        inst->dev = of_client_find_qom_dev(sysbus_get_default(), tmp);
        g_free(tmp);
    }
    inst->path = g_strdup(path);
    g_hash_table_insert(spapr->of_instances,
                        GINT_TO_POINTER(spapr->of_instance_last),
                        inst);
    ret = spapr->of_instance_last;

    if (inst->dev) {
        const char *cdevstr = object_property_get_str(OBJECT(inst->dev),
                                                      "chardev", NULL);
        const char *blkstr = object_property_get_str(OBJECT(inst->dev),
                                                     "drive", NULL);

        if (cdevstr) {
            Chardev *cdev = qemu_chr_find(cdevstr);

            if (cdev) {
                inst->cbe = cdev->be;
            }
        } else if (blkstr) {
            BlockConf conf = { 0 };

            inst->blk = blk_by_name(blkstr);
            conf.blk = inst->blk;
            blkconf_blocksizes(&conf);
            inst->blk_physical_block_size = conf.physical_block_size;
        }
    }

trace_exit:
    trace_spapr_of_client_open(path, inst ? inst->phandle : 0, ret);
    g_free(node);
    g_free(unit);
    g_free(part);

    return ret;
}

static uint32_t of_client_open(SpaprMachineState *spapr, uint32_t pathaddr)
{
    char path[256];

    readstr(pathaddr, path, sizeof(path));

    return spapr_of_client_open(spapr, path);
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
    gpointer instp = g_hash_table_lookup(spapr->of_instances,
                                         GINT_TO_POINTER(ihandle));
    uint32_t ret = -1;

    if (instp) {
        ret = ((SpaprOfInstance *)instp)->phandle;
    }
    trace_spapr_of_client_instance_to_package(ihandle, ret);

    return ret;
}

static uint32_t of_client_package_to_path(const void *fdt, uint32_t phandle,
                                          uint32_t buf, uint32_t len)
{
    uint32_t ret = -1;
    char tmp[256] = "";

    if (0 == fdt_get_path(fdt, fdt_node_offset_by_phandle(fdt, phandle), tmp,
                          sizeof(tmp))) {
        tmp[sizeof(tmp) - 1] = 0;
        ret = MIN(len, strlen(tmp) + 1);
        cpu_physical_memory_write(buf, tmp, ret);
    }

    trace_spapr_of_client_package_to_path(phandle, tmp, ret);

    return ret;
}

static uint32_t of_client_instance_to_path(SpaprMachineState *spapr,
                                           uint32_t ihandle, uint32_t buf,
                                           uint32_t len)
{
    uint32_t ret = -1;
    uint32_t phandle = of_client_instance_to_package(spapr, ihandle);
    char tmp[256] = "";

    if (phandle != -1) {
        if (0 == fdt_get_path(spapr->fdt_blob,
                              fdt_node_offset_by_phandle(spapr->fdt_blob,
                                                         phandle),
                              tmp, sizeof(tmp))) {
            tmp[sizeof(tmp) - 1] = 0;
            ret = MIN(len, strlen(tmp) + 1);
            cpu_physical_memory_write(buf, tmp, ret);
        }
    }
    trace_spapr_of_client_instance_to_path(ihandle, phandle, tmp, ret);

    return ret;
}

static uint32_t of_client_write(SpaprMachineState *spapr, uint32_t ihandle,
                                uint32_t buf, uint32_t len)
{
    char tmp[256];
    int toread, toprint, cb = MIN(len, 1024);
    SpaprOfInstance *inst = (SpaprOfInstance *)
        g_hash_table_lookup(spapr->of_instances, GINT_TO_POINTER(ihandle));

    while (cb > 0) {
        toread = MIN(cb, sizeof(tmp) - 1);

        cpu_physical_memory_read(buf, tmp, toread);

        toprint = toread;
        if (inst) {
            if (inst->cbe) {
                toprint = qemu_chr_fe_write_all(inst->cbe, (uint8_t *) tmp,
                                                toprint);
            } else if (inst->blk) {
                trace_spapr_of_client_blk_write(ihandle, len);
            }
        } else {
            /* We normally open stdout so this is fallback */
            tmp[toprint] = '\0';
            printf("DBG[%d]%s", ihandle, tmp);
        }
        buf += toprint;
        cb -= toprint;
    }

    return len;
}

static uint32_t of_client_read(SpaprMachineState *spapr, uint32_t ihandle,
                               uint32_t bufaddr, uint32_t len)
{
    uint32_t ret = 0;
    SpaprOfInstance *inst = (SpaprOfInstance *)
        g_hash_table_lookup(spapr->of_instances, GINT_TO_POINTER(ihandle));

    if (inst) {
        hwaddr xlat = 0;
        hwaddr xlen = len;
        MemoryRegion *mr = address_space_translate(&address_space_memory,
                                                   bufaddr, &xlat, &xlen, true,
                                                   MEMTXATTRS_UNSPECIFIED);

        if (mr && xlen == len) {
            uint8_t *buf = memory_region_get_ram_ptr(mr) + xlat;

            if (inst->cbe) {
                SpaprVioDevice *sdev = VIO_SPAPR_DEVICE(inst->dev);

                ret = vty_getchars(sdev, buf, len); /* qemu_chr_fe_read_all? */
            } else if (inst->blk) {
                int rc = blk_pread(inst->blk, inst->blk_pos, buf, len);

                if (rc > 0) {
                    ret = rc;
                }
                trace_spapr_of_client_blk_read(ihandle, inst->blk_pos, len,
                                               ret);
                if (rc > 0) {
                    inst->blk_pos += rc;
                }
            }
        }
    }

    return ret;
}

static uint32_t of_client_seek(SpaprMachineState *spapr, uint32_t ihandle,
                               uint32_t hi, uint32_t lo)
{
    uint32_t ret = -1;
    uint64_t pos = ((uint64_t) hi << 32) | lo;
    SpaprOfInstance *inst = (SpaprOfInstance *)
        g_hash_table_lookup(spapr->of_instances, GINT_TO_POINTER(ihandle));

    if (inst) {
        if (inst->blk) {
            inst->blk_pos = pos;
            ret = 1;
            trace_spapr_of_client_blk_seek(ihandle, pos, ret);
        }
    }

    return ret;
}

static void of_client_clamed_dump(GArray *claimed)
{
#ifdef DEBUG
    int i;
    SpaprOfClaimed c;

    for (i = 0; i < claimed->len; ++i) {
        c = g_array_index(claimed, SpaprOfClaimed, i);
        error_printf("CLAIMED %lx..%lx size=%ld\n", c.start, c.start + c.size,
                     c.size);
    }
#endif
}

static bool of_client_claim_avail(GArray *claimed, uint64_t virt, uint64_t size)
{
    int i;
    SpaprOfClaimed c;

    for (i = 0; i < claimed->len; ++i) {
        c = g_array_index(claimed, SpaprOfClaimed, i);
        if ((c.start <= virt && virt < c.start + c.size) ||
            (virt <= c.start && c.start < virt + size)) {
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

/*
 * "claim" claims memory at @virt if @align==0; otherwise it allocates
 * memory at the requested alignment.
 */
static void of_client_dt_memory_available(void *fdt, GArray *claimed,
                                          uint64_t base);

static uint64_t of_client_claim(SpaprMachineState *spapr, uint64_t virt,
                                uint64_t size, uint64_t align)
{
    uint64_t ret;

    if (align == 0) {
        if (!of_client_claim_avail(spapr->claimed, virt, size)) {
            ret = -1;
        } else {
            ret = virt;
        }
    } else {
        spapr->claimed_base = ALIGN(spapr->claimed_base, align);
        while (1) {
            if (spapr->claimed_base >= spapr->rma_size) {
                error_report("Out of RMA memory for the OF client");
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

    if (ret != -1) {
        spapr->claimed_base = MAX(spapr->claimed_base, ret + size);
        of_client_claim_add(spapr->claimed, ret, size);
        /* The client reads "/memory@0/available" to know where it can claim */
        of_client_dt_memory_available(spapr->fdt_blob, spapr->claimed,
                                      spapr->claimed_base);
    }
    trace_spapr_of_client_claim(virt, size, align, ret);

    return ret;
}

static uint32_t of_client_release(SpaprMachineState *spapr, uint64_t virt,
                                  uint64_t size)
{
    uint32_t ret = -1;
    int i;
    GArray *claimed = spapr->claimed;
    SpaprOfClaimed c;

    for (i = 0; i < claimed->len; ++i) {
        c = g_array_index(claimed, SpaprOfClaimed, i);
        if (c.start == virt && c.size == size) {
            g_array_remove_index(claimed, i);
            ret = 0;
            break;
        }
    }

    trace_spapr_of_client_release(virt, size, ret);

    return ret;
}

static void of_client_instantiate_rtas(SpaprMachineState *spapr, uint32_t base)
{
    uint64_t check_claim = of_client_claim(spapr, base, sizeof(rtas_blob), 0);
    /*
     * The client should have claimed RTAS memory, make sure it has or
     * just claim it here with a warning.
     */
    if (check_claim != -1) {
        error_report("The OF client did not claim RTAS memory at 0x%x", base);
    }
    spapr->rtas_base = base;
    cpu_physical_memory_write(base, rtas_blob, sizeof(rtas_blob));
}

static uint32_t of_client_call_method(SpaprMachineState *spapr,
                                      uint32_t methodaddr, uint32_t ihandle,
                                      uint32_t param1, uint32_t param2,
                                      uint32_t param3, uint32_t param4,
                                      uint32_t *ret2)
{
    uint32_t ret = -1;
    char method[256] = "";
    SpaprOfInstance *inst = NULL;

    if (!ihandle) {
        goto trace_exit;
    }

    inst = (SpaprOfInstance *) g_hash_table_lookup(spapr->of_instances,
                                                   GINT_TO_POINTER(ihandle));
    if (!inst) {
        goto trace_exit;
    }

    readstr(methodaddr, method, sizeof(method));

    if (strcmp(inst->path, "/") == 0) {
        if (strcmp(method, "ibm,client-architecture-support") == 0) {
            ret = do_client_architecture_support(POWERPC_CPU(first_cpu), spapr,
                                                 param1, FDT_MAX_SIZE);
            *ret2 = 0;
        }
    } else if (strcmp(inst->path, "/rtas") == 0) {
        if (strcmp(method, "instantiate-rtas") == 0) {
            of_client_instantiate_rtas(spapr, param1);
            ret = 0;
            *ret2 = param1; /* rtasbase */
        }
    } else if (inst->cbe) {
        if (strcmp(method, "color!") == 0) {
            /* do not bother about colors now */
            ret = 0;
        }
    } else if (inst->blk) {
        if (strcmp(method, "block-size") == 0) {
            ret = 0;
            *ret2 = inst->blk_physical_block_size;
        } else if (strcmp(method, "#blocks") == 0) {
            ret = 0;
            *ret2 = blk_getlength(inst->blk) / inst->blk_physical_block_size;
        }
     } else if (inst->dev) {
        if (strcmp(method, "vscsi-report-luns") == 0) {
            /* TODO: Not implemented yet, not clear when it is really needed */
            ret = -1;
            *ret2 = 1;
        }
    } else {
        trace_spapr_of_client_error_unknown_method(method);
    }

trace_exit:
    trace_spapr_of_client_method(ihandle, method, param1, ret, *ret2);

    return ret;
}

static uint32_t of_client_call_interpret(SpaprMachineState *spapr,
                                         uint32_t cmdaddr, uint32_t param1,
                                         uint32_t param2, uint32_t *ret2)
{
    uint32_t ret = -1;
    char cmd[256] = "";

    readstr(cmdaddr, cmd, sizeof(cmd));
    trace_spapr_of_client_interpret(cmd, param1, param2, ret, *ret2);

    return ret;
}

static void of_client_quiesce(SpaprMachineState *spapr)
{
    /* We could as well just free the blob as there is no use for it from now */
    int rc = fdt_pack(spapr->fdt_blob);
    /* Should only fail if we've built a corrupted tree */
    assert(rc == 0);

    spapr->fdt_size = fdt_totalsize(spapr->fdt_blob);
    spapr->fdt_initial_size = spapr->fdt_size;
    of_client_clamed_dump(spapr->claimed);
}

static target_ulong spapr_h_of_client(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                      target_ulong opcode, target_ulong *args)
{
    target_ulong of_client_args = ppc64_phys_to_real(args[0]);
    struct prom_args pargs = { 0 };
    char service[64];
    unsigned nargs, nret;
    int i, servicelen;

    cpu_physical_memory_read(of_client_args, &pargs, sizeof(pargs));
    nargs = be32_to_cpu(pargs.nargs);
    nret = be32_to_cpu(pargs.nret);
    readstr(be32_to_cpu(pargs.service), service, sizeof(service));
    servicelen = strlen(service);

    if (nargs >= ARRAY_SIZE(pargs.args)) {
        /* Bounds checking: something is just very wrong */
        return H_PARAMETER;
    }

#define cmpserv(s, a, r) \
    cmpservice(service, servicelen, nargs, nret, (s), sizeof(s), (a), (r))

    if (cmpserv("finddevice", 1, 1)) {
        pargs.args[nargs] =
            of_client_finddevice(spapr->fdt_blob,
                                 be32_to_cpu(pargs.args[0]));
    } else if (cmpserv("getprop", 4, 1)) {
        pargs.args[nargs] =
            of_client_getprop(spapr->fdt_blob,
                              be32_to_cpu(pargs.args[0]),
                              be32_to_cpu(pargs.args[1]),
                              be32_to_cpu(pargs.args[2]),
                              be32_to_cpu(pargs.args[3]));
    } else if (cmpserv("getproplen", 2, 1)) {
        pargs.args[nargs] =
            of_client_getproplen(spapr->fdt_blob,
                                 be32_to_cpu(pargs.args[0]),
                                 be32_to_cpu(pargs.args[1]));
    } else if (cmpserv("setprop", 4, 1)) {
        pargs.args[nargs] =
            of_client_setprop(spapr,
                              be32_to_cpu(pargs.args[0]),
                              be32_to_cpu(pargs.args[1]),
                              be32_to_cpu(pargs.args[2]),
                              be32_to_cpu(pargs.args[3]));
    } else if (cmpserv("nextprop", 3, 1)) {
        pargs.args[nargs] =
            of_client_nextprop(spapr->fdt_blob,
                               be32_to_cpu(pargs.args[0]),
                               be32_to_cpu(pargs.args[1]),
                               be32_to_cpu(pargs.args[2]));
    } else if (cmpserv("peer", 1, 1)) {
        pargs.args[nargs] =
            of_client_peer(spapr->fdt_blob,
                           be32_to_cpu(pargs.args[0]));
    } else if (cmpserv("child", 1, 1)) {
        pargs.args[nargs] =
            of_client_child(spapr->fdt_blob,
                            be32_to_cpu(pargs.args[0]));
    } else if (cmpserv("parent", 1, 1)) {
        pargs.args[nargs] =
            of_client_parent(spapr->fdt_blob,
                             be32_to_cpu(pargs.args[0]));
    } else if (cmpserv("open", 1, 1)) {
        pargs.args[nargs] = of_client_open(spapr, be32_to_cpu(pargs.args[0]));
    } else if (cmpserv("close", 1, 0)) {
        of_client_close(spapr, be32_to_cpu(pargs.args[0]));
    } else if (cmpserv("instance-to-package", 1, 1)) {
        pargs.args[nargs] =
            of_client_instance_to_package(spapr,
                                          be32_to_cpu(pargs.args[0]));
    } else if (cmpserv("package-to-path", 3, 1)) {
        pargs.args[nargs] =
            of_client_package_to_path(spapr->fdt_blob,
                                      be32_to_cpu(pargs.args[0]),
                                      be32_to_cpu(pargs.args[1]),
                                      be32_to_cpu(pargs.args[2]));
    } else if (cmpserv("instance-to-path", 3, 1)) {
        pargs.args[nargs] =
            of_client_instance_to_path(spapr,
                                       be32_to_cpu(pargs.args[0]),
                                       be32_to_cpu(pargs.args[1]),
                                       be32_to_cpu(pargs.args[2]));
    } else if (cmpserv("write", 3, 1)) {
        pargs.args[nargs] =
            of_client_write(spapr,
                            be32_to_cpu(pargs.args[0]),
                            be32_to_cpu(pargs.args[1]),
                            be32_to_cpu(pargs.args[2]));
    } else if (cmpserv("read", 3, 1)) {
        pargs.args[nargs] =
            of_client_read(spapr,
                           be32_to_cpu(pargs.args[0]),
                           be32_to_cpu(pargs.args[1]),
                           be32_to_cpu(pargs.args[2]));
    } else if (cmpserv("seek", 3, 1)) {
        pargs.args[nargs] =
            of_client_seek(spapr,
                           be32_to_cpu(pargs.args[0]),
                           be32_to_cpu(pargs.args[1]),
                           be32_to_cpu(pargs.args[2]));
    } else if (cmpserv("claim", 3, 1)) {
        pargs.args[nargs] =
            of_client_claim(spapr,
                            be32_to_cpu(pargs.args[0]),
                            be32_to_cpu(pargs.args[1]),
                            be32_to_cpu(pargs.args[2]));
    } else if (cmpserv("release", 2, 0)) {
        pargs.args[nargs] =
            of_client_release(spapr,
                              be32_to_cpu(pargs.args[0]),
                              be32_to_cpu(pargs.args[1]));
    } else if (cmpserv("call-method", 0, 0)) {
        pargs.args[nargs] =
            of_client_call_method(spapr,
                                  be32_to_cpu(pargs.args[0]),
                                  be32_to_cpu(pargs.args[1]),
                                  be32_to_cpu(pargs.args[2]),
                                  be32_to_cpu(pargs.args[3]),
                                  be32_to_cpu(pargs.args[4]),
                                  be32_to_cpu(pargs.args[5]),
                                  &pargs.args[nargs + 1]);
    } else if (cmpserv("interpret", 0, 0)) {
        pargs.args[nargs] =
            of_client_call_interpret(spapr,
                                     be32_to_cpu(pargs.args[0]),
                                     be32_to_cpu(pargs.args[1]),
                                     be32_to_cpu(pargs.args[2]),
                                     &pargs.args[nargs + 1]);
    } else if (cmpserv("milliseconds", 0, 1)) {
        pargs.args[nargs] = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    } else if (cmpserv("quiesce", 0, 0)) {
        of_client_quiesce(spapr);
    } else if (cmpserv("exit", 0, 0)) {
        error_report("Stopped as the VM requested \"exit\"");
        vm_stop(RUN_STATE_PAUSED); /* Or qemu_system_guest_panicked(NULL); ? */
    } else {
        trace_spapr_of_client_error_unknown_service(service, nargs, nret);
        pargs.args[nargs] = -1;
    }

    for (i = 0; i < nret; ++i) {
        pargs.args[nargs + i] = be32_to_cpu(pargs.args[nargs + i]);
    }

    /* Copy what is needed as GRUB allocates only required minimum on stack */
    cpu_physical_memory_write(of_client_args, &pargs,
                              sizeof(uint32_t) * (3 + nargs + nret));

    return H_SUCCESS;
}

static void of_instance_free(gpointer data)
{
    SpaprOfInstance *inst = (SpaprOfInstance *) data;

    g_free(inst->path);
    g_free(inst);
}

void spapr_setup_of_client(SpaprMachineState *spapr, target_ulong *stack_ptr,
                           target_ulong *prom_entry)
{
    if (spapr->claimed) {
        g_array_unref(spapr->claimed);
    }
    if (spapr->of_instances) {
        g_hash_table_unref(spapr->of_instances);
    }

    spapr->claimed = g_array_new(false, false, sizeof(SpaprOfClaimed));
    spapr->of_instances = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                                NULL, of_instance_free);

    *stack_ptr = of_client_claim(spapr, 0, OF_STACK_SIZE, OF_STACK_SIZE);
    if (*stack_ptr == -1) {
        error_report("Memory allocation for stack failed");
        exit(1);
    }
    /*
     * Stack grows downwards and we also reserve here space for
     * the minimum stack frame.
     */
    *stack_ptr += OF_STACK_SIZE - 0x20;

    *prom_entry = of_client_claim(spapr, 0, sizeof(of_client_blob),
                                  sizeof(of_client_blob));
    if (*prom_entry == -1) {
        error_report("Memory allocation for OF client failed");
        exit(1);
    }

    cpu_physical_memory_write(*prom_entry, of_client_blob,
                              sizeof(of_client_blob));

    if (of_client_claim(spapr, spapr->kernel_addr,
                        spapr->kernel_size, 0) == -1) {
        error_report("Memory for kernel is in use");
        exit(1);
    }

    if (spapr->initrd_size &&
        of_client_claim(spapr, spapr->initrd_base,
                        spapr->initrd_size, 0) == -1) {
        error_report("Memory for initramdisk is in use");
        exit(1);
    }

    /*
     * We skip writing FDT as nothing expects it; OF client interface is
     * going to be used for reading the device tree.
     */
}

static gint of_claimed_compare_func(gconstpointer a, gconstpointer b)
{
    return ((SpaprOfClaimed *)a)->start - ((SpaprOfClaimed *)b)->start;
}

static void of_client_dt_memory_available(void *fdt, GArray *claimed,
                                          uint64_t base)
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
    of_client_clamed_dump(claimed);

    avail = g_malloc0(sizeof(uint64_t) * 2 * claimed->len);
    for (i = 0, n = 0; i < claimed->len; ++i) {
        SpaprOfClaimed c = g_array_index(claimed, SpaprOfClaimed, i);

        avail[n].start = c.start + c.size;
        if (i < claimed->len - 1) {
            SpaprOfClaimed cn = g_array_index(claimed, SpaprOfClaimed, i + 1);

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

void spapr_of_client_dt(SpaprMachineState *spapr, void *fdt)
{
    uint32_t phandle;
    int i, offset, proplen = 0;
    const void *prop;
    bool found = false;
    GArray *phandles = g_array_new(false, false, sizeof(uint32_t));
    const char *nodename;
    char *stdout_path = spapr_vio_stdout_path(spapr->vio_bus);
    int aliases;
    aliases = fdt_add_subnode(fdt, 0, "aliases");

    if (stdout_path) {
        fdt_setprop_string(fdt, aliases, "hvterm", stdout_path);
    }

    /* Add options now, doing it at the end of this __func__ breaks it :-/ */
    offset = fdt_add_subnode(fdt, 0, "options");
    if (offset > 0) {
        struct winsize ws;

        if (ioctl(1, TIOCGWINSZ, &ws) != -1) {
            _FDT(fdt_setprop_cell(fdt, offset, "screen-#columns", ws.ws_col));
            _FDT(fdt_setprop_cell(fdt, offset, "screen-#rows", ws.ws_row));
        }
        _FDT(fdt_setprop_cell(fdt, offset, "real-mode?", 1));
    }

    /* Add "disk" nodes to SCSI hosts */
    for (offset = fdt_next_node(fdt, -1, NULL), phandle = 1;
         offset >= 0;
         offset = fdt_next_node(fdt, offset, NULL), ++phandle) {

        nodename = fdt_get_name(fdt, offset, NULL);
        if (strncmp(nodename, "scsi@", 5) == 0 ||
            strncmp(nodename, "v-scsi@", 7) == 0) {
            int disk_node_off = fdt_add_subnode(fdt, offset, "disk");

            fdt_setprop_string(fdt, disk_node_off, "device_type", "block");
        }
    }

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

    of_client_dt_memory_available(fdt, spapr->claimed, spapr->claimed_base);

    /* Advertise RTAS presense */
    offset = fdt_path_offset(fdt, "/rtas");
    _FDT(offset);
    _FDT(fdt_setprop_cell(fdt, offset, "rtas-size", sizeof(rtas_blob)));
}

void spapr_of_client_dt_finalize(SpaprMachineState *spapr)
{
    void *fdt = spapr->fdt_blob;
    char *stdout_path = spapr_vio_stdout_path(spapr->vio_bus);
    int chosen = fdt_path_offset(fdt, "/chosen");
    size_t cb = 0;
    char *bootlist = get_boot_devices_list(&cb);

    /*
     * SLOF-less setup requires an open instance of stdout for early
     * kernel printk. By now all phandles are settled so we can open
     * the default serial console.
     */
    if (stdout_path) {
        _FDT(fdt_setprop_cell(fdt, chosen, "stdout",
                              spapr_of_client_open(spapr, stdout_path)));
        _FDT(fdt_setprop_cell(fdt, chosen, "stdin",
                              spapr_of_client_open(spapr, stdout_path)));
    }

    if (bootlist) {
        _FDT(fdt_setprop_string(fdt, chosen, "bootpath", bootlist));
        _FDT(fdt_setprop_string(fdt, chosen, "bootargs",
                                spapr->bootargs ? spapr->bootargs : ""));
    }
}

static void spapr_of_client_machine_ready(Notifier *n, void *opaque)
{
    SpaprMachineState *spapr = container_of(n, SpaprMachineState,
                                            machine_ready);
    size_t cb = 0;
    char *bootlist = get_boot_devices_list(&cb);
    const char *blkstr;
    BlockBackend *blk;
    char *cur, *next;
    DeviceState *qdev;
    uint64_t offset = 0, size = 0;
    uint8_t *grub;
    int rc;

    if (spapr->kernel_size) {
        return;
    }

    bootlist = get_boot_devices_list(&cb);

    if (!bootlist) {
        return;
    }

    for (cur = bootlist; cb > 0; cur = next + 1) {
        for (next = cur; cb > 0; --cb) {
            if (*next == '\n') {
                *next = '\0';
                ++next;
                --cb;
                break;
            }
        }

        qdev = of_client_find_qom_dev(sysbus_get_default(), cur);
        if (!qdev) {
            continue;
        }

        blkstr = object_property_get_str(OBJECT(qdev), "drive", NULL);
        if (!blkstr) {
            continue;
        }

        blk = blk_by_name(blkstr);
        if (!blk) {
            continue;
        }

        if (find_prep_partition(blk, &offset, &size)) {
            continue;
        }

        grub = g_malloc0(size);
        if (!grub) {
            continue;
        }

        rc = blk_pread(blk, offset, grub, size);
        if (rc <= 0) {
            g_free(grub);
            continue;
        }

        g_file_set_contents("my.grub", (void *) grub, size, NULL);
        spapr->kernel_size = load_elf("my.grub", NULL, NULL, NULL,
                                      NULL, &spapr->kernel_addr,
                                      NULL, NULL, 1,
                                      PPC_ELF_MACHINE, 0, 0);
        spapr->kernel_size = size;

        trace_spapr_of_client_blk_bootloader_read(offset, size);
        break;
    }

    g_free(bootlist);
}

void spapr_of_client_machine_init(SpaprMachineState *spapr)
{
    spapr_register_hypercall(KVMPPC_H_OF_CLIENT, spapr_h_of_client);
    spapr->machine_ready.notify = spapr_of_client_machine_ready;
    qemu_add_machine_init_done_notifier(&spapr->machine_ready);
}
