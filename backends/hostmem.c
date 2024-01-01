/*
 * QEMU Host Memory Backend
 *
 * Copyright (C) 2013-2014 Red Hat Inc
 *
 * Authors:
 *   Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "sysemu/hostmem.h"
#include "hw/boards.h"
#include "qapi/error.h"
#include "qapi/qapi-builtin-visit.h"
#include "qapi/visitor.h"
#include "qemu/config-file.h"
#include "qom/object_interfaces.h"
#include "qemu/mmap-alloc.h"
#include "qemu/madvise.h"

#ifdef CONFIG_NUMA
#include <numaif.h>
#include <numa.h>
QEMU_BUILD_BUG_ON(HOST_MEM_POLICY_DEFAULT != MPOL_DEFAULT);
/*
 * HOST_MEM_POLICY_PREFERRED may either translate to MPOL_PREFERRED or
 * MPOL_PREFERRED_MANY, see comments further below.
 */
QEMU_BUILD_BUG_ON(HOST_MEM_POLICY_PREFERRED != MPOL_PREFERRED);
QEMU_BUILD_BUG_ON(HOST_MEM_POLICY_BIND != MPOL_BIND);
QEMU_BUILD_BUG_ON(HOST_MEM_POLICY_INTERLEAVE != MPOL_INTERLEAVE);
#endif

char *
host_memory_backend_get_name(HostMemoryBackend *backend)
{
    if (!backend->use_canonical_path) {
        return g_strdup(object_get_canonical_path_component(OBJECT(backend)));
    }

    return object_get_canonical_path(OBJECT(backend));
}

#define FILE_LINE_LEN 256
static int
is_valid_node(const char *path) {
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }

    char line[FILE_LINE_LEN];
    if (fgets(line, sizeof(line), file) != NULL) {
        int target_node = atoi(line);

        if (target_node >= 0) {
            fclose(file);
            return target_node;
        }
    }

    fclose(file);
    return -1;
}

static int
is_directory(const char *path) {
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISDIR(path_stat.st_mode);
}

static int
is_symlink(const char *path) {
    struct stat path_stat;
    if (lstat(path, &path_stat) == -1) {
        return 0;
    }
    return S_ISLNK(path_stat.st_mode);
}

#define CXL_DEVICE_PATH "/sys/bus/cxl/devices/"
#define REGION_PATH_LEN 307
#define DAX_REGION_PATH_LEN 563
#define DAX_PATH_LEN 819
#define TARGET_FILE_PATH_LEN 831
/*
 * return: the number of valid numa node id found
 */
static int
host_memory_backend_get_cxlram_nodes(int *valid_cxlram_nodes) {
    DIR *base_dir = NULL, *region_dir = NULL, *dax_region_dir = NULL;
    const char *base_dir_path = CXL_DEVICE_PATH;
    struct dirent *entry;
    int valid_node = 0, ret = 0;

    base_dir = opendir(base_dir_path);
    if (base_dir == NULL) {
        return valid_node;
    }

    while ((entry = readdir(base_dir)) != NULL) {
        char region_path[REGION_PATH_LEN];

        ret = snprintf(region_path, sizeof(region_path), "%s%s",
                                            base_dir_path, entry->d_name);
        if (ret < 0 ||
            !is_symlink(region_path) ||
            strncmp(entry->d_name, "region", ARRAY_SIZE("region") - 1)) {
            continue;
        }

        region_dir = opendir(region_path);
        if (region_dir == NULL) {
            goto region_exit;
        }

        while ((entry = readdir(region_dir)) != NULL) {
            char dax_region_path[DAX_REGION_PATH_LEN];

            ret = snprintf(dax_region_path, sizeof(dax_region_path), "%s/%s",
                                                    region_path, entry->d_name);
            if (ret < 0 ||
                !is_directory(dax_region_path) ||
                strncmp(entry->d_name, "dax_region",
                            ARRAY_SIZE("dax_region") - 1)) {

                continue;
            }

            dax_region_dir = opendir(dax_region_path);
            if (dax_region_dir == NULL) {
                goto dax_region_exit;
            }

            while ((entry = readdir(dax_region_dir)) != NULL) {
                int target_node;
                char dax_path[DAX_PATH_LEN];
                char target_file_path[TARGET_FILE_PATH_LEN];
                ret = snprintf(dax_path, sizeof(dax_path), "%s/%s",
                                            dax_region_path, entry->d_name);
                if (ret < 0 ||
                    !is_directory(dax_path) ||
                    strncmp(entry->d_name, "dax", ARRAY_SIZE("dax") - 1)) {
                    continue;
                }

                ret = snprintf(target_file_path, sizeof(target_file_path),
                                                    "%s/target_node", dax_path);
                if (ret < 0) {
                    continue;
                }

                target_node = is_valid_node(target_file_path);
                if (target_node >= 0) {
                    valid_cxlram_nodes[valid_node] = target_node;
                    valid_node++;
                }
            }
        }
    }

    closedir(dax_region_dir);
dax_region_exit:
    closedir(region_dir);
region_exit:
    closedir(base_dir);
    return valid_node;
}

static void
host_memory_backend_get_size(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    uint64_t value = backend->size;

    visit_type_size(v, name, &value, errp);
}

static void
host_memory_backend_set_size(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    uint64_t value;

    if (host_memory_backend_mr_inited(backend)) {
        error_setg(errp, "cannot change property %s of %s ", name,
                   object_get_typename(obj));
        return;
    }

    if (!visit_type_size(v, name, &value, errp)) {
        return;
    }
    if (!value) {
        error_setg(errp,
                   "property '%s' of %s doesn't take value '%" PRIu64 "'",
                   name, object_get_typename(obj), value);
        return;
    }
    backend->size = value;
}

static void
host_memory_backend_get_host_nodes(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    uint16List *host_nodes = NULL;
    uint16List **tail = &host_nodes;
    unsigned long value;

    value = find_first_bit(backend->host_nodes, MAX_NODES);
    if (value == MAX_NODES) {
        goto ret;
    }

    QAPI_LIST_APPEND(tail, value);

    do {
        value = find_next_bit(backend->host_nodes, MAX_NODES, value + 1);
        if (value == MAX_NODES) {
            break;
        }

        QAPI_LIST_APPEND(tail, value);
    } while (true);

ret:
    visit_type_uint16List(v, name, &host_nodes, errp);
    qapi_free_uint16List(host_nodes);
}

static void
host_memory_backend_set_host_nodes(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
#ifdef CONFIG_NUMA
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    uint16List *l, *host_nodes = NULL;

    if (backend->host_mem_type == HOST_MEM_TYPE_CXLRAM) {
        error_setg(errp,
            "'host-mem-type=' and 'host-nodes='/'policy=' are incompatible");
        return;
    }

    visit_type_uint16List(v, name, &host_nodes, errp);

    for (l = host_nodes; l; l = l->next) {
        if (l->value >= MAX_NODES) {
            error_setg(errp, "Invalid host-nodes value: %d", l->value);
            goto out;
        }
    }

    for (l = host_nodes; l; l = l->next) {
        bitmap_set(backend->host_nodes, l->value, 1);
    }

out:
    qapi_free_uint16List(host_nodes);
#else
    error_setg(errp, "NUMA node binding are not supported by this QEMU");
#endif
}

static int
host_memory_backend_get_policy(Object *obj, Error **errp G_GNUC_UNUSED)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    return backend->policy;
}

static void
host_memory_backend_set_policy(Object *obj, int policy, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    backend->policy = policy;

    if (backend->host_mem_type == HOST_MEM_TYPE_CXLRAM) {
        error_setg(errp,
            "'host-mem-type=' and 'host-nodes='/'policy=' are incompatible");
    }

#ifndef CONFIG_NUMA
    if (policy != HOST_MEM_POLICY_DEFAULT) {
        error_setg(errp, "NUMA policies are not supported by this QEMU");
    }
#endif
}

static int
host_memory_backend_get_host_mem_type(Object *obj, Error **errp G_GNUC_UNUSED)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    return backend->host_mem_type;
}

static void
host_memory_backend_set_host_mem_type(Object *obj, int host_mem_type, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    backend->host_mem_type = host_mem_type;

#ifndef CONFIG_NUMA
    error_setg(errp, "NUMA node host memory types are not supported by this QEMU");
#else
    int i, valid_cxlram_nodes[MAX_NODES];

    if (backend->policy > 0 ||
        !bitmap_empty(backend->host_nodes, MAX_NODES)) {
        error_setg(errp,
            "'host-mem-type=' and 'host-nodes='/'policy=' are incompatible");
        return;
    }

    if (host_memory_backend_get_cxlram_nodes(valid_cxlram_nodes) > 0) {
        for (i = 0; i < MAX_NODES; i++) {
            if (valid_cxlram_nodes[i] < 0) {
                break;
            }
            bitmap_set(backend->host_nodes, valid_cxlram_nodes[i], 1);
        }
    } else {
        error_setg(errp, "Cannot find CXL RAM on host");
        return;
    }
    backend->policy = HOST_MEM_POLICY_BIND;
#endif
}

static bool host_memory_backend_get_merge(Object *obj, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    return backend->merge;
}

static void host_memory_backend_set_merge(Object *obj, bool value, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    if (!host_memory_backend_mr_inited(backend)) {
        backend->merge = value;
        return;
    }

    if (value != backend->merge) {
        void *ptr = memory_region_get_ram_ptr(&backend->mr);
        uint64_t sz = memory_region_size(&backend->mr);

        qemu_madvise(ptr, sz,
                     value ? QEMU_MADV_MERGEABLE : QEMU_MADV_UNMERGEABLE);
        backend->merge = value;
    }
}

static bool host_memory_backend_get_dump(Object *obj, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    return backend->dump;
}

static void host_memory_backend_set_dump(Object *obj, bool value, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    if (!host_memory_backend_mr_inited(backend)) {
        backend->dump = value;
        return;
    }

    if (value != backend->dump) {
        void *ptr = memory_region_get_ram_ptr(&backend->mr);
        uint64_t sz = memory_region_size(&backend->mr);

        qemu_madvise(ptr, sz,
                     value ? QEMU_MADV_DODUMP : QEMU_MADV_DONTDUMP);
        backend->dump = value;
    }
}

static bool host_memory_backend_get_prealloc(Object *obj, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    return backend->prealloc;
}

static void host_memory_backend_set_prealloc(Object *obj, bool value,
                                             Error **errp)
{
    Error *local_err = NULL;
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    if (!backend->reserve && value) {
        error_setg(errp, "'prealloc=on' and 'reserve=off' are incompatible");
        return;
    }

    if (!host_memory_backend_mr_inited(backend)) {
        backend->prealloc = value;
        return;
    }

    if (value && !backend->prealloc) {
        int fd = memory_region_get_fd(&backend->mr);
        void *ptr = memory_region_get_ram_ptr(&backend->mr);
        uint64_t sz = memory_region_size(&backend->mr);

        qemu_prealloc_mem(fd, ptr, sz, backend->prealloc_threads,
                          backend->prealloc_context, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
        backend->prealloc = true;
    }
}

static void host_memory_backend_get_prealloc_threads(Object *obj, Visitor *v,
    const char *name, void *opaque, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    visit_type_uint32(v, name, &backend->prealloc_threads, errp);
}

static void host_memory_backend_set_prealloc_threads(Object *obj, Visitor *v,
    const char *name, void *opaque, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }
    if (value <= 0) {
        error_setg(errp, "property '%s' of %s doesn't take value '%d'", name,
                   object_get_typename(obj), value);
        return;
    }
    backend->prealloc_threads = value;
}

static void host_memory_backend_init(Object *obj)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    MachineState *machine = MACHINE(qdev_get_machine());

    /* TODO: convert access to globals to compat properties */
    backend->merge = machine_mem_merge(machine);
    backend->dump = machine_dump_guest_core(machine);
    backend->reserve = true;
    backend->prealloc_threads = machine->smp.cpus;
}

static void host_memory_backend_post_init(Object *obj)
{
    object_apply_compat_props(obj);
}

bool host_memory_backend_mr_inited(HostMemoryBackend *backend)
{
    /*
     * NOTE: We forbid zero-length memory backend, so here zero means
     * "we haven't inited the backend memory region yet".
     */
    return memory_region_size(&backend->mr) != 0;
}

MemoryRegion *host_memory_backend_get_memory(HostMemoryBackend *backend)
{
    return host_memory_backend_mr_inited(backend) ? &backend->mr : NULL;
}

void host_memory_backend_set_mapped(HostMemoryBackend *backend, bool mapped)
{
    backend->is_mapped = mapped;
}

bool host_memory_backend_is_mapped(HostMemoryBackend *backend)
{
    return backend->is_mapped;
}

size_t host_memory_backend_pagesize(HostMemoryBackend *memdev)
{
    size_t pagesize = qemu_ram_pagesize(memdev->mr.ram_block);
    g_assert(pagesize >= qemu_real_host_page_size());
    return pagesize;
}

static void
host_memory_backend_memory_complete(UserCreatable *uc, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(uc);
    HostMemoryBackendClass *bc = MEMORY_BACKEND_GET_CLASS(uc);
    Error *local_err = NULL;
    void *ptr;
    uint64_t sz;

    if (bc->alloc) {
        bc->alloc(backend, &local_err);
        if (local_err) {
            goto out;
        }

        ptr = memory_region_get_ram_ptr(&backend->mr);
        sz = memory_region_size(&backend->mr);

        if (backend->merge) {
            qemu_madvise(ptr, sz, QEMU_MADV_MERGEABLE);
        }
        if (!backend->dump) {
            qemu_madvise(ptr, sz, QEMU_MADV_DONTDUMP);
        }
#ifdef CONFIG_NUMA
        unsigned long lastbit = find_last_bit(backend->host_nodes, MAX_NODES);
        /* lastbit == MAX_NODES means maxnode = 0 */
        unsigned long maxnode = (lastbit + 1) % (MAX_NODES + 1);
        /* ensure policy won't be ignored in case memory is preallocated
         * before mbind(). note: MPOL_MF_STRICT is ignored on hugepages so
         * this doesn't catch hugepage case. */
        unsigned flags = MPOL_MF_STRICT | MPOL_MF_MOVE;
        int mode = backend->policy;

        /* check for invalid host-nodes and policies and give more verbose
         * error messages than mbind(). */
        if (maxnode && backend->policy == MPOL_DEFAULT) {
            error_setg(errp, "host-nodes must be empty for policy default,"
                       " or you should explicitly specify a policy other"
                       " than default");
            return;
        } else if (maxnode == 0 && backend->policy != MPOL_DEFAULT) {
            error_setg(errp, "host-nodes must be set for policy %s",
                       HostMemPolicy_str(backend->policy));
            return;
        }

        /* We can have up to MAX_NODES nodes, but we need to pass maxnode+1
         * as argument to mbind() due to an old Linux bug (feature?) which
         * cuts off the last specified node. This means backend->host_nodes
         * must have MAX_NODES+1 bits available.
         */
        assert(sizeof(backend->host_nodes) >=
               BITS_TO_LONGS(MAX_NODES + 1) * sizeof(unsigned long));
        assert(maxnode <= MAX_NODES);

#ifdef HAVE_NUMA_HAS_PREFERRED_MANY
        if (mode == MPOL_PREFERRED && numa_has_preferred_many() > 0) {
            /*
             * Replace with MPOL_PREFERRED_MANY otherwise the mbind() below
             * silently picks the first node.
             */
            mode = MPOL_PREFERRED_MANY;
        }
#endif

        if (maxnode &&
            mbind(ptr, sz, mode, backend->host_nodes, maxnode + 1, flags)) {
            if (backend->policy != MPOL_DEFAULT || errno != ENOSYS) {
                error_setg_errno(errp, errno,
                                 "cannot bind memory to host NUMA nodes");
                return;
            }
        }
#endif
        /* Preallocate memory after the NUMA policy has been instantiated.
         * This is necessary to guarantee memory is allocated with
         * specified NUMA policy in place.
         */
        if (backend->prealloc) {
            qemu_prealloc_mem(memory_region_get_fd(&backend->mr), ptr, sz,
                              backend->prealloc_threads,
                              backend->prealloc_context, &local_err);
            if (local_err) {
                goto out;
            }
        }
    }
out:
    error_propagate(errp, local_err);
}

static bool
host_memory_backend_can_be_deleted(UserCreatable *uc)
{
    if (host_memory_backend_is_mapped(MEMORY_BACKEND(uc))) {
        return false;
    } else {
        return true;
    }
}

static bool host_memory_backend_get_share(Object *o, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(o);

    return backend->share;
}

static void host_memory_backend_set_share(Object *o, bool value, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(o);

    if (host_memory_backend_mr_inited(backend)) {
        error_setg(errp, "cannot change property value");
        return;
    }
    backend->share = value;
}

#ifdef CONFIG_LINUX
static bool host_memory_backend_get_reserve(Object *o, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(o);

    return backend->reserve;
}

static void host_memory_backend_set_reserve(Object *o, bool value, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(o);

    if (host_memory_backend_mr_inited(backend)) {
        error_setg(errp, "cannot change property value");
        return;
    }
    if (backend->prealloc && !value) {
        error_setg(errp, "'prealloc=on' and 'reserve=off' are incompatible");
        return;
    }
    backend->reserve = value;
}
#endif /* CONFIG_LINUX */

static bool
host_memory_backend_get_use_canonical_path(Object *obj, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    return backend->use_canonical_path;
}

static void
host_memory_backend_set_use_canonical_path(Object *obj, bool value,
                                           Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    backend->use_canonical_path = value;
}

static void
host_memory_backend_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = host_memory_backend_memory_complete;
    ucc->can_be_deleted = host_memory_backend_can_be_deleted;

    object_class_property_add_bool(oc, "merge",
        host_memory_backend_get_merge,
        host_memory_backend_set_merge);
    object_class_property_set_description(oc, "merge",
        "Mark memory as mergeable");
    object_class_property_add_bool(oc, "dump",
        host_memory_backend_get_dump,
        host_memory_backend_set_dump);
    object_class_property_set_description(oc, "dump",
        "Set to 'off' to exclude from core dump");
    object_class_property_add_bool(oc, "prealloc",
        host_memory_backend_get_prealloc,
        host_memory_backend_set_prealloc);
    object_class_property_set_description(oc, "prealloc",
        "Preallocate memory");
    object_class_property_add(oc, "prealloc-threads", "int",
        host_memory_backend_get_prealloc_threads,
        host_memory_backend_set_prealloc_threads,
        NULL, NULL);
    object_class_property_set_description(oc, "prealloc-threads",
        "Number of CPU threads to use for prealloc");
    object_class_property_add_link(oc, "prealloc-context",
        TYPE_THREAD_CONTEXT, offsetof(HostMemoryBackend, prealloc_context),
        object_property_allow_set_link, OBJ_PROP_LINK_STRONG);
    object_class_property_set_description(oc, "prealloc-context",
        "Context to use for creating CPU threads for preallocation");
    object_class_property_add(oc, "size", "int",
        host_memory_backend_get_size,
        host_memory_backend_set_size,
        NULL, NULL);
    object_class_property_set_description(oc, "size",
        "Size of the memory region (ex: 500M)");
    object_class_property_add(oc, "host-nodes", "int",
        host_memory_backend_get_host_nodes,
        host_memory_backend_set_host_nodes,
        NULL, NULL);
    object_class_property_set_description(oc, "host-nodes",
        "Binds memory to the list of NUMA host nodes");
    object_class_property_add_enum(oc, "policy", "HostMemPolicy",
        &HostMemPolicy_lookup,
        host_memory_backend_get_policy,
        host_memory_backend_set_policy);
    object_class_property_set_description(oc, "policy",
        "Set the NUMA policy");
    object_class_property_add_bool(oc, "share",
        host_memory_backend_get_share, host_memory_backend_set_share);
    object_class_property_set_description(oc, "share",
        "Mark the memory as private to QEMU or shared");
    object_class_property_add_enum(oc, "host-mem-type", "HostMemType",
        &HostMemType_lookup,
        host_memory_backend_get_host_mem_type,
        host_memory_backend_set_host_mem_type);
    object_class_property_set_description(oc, "host-mem-type",
        "Set the backend host memory type");
#ifdef CONFIG_LINUX
    object_class_property_add_bool(oc, "reserve",
        host_memory_backend_get_reserve, host_memory_backend_set_reserve);
    object_class_property_set_description(oc, "reserve",
        "Reserve swap space (or huge pages) if applicable");
#endif /* CONFIG_LINUX */
    /*
     * Do not delete/rename option. This option must be considered stable
     * (as if it didn't have the 'x-' prefix including deprecation period) as
     * long as 4.0 and older machine types exists.
     * Option will be used by upper layers to override (disable) canonical path
     * for ramblock-id set by compat properties on old machine types ( <= 4.0),
     * to keep migration working when backend is used for main RAM with
     * -machine memory-backend= option (main RAM historically used prefix-less
     * ramblock-id).
     */
    object_class_property_add_bool(oc, "x-use-canonical-path-for-ramblock-id",
        host_memory_backend_get_use_canonical_path,
        host_memory_backend_set_use_canonical_path);
}

static const TypeInfo host_memory_backend_info = {
    .name = TYPE_MEMORY_BACKEND,
    .parent = TYPE_OBJECT,
    .abstract = true,
    .class_size = sizeof(HostMemoryBackendClass),
    .class_init = host_memory_backend_class_init,
    .instance_size = sizeof(HostMemoryBackend),
    .instance_init = host_memory_backend_init,
    .instance_post_init = host_memory_backend_post_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void register_types(void)
{
    type_register_static(&host_memory_backend_info);
}

type_init(register_types);
