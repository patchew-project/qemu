/*
 * Fault Injection Plugin
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <ctype.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <unistd.h>

#include "qemu/osdep.h"
#include <qemu-plugin.h>

#include "glib/gmarkup.h"

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef enum {
    TRIGGER_ON_PC = 0,
    TRIGGER_ON_SYSREG,
    TRIGGER_ON_RAM,
    TRIGGER_ON_MMIO,
    TRIGGER_ON_TIMER
} FaultTrigger;

typedef enum {
    TARGET_EMPTY = 0,
    TARGET_CPU_REG,
    TARGET_RAM,
    TARGET_MMIO,
    TARGET_IRQ,
    TARGET_EXCP,
    TARGET_CUSTOM
} FaultTarget;

typedef struct {
    FaultTarget target;
    uint64_t target_data;

    FaultTrigger trigger;
    uint64_t trigger_condition;
    gchar *trigger_condition_str;

    uint64_t fault_data;
    gchar *fault_name;

    uint8_t size;
    uint8_t cpu;
    gchar *irq_type;
} FaultConfig;

typedef struct {
    uint64_t hwaddr;
    uint64_t value;
    uint8_t  size;
} MmioOverrideConfig;

#define FI_LOG(...) do { \
    g_autofree gchar *__msg = g_strdup_printf(__VA_ARGS__); \
    qemu_plugin_outs(__msg); \
} while (0)

static bool plugin_is_shutting_down = false;
static int socket_fd = -1;

static GRWLock trigger_lock;

GHashTable *pc_faults;
GHashTable *mem_faults;
GHashTable *sys_reg_faults;

static GRWLock mmio_override_lock;
static GRWLock sysreg_lock;

GHashTable *mmio_override;

static struct qemu_plugin_register *gp_registers[31];

static void register_pc_trigger(FaultConfig* fc);
static void register_mmio_override(FaultConfig *fc);

static void fc_free(FaultConfig *fc);

static bool apply_mmio_override(uint64_t hwaddr, unsigned size, bool is_write,
                             uint64_t *value)
{
    g_rw_lock_reader_lock(&mmio_override_lock);

    MmioOverrideConfig *conf = g_hash_table_lookup(mmio_override, &hwaddr);
    if (!conf) {
        g_rw_lock_reader_unlock(&mmio_override_lock);
        return false;
    }

    *value = conf->value;

    g_rw_lock_reader_unlock(&mmio_override_lock);

    return true;
}

static bool mmio_override_cb(uint64_t hwaddr, unsigned size, bool is_write,
                             uint64_t *value)
{
    if (is_write) {
        return false;
    }

    return apply_mmio_override(hwaddr, size, is_write, value);
}

static void cpu_write_reg(int reg_id, uint64_t value)
{
    g_assert(reg_id >= 0 && reg_id <= 30);

    g_autoptr(GByteArray) buf = g_byte_array_new();

    g_byte_array_set_size(buf, 8);

    memcpy(buf->data, &value, 8);

    bool success = qemu_plugin_write_register(gp_registers[reg_id], buf);
    if (!success) {
        FI_LOG("FI: Failed to write register\n");
    }
}

static void cpu_write_mem(uint64_t addr, uint64_t data, uint8_t size)
{
    g_autoptr(GByteArray) buf = g_byte_array_new();

    g_byte_array_set_size(buf, size);

    memcpy(buf->data, &data, size);

    bool success = qemu_plugin_write_memory_vaddr(addr, buf);
    if (!success) {
        FI_LOG("FI: Failed to write memory\n");
    }
}

static void inject_irq(FaultConfig *fc)
{
    int irq_num = fc->target_data;

    if (!fc->irq_type || !g_strcmp0(fc->irq_type, "SPI")) {
        irq_num += 32;
    } else if (!g_strcmp0(fc->irq_type, "PPI")) {
        irq_num += 16;
    } else if (!g_strcmp0(fc->irq_type, "SGI")) {
        /* skip */
    } else {
        FI_LOG("FI: Unknown IRQ type: %s\n", fc->irq_type);
    }

    qemu_plugin_inject_irq(irq_num, fc->cpu, fc->fault_data);

}

static void inject_fault(FaultConfig* fc)
{
    switch (fc->target) {
        case TARGET_CPU_REG:
            cpu_write_reg(fc->target_data, fc->fault_data);
            break;
        case TARGET_RAM:
            cpu_write_mem(fc->target_data, fc->fault_data, fc->size);
            break;
        case TARGET_MMIO:
            register_mmio_override(fc);
            break;
        case TARGET_IRQ:
            inject_irq(fc);
            break;
        case TARGET_EXCP:
            qemu_plugin_inject_exception(fc->target_data, fc->fault_data);
            break;
        case TARGET_CUSTOM:
            qemu_plugin_trigger_custom_fault(fc->fault_name,
                                &fc->target_data, &fc->fault_data);
            break;
        default:
            FI_LOG("FI: Unsupported fault type\n");
            break;
    }
}

static void timed_fault_timer_cb(void* data)
{
    FaultConfig* fc = (FaultConfig*)data;

    inject_fault(fc);

    fc_free(fc);
}

static void vcpu_mem_cb(unsigned int vcpu_index,
                        qemu_plugin_meminfo_t info,
                        uint64_t vaddr, void *userdata)
{
    GSList *fault_list;

    g_rw_lock_reader_lock(&trigger_lock);

    fault_list = g_hash_table_lookup(mem_faults, &vaddr);
    for (GSList *entry = fault_list; entry != NULL; entry = entry->next) {
        FaultConfig *fc = (FaultConfig *)entry->data;

        inject_fault(fc);
    }

    g_rw_lock_reader_unlock(&trigger_lock);
}

static void vcpu_insn_exec_cb(unsigned int vcpu_index, void *data)
{
    uint64_t insn_vaddr = (uint64_t)data;
    GSList *fault_list;

    g_rw_lock_reader_lock(&trigger_lock);

    fault_list = g_hash_table_lookup(pc_faults,
                                        &insn_vaddr);

    for (GSList *l = fault_list; l != NULL; l = l->next) {
        FaultConfig *fc = (FaultConfig *)l->data;

        inject_fault(fc);
    }

    g_rw_lock_reader_unlock(&trigger_lock);
}

#define MRS_OPCODE 0xD5300000
#define MRS_OPCODE_MASK 0xFFF00000

static void handle_sysreg_fault(struct qemu_plugin_insn *insn, uint64_t insn_vaddr)
{
    FaultConfig *fc;
    uint32_t raw_opcode;
    size_t data_size = qemu_plugin_insn_data(insn, &raw_opcode, sizeof(raw_opcode));
    if (data_size < sizeof(raw_opcode)) {
        return;
    }

    uint32_t opcode = GUINT32_FROM_LE(raw_opcode);

    if ((opcode & MRS_OPCODE_MASK) != MRS_OPCODE) {
        return;
    }

    char *disas = qemu_plugin_insn_disas(insn);
    if (!disas) {
        return;
    }

    int dest_reg;
    char sysreg_name[32] = { 0 };

    if (sscanf(disas, "mrs x%d, %31s", &dest_reg, sysreg_name) == 2) {
        uint64_t fault_data;
        bool found = false;

        g_rw_lock_reader_lock(&sysreg_lock);

        fc = g_hash_table_lookup(sys_reg_faults, sysreg_name);
        if (fc) {
            fault_data = fc->fault_data;
            found = true;
        }

        g_rw_lock_reader_unlock(&sysreg_lock);

        if (found) {
            /*
             * WA: For CPU system registers, injecting fault to destination
             * gp register on next PC
             */
            FaultConfig *dyn_pc_fault = g_new0(FaultConfig, 1);

            dyn_pc_fault->trigger = TRIGGER_ON_PC;
            dyn_pc_fault->trigger_condition = insn_vaddr + 4;
            dyn_pc_fault->target = TARGET_CPU_REG;
            dyn_pc_fault->target_data = dest_reg;
            dyn_pc_fault->fault_data = fault_data;

            register_pc_trigger(dyn_pc_fault);
        }
    }

    g_free(disas);
}

static void vcpu_tb_trans_cb(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    for(int i = 0; i < qemu_plugin_tb_n_insns(tb); i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t insn_vaddr = qemu_plugin_insn_vaddr(insn);
        GSList *fault_list;

        qemu_plugin_register_vcpu_mem_cb(insn, vcpu_mem_cb,
                                         QEMU_PLUGIN_CB_NO_REGS, QEMU_PLUGIN_MEM_RW, NULL);

        handle_sysreg_fault(insn, insn_vaddr);

        g_rw_lock_reader_lock(&trigger_lock);

        fault_list = g_hash_table_lookup(pc_faults,
                                            &insn_vaddr);

        if (fault_list) {
            qemu_plugin_register_vcpu_insn_exec_cb(insn, vcpu_insn_exec_cb,
                                             QEMU_PLUGIN_CB_RW_REGS,
                                         (void *)insn_vaddr);
        }

        g_rw_lock_reader_unlock(&trigger_lock);
    }
}

static void vcpu_init_cb(qemu_plugin_id_t id, unsigned int vcpu_index)
{
    if (vcpu_index) {
        /* Init reg's and mem watchpoints only once, with CPU 0 */
        return;
    }

    g_autoptr(GArray) reg_list = qemu_plugin_get_registers();

    for (int i = 0; i < reg_list->len; ++i) {
        qemu_plugin_reg_descriptor *rd = &g_array_index(reg_list,
                                                qemu_plugin_reg_descriptor, i);

        if (rd->name[0] == 'x' && isdigit(rd->name[1])) {
            int reg_ind = atoi(&rd->name[1]);

            if (reg_ind >= 0 && reg_ind <= 30) {
                gp_registers[reg_ind] = rd->handle;
            }
        }
    }
}

static void register_mmio_override(FaultConfig *fc)
{
    g_rw_lock_writer_lock(&mmio_override_lock);

    MmioOverrideConfig *curr_conf = g_hash_table_lookup(mmio_override,
                                                        &fc->target_data);
    if (curr_conf) {
        curr_conf->value = fc->fault_data;
        curr_conf->size = fc->size;
    } else {
        MmioOverrideConfig *new_conf = g_new0(MmioOverrideConfig, 1);

        new_conf->hwaddr = fc->target_data;
        new_conf->value = fc->fault_data;
        new_conf->size = fc->size;

        g_hash_table_insert(mmio_override, &new_conf->hwaddr,
                     new_conf);
    }

    g_rw_lock_writer_unlock(&mmio_override_lock);
}

static void register_sysreg_override(FaultConfig *fc)
{
    g_rw_lock_writer_lock(&sysreg_lock);

    FaultConfig *old_fc = g_hash_table_lookup(sys_reg_faults,
                                              fc->trigger_condition_str);
    g_hash_table_replace(sys_reg_faults,
                    fc->trigger_condition_str,
                  fc);

    if (old_fc) {
        fc_free(old_fc);
    }

    g_rw_lock_writer_unlock(&sysreg_lock);
}

static void register_ram_trigger(FaultConfig* fc)
{

    g_rw_lock_writer_lock(&trigger_lock);

    GSList *mem_list = g_hash_table_lookup(mem_faults, &fc->trigger_condition);

    mem_list = g_slist_append(mem_list, fc);
    g_hash_table_insert(mem_faults,
                    &fc->trigger_condition, mem_list);

    g_rw_lock_writer_unlock(&trigger_lock);

}

static void register_pc_trigger(FaultConfig* fc)
{
    g_rw_lock_writer_lock(&trigger_lock);

    bool duplicate = false;
    GSList *pc_list = g_hash_table_lookup(pc_faults,
                                            &fc->trigger_condition);

    for (GSList *l = pc_list; l != NULL; l = l->next) {
        FaultConfig *existing = (FaultConfig *)l->data;

        if (existing->target == fc->target &&
            existing->target_data == fc->target_data &&
            existing->fault_data == fc->fault_data) {
            duplicate = true;
            break;
        }
    }

    if (!duplicate) {
        pc_list = g_slist_append(pc_list, fc);
        g_hash_table_insert(pc_faults, &fc->trigger_condition,
                    pc_list);
    } else {
        fc_free(fc);
    }

    g_rw_lock_writer_unlock(&trigger_lock);

}

static bool register_fault(FaultConfig *fc)
{
    FaultTrigger trigger_type = fc->trigger;

    if (fc->target == TARGET_CUSTOM && !fc->fault_name) {
        FI_LOG("FI: fault_name needed for custom targets\n");
        return false;
    }

    if (!fc->size) {
        fc->size = sizeof(fc->fault_data);
    }

    switch (fc->trigger) {
        case TRIGGER_ON_PC:
            register_pc_trigger(fc);
            break;
        case TRIGGER_ON_SYSREG:
            if (fc->target != TARGET_EMPTY) {
                FI_LOG("FI: SYS_REG faults does not support target\n");
                return false;
            }

            register_sysreg_override(fc);
            break;
        case TRIGGER_ON_RAM:
            if (fc->target == TARGET_EMPTY) {
                /* Allow short form for RAM triggers to override same memory */
                fc->target = TARGET_RAM;
                fc->target_data = fc->trigger_condition;
            }

            register_ram_trigger(fc);
            break;
        case TRIGGER_ON_MMIO:
            if (fc->target != TARGET_EMPTY) {
                FI_LOG("FI: No target support for MMIO trigger for now\n");
                return false;
            }

            register_mmio_override(fc);
            fc_free(fc);
            break;
        case TRIGGER_ON_TIMER:
            if (fc->target == TARGET_CPU_REG) {
                FI_LOG("FI: CPU_REG is invalid for TIMER trigger\n");
                return false;
            }
            qemu_plugin_timer_virt_ns(fc->trigger_condition,
                                   timed_fault_timer_cb, fc);
            break;
        default:
            /* skip */
            break;
    }

    if (trigger_type == TRIGGER_ON_PC || trigger_type == TRIGGER_ON_SYSREG) {
        qemu_plugin_flush_tb_cache();
    }

    return true;
}

static void fc_free(FaultConfig *fc)
{
    if (!fc) {
        return;
    }

    g_free(fc->trigger_condition_str);
    g_free(fc->fault_name);
    g_free(fc->irq_type);

    g_free(fc);
}

static void xml_start_elem(GMarkupParseContext *context,
                          const gchar         *element_name,
                          const gchar        **attribute_names,
                          const gchar        **attribute_values,
                          gpointer             user_data,
                          GError             **error)
{
    if (!g_strcmp0(element_name, "Fault")) {
        FaultConfig *fc = g_new0(FaultConfig, 1);

        for (int i = 0; attribute_names[i] != NULL; i++) {
            const char *key = attribute_names[i];
            const char *value = attribute_values[i];

            if (!g_strcmp0(key, "target")) {
                if (!g_strcmp0(value, "CPU_REG")) {
                    fc->target = TARGET_CPU_REG;
                } else if (!g_strcmp0(value, "RAM")) {
                    fc->target = TARGET_RAM;
                } else if (!g_strcmp0(value, "MMIO")) {
                    fc->target = TARGET_MMIO;
                } else if (!g_strcmp0(value, "IRQ")) {
                    fc->target = TARGET_IRQ;
                } else if (!g_strcmp0(value, "EXCP")) {
                    fc->target = TARGET_EXCP;
                } else if (!g_strcmp0(value, "CUSTOM")) {
                    fc->target = TARGET_CUSTOM;
                } else {
                    g_set_error(error, G_MARKUP_ERROR,
                          G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                        "FI: Unknown target type '%s'", value);
                    fc_free(fc);
                    return;
                }
            } else if (!g_strcmp0(key, "trigger")) {
                if (!g_strcmp0(value, "PC")) {
                    fc->trigger = TRIGGER_ON_PC;
                } else if (!g_strcmp0(value, "SYS_REG")) {
                    fc->trigger = TRIGGER_ON_SYSREG;
                } else if (!g_strcmp0(value, "RAM")) {
                    fc->trigger = TRIGGER_ON_RAM;
                } else if (!g_strcmp0(value, "MMIO")) {
                    fc->trigger = TRIGGER_ON_MMIO;
                } else if (!g_strcmp0(value, "TIMER")) {
                    fc->trigger = TRIGGER_ON_TIMER;
                } else {
                    g_set_error(error, G_MARKUP_ERROR,
                        G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                    "FI: Unknown trigger type: '%s'", value);
                    fc_free(fc);
                    return;
                }
            } else if (!g_strcmp0(key, "target_data")) {
                fc->target_data = strtoull(value, NULL, 0);
            } else if (!g_strcmp0(key, "trigger_condition")) {
                fc->trigger_condition_str = g_strdup(value);
                fc->trigger_condition = strtoull(value, NULL, 0);
            } else if (!g_strcmp0(key, "fault_data")) {
                fc->fault_data = strtoull(value, NULL, 0);
            } else if (!g_strcmp0(key, "size")) {
                fc->size = strtoull(value, NULL, 0);
            } else if (!g_strcmp0(key, "cpu")) {
                fc->cpu = strtoull(value, NULL, 0);
            } else if (!g_strcmp0(key, "irq_type")) {
                fc->irq_type = g_strdup(value);
            } else if (!g_strcmp0(key, "fault_name")) {
                fc->fault_name = g_strdup(value);
            }
        }

        if (!register_fault(fc)) {
            g_set_error(error, G_MARKUP_ERROR,
                    G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                "FI: Failed to register fault");
            fc_free(fc);
            return;
        }
    }
}

static GMarkupParser parser = {
    .start_element = xml_start_elem,
};

static void *ipc_listener_thread(void *arg)
{
    char *sock_path = (char *)arg;
    struct sockaddr_un addr;
    int client_fd;
    char buf[1024];

    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        FI_LOG("Failed to create socket, err = %s\n",
               strerror(errno));
        return NULL;
    }

    memset(&addr, 0, sizeof(addr));

    addr.sun_family = AF_UNIX;
    g_strlcpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    unlink(sock_path);

    if (bind(socket_fd, &addr, sizeof(addr)) < 0) {
        FI_LOG("Failed to create socket, err = %s\n",
               strerror(errno));
        close(socket_fd);
        return NULL;
    }

    if (listen(socket_fd, 1)) {
        FI_LOG("Listen socket failed, err = %s\n",
               strerror(errno));
        close(socket_fd);
        return NULL;
    }

    while (true) {
        client_fd = accept(socket_fd, NULL, NULL);

        if (client_fd < 0) {
            if (plugin_is_shutting_down) {
                break;
            }
            continue;
        }

        GString *xml_payload = g_string_new(NULL);

        memset(buf, 0, sizeof(buf));

        while (true) {
            ssize_t bytes_read = read(client_fd, buf, sizeof(buf) - 1);

            if (bytes_read > 0) {
                g_string_append_len(xml_payload, buf, bytes_read);
            } else if (bytes_read == 0) {
                break;
            } else {
                if (errno == EINTR) {
                    continue;
                }

                break;
            }
        }

        if (xml_payload->len > 0) {
            GError *err = NULL;

            GMarkupParseContext *ctx = g_markup_parse_context_new(&parser,
                        0, NULL, NULL);

            if (!g_markup_parse_context_parse(ctx, xml_payload->str,
                                    xml_payload->len, &err)) {
                FI_LOG("FI Error: Failed to parse dynamic XML: %s\n",
                    err->message);
                g_error_free(err);
            }

            g_markup_parse_context_free(ctx);
        }

        g_string_free(xml_payload, TRUE);
        close(client_fd);
    }

    unlink(sock_path);
    g_free(sock_path);

    return NULL;
}

static void plugin_exit_cb(qemu_plugin_id_t id, void *userdata)
{
    plugin_is_shutting_down = true;

    if (socket_fd >= 0) {
        close(socket_fd);
        socket_fd = -1;
    }
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    const char *config_path = NULL;
    const char *socket_path = NULL;
    gchar *config;
    gsize length;
    GError *err = NULL;
    bool success;

    if (strcmp(info->target_name, "aarch64")) {
        FI_LOG("FI: Target %s is not supported\n", info->target_name);
        return 1;
    }

    for (int i = 0; i < argc; ++i) {
        if (g_str_has_prefix(argv[i], "config=")) {
            config_path = argv[i] + strlen("config=");
        } else if (g_str_has_prefix(argv[i], "socket=")) {
            socket_path = g_strdup(argv[i] + strlen("socket="));
        }
    }

    if (!config_path && !socket_path) {
        FI_LOG("FI: either config or socket path required\n");
        return 1;
    }

    pc_faults = g_hash_table_new(g_int64_hash, g_int64_equal);
    mem_faults = g_hash_table_new(g_int64_hash, g_int64_equal);
    sys_reg_faults = g_hash_table_new(g_str_hash, g_str_equal);
    mmio_override = g_hash_table_new(g_int64_hash, g_int64_equal);

    g_rw_lock_init(&trigger_lock);
    g_rw_lock_init(&mmio_override_lock);
    g_rw_lock_init(&sysreg_lock);

    if (config_path) {
        if (access(config_path, R_OK)) {
            FI_LOG("FI: can't access config file, err = %s\n",
                   strerror(errno));
            return 1;
        }

        success = g_file_get_contents(config_path, &config,
                        &length, &err);
        if (success) {
            GMarkupParseContext *ctx = g_markup_parse_context_new(&parser,
                                                            0, NULL, NULL);

            success = g_markup_parse_context_parse(ctx, config, length, &err);
        }

        if (!success) {
            FI_LOG("FI: failed to parse config file\n");
            return 1;
        }
    }

    if (socket_path) {
        pthread_t thread_id;

        pthread_create(&thread_id, NULL, ipc_listener_thread,
                       (void*)socket_path);
        pthread_detach(thread_id);
    }

    qemu_plugin_register_vcpu_init_cb(id, vcpu_init_cb);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans_cb);
    qemu_plugin_register_mmio_override_cb(id, mmio_override_cb);

    qemu_plugin_register_atexit_cb(id, plugin_exit_cb, NULL);

    return 0;
}