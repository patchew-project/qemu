/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include "qemu/datadir.h"
#include "qemu/units.h"
#include "exec/cpu-common.h"
#include "hw/qdev-properties.h"
#include "qapi/compat-policy.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qjson.h"
#include "qemu-version.h"
#include "qemu/cutils.h"
#include "qemu/help_option.h"
#include "qemu/uuid.h"
#include "sysemu/reset.h"
#include "sysemu/runstate.h"
#include "sysemu/runstate-action.h"
#include "sysemu/seccomp.h"
#include "sysemu/tcg.h"
#include "sysemu/xen.h"

#include "qemu/error-report.h"
#include "qemu/sockets.h"
#include "qemu/accel.h"
#include "hw/usb.h"
#include "hw/isa/isa.h"
#include "hw/scsi/scsi.h"
#include "hw/display/vga.h"
#include "sysemu/watchdog.h"
#include "hw/firmware/smbios.h"
#include "hw/acpi/acpi.h"
#include "hw/xen/xen.h"
#include "hw/loader.h"
#include "monitor/qdev.h"
#include "net/net.h"
#include "net/slirp.h"
#include "monitor/monitor.h"
#include "ui/console.h"
#include "ui/input.h"
#include "sysemu/sysemu.h"
#include "sysemu/numa.h"
#include "sysemu/hostmem.h"
#include "exec/gdbstub.h"
#include "qemu/timer.h"
#include "chardev/char.h"
#include "qemu/bitmap.h"
#include "qemu/log.h"
#include "sysemu/blockdev.h"
#include "hw/block/block.h"
#include "hw/i386/x86.h"
#include "hw/i386/pc.h"
#include "migration/misc.h"
#include "migration/snapshot.h"
#include "sysemu/tpm.h"
#include "sysemu/dma.h"
#include "hw/audio/soundhw.h"
#include "audio/audio.h"
#include "sysemu/cpus.h"
#include "sysemu/cpu-timers.h"
#include "migration/colo.h"
#include "migration/postcopy-ram.h"
#include "sysemu/kvm.h"
#include "sysemu/hax.h"
#include "qapi/qobject-input-visitor.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qemu/qemu-options.h"
#include "qemu/main-loop.h"
#ifdef CONFIG_VIRTFS
#include "fsdev/qemu-fsdev.h"
#endif
#include "sysemu/qtest.h"

#include "disas/disas.h"

#include "trace.h"
#include "trace/control.h"
#include "qemu/plugin.h"
#include "qemu/queue.h"
#include "sysemu/arch_init.h"
#include "exec/confidential-guest-support.h"

#include "ui/qemu-spice.h"
#include "qapi/string-input-visitor.h"
#include "qapi/opts-visitor.h"
#include "qapi/clone-visitor.h"
#include "qom/object_interfaces.h"
#include "semihosting/semihost.h"
#include "crypto/init.h"
#include "sysemu/replay.h"
#include "qapi/qapi-events-run-state.h"
#include "qapi/qapi-visit-block-core.h"
#include "qapi/qapi-visit-compat.h"
#include "qapi/qapi-visit-ui.h"
#include "qapi/qapi-commands-block-core.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qapi-visit-qom.h"
#include "qapi/qapi-commands-ui.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qerror.h"
#include "sysemu/iothread.h"
#include "qemu/guest-random.h"

#include "config-host.h"

#include "monitor/monitor-internal.h"
#include "qapi/qapi-commands-char.h"
#include "qapi/qapi-types-control.h"

static const char *incoming;
static const char *accelerators;
static ram_addr_t maxram_size;
static uint64_t ram_slots;
static int display_remote;
static ram_addr_t ram_size;
static DisplayOptions dpy;

static QemuOptsList qemu_rtc_opts = {
    .name = "rtc",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_rtc_opts.head),
    .merge_lists = true,
    .desc = {
        {
            .name = "base",
            .type = QEMU_OPT_STRING,
        },{
            .name = "clock",
            .type = QEMU_OPT_STRING,
        },{
            .name = "driftfix",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_option_rom_opts = {
    .name = "option-rom",
    .implied_opt_name = "romfile",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_option_rom_opts.head),
    .desc = {
        {
            .name = "bootindex",
            .type = QEMU_OPT_NUMBER,
        }, {
            .name = "romfile",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_accel_opts = {
    .name = "accel",
    .implied_opt_name = "accel",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_accel_opts.head),
    .desc = {
        /*
         * no elements => accept any
         * sanity checking will happen later
         * when setting accelerator properties
         */
        { }
    },
};

static QemuOptsList qemu_boot_opts = {
    .name = "boot-opts",
    .implied_opt_name = "order",
    .merge_lists = true,
    .head = QTAILQ_HEAD_INITIALIZER(qemu_boot_opts.head),
    .desc = {
        {
            .name = "order",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "once",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "menu",
            .type = QEMU_OPT_BOOL,
        }, {
            .name = "splash",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "splash-time",
            .type = QEMU_OPT_NUMBER,
        }, {
            .name = "reboot-timeout",
            .type = QEMU_OPT_NUMBER,
        }, {
            .name = "strict",
            .type = QEMU_OPT_BOOL,
        },
        { /*End of list */ }
    },
};

static QemuOptsList qemu_add_fd_opts = {
    .name = "add-fd",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_add_fd_opts.head),
    .desc = {
        {
            .name = "fd",
            .type = QEMU_OPT_NUMBER,
            .help = "file descriptor of which a duplicate is added to fd set",
        },{
            .name = "set",
            .type = QEMU_OPT_NUMBER,
            .help = "ID of the fd set to add fd to",
        },{
            .name = "opaque",
            .type = QEMU_OPT_STRING,
            .help = "free-form string used to describe fd",
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_object_opts = {
    .name = "object",
    .implied_opt_name = "qom-type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_object_opts.head),
    .desc = {
        { }
    },
};

static QemuOptsList qemu_tpmdev_opts = {
    .name = "tpmdev",
    .implied_opt_name = "type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_tpmdev_opts.head),
    .desc = {
        /* options are defined in the TPM backends */
        { /* end of list */ }
    },
};

static QemuOptsList qemu_overcommit_opts = {
    .name = "overcommit",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_overcommit_opts.head),
    .desc = {
        {
            .name = "mem-lock",
            .type = QEMU_OPT_BOOL,
        },
        {
            .name = "cpu-pm",
            .type = QEMU_OPT_BOOL,
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_msg_opts = {
    .name = "msg",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_msg_opts.head),
    .desc = {
        {
            .name = "timestamp",
            .type = QEMU_OPT_BOOL,
        },
        {
            .name = "guest-name",
            .type = QEMU_OPT_BOOL,
            .help = "Prepends guest name for error messages but only if "
                    "-name guest is set otherwise option is ignored\n",
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_name_opts = {
    .name = "name",
    .implied_opt_name = "guest",
    .merge_lists = true,
    .head = QTAILQ_HEAD_INITIALIZER(qemu_name_opts.head),
    .desc = {
        {
            .name = "guest",
            .type = QEMU_OPT_STRING,
            .help = "Sets the name of the guest.\n"
                    "This name will be displayed in the SDL window caption.\n"
                    "The name will also be used for the VNC server",
        }, {
            .name = "process",
            .type = QEMU_OPT_STRING,
            .help = "Sets the name of the QEMU process, as shown in top etc",
        }, {
            .name = "debug-threads",
            .type = QEMU_OPT_BOOL,
            .help = "When enabled, name the individual threads; defaults off.\n"
                    "NOTE: The thread names are for debugging and not a\n"
                    "stable API.",
        },
        { /* End of list */ }
    },
};

static QemuOptsList qemu_mem_opts = {
    .name = "memory",
    .implied_opt_name = "size",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_mem_opts.head),
    .merge_lists = true,
    .desc = {
        {
            .name = "size",
            .type = QEMU_OPT_SIZE,
        },
        {
            .name = "slots",
            .type = QEMU_OPT_NUMBER,
        },
        {
            .name = "maxmem",
            .type = QEMU_OPT_SIZE,
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_icount_opts = {
    .name = "icount",
    .implied_opt_name = "shift",
    .merge_lists = true,
    .head = QTAILQ_HEAD_INITIALIZER(qemu_icount_opts.head),
    .desc = {
        {
            .name = "shift",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "align",
            .type = QEMU_OPT_BOOL,
        }, {
            .name = "sleep",
            .type = QEMU_OPT_BOOL,
        }, {
            .name = "rr",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "rrfile",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "rrsnapshot",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_fw_cfg_opts = {
    .name = "fw_cfg",
    .implied_opt_name = "name",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_fw_cfg_opts.head),
    .desc = {
        {
            .name = "name",
            .type = QEMU_OPT_STRING,
            .help = "Sets the fw_cfg name of the blob to be inserted",
        }, {
            .name = "file",
            .type = QEMU_OPT_STRING,
            .help = "Sets the name of the file from which "
                    "the fw_cfg blob will be loaded",
        }, {
            .name = "string",
            .type = QEMU_OPT_STRING,
            .help = "Sets content of the blob to be inserted from a string",
        }, {
            .name = "gen_id",
            .type = QEMU_OPT_STRING,
            .help = "Sets id of the object generating the fw_cfg blob "
                    "to be inserted",
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_action_opts = {
    .name = "action",
    .merge_lists = true,
    .head = QTAILQ_HEAD_INITIALIZER(qemu_action_opts.head),
    .desc = {
        {
            .name = "shutdown",
            .type = QEMU_OPT_STRING,
        },{
            .name = "reboot",
            .type = QEMU_OPT_STRING,
        },{
            .name = "panic",
            .type = QEMU_OPT_STRING,
        },{
            .name = "watchdog",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

const char *qemu_get_vm_name(void)
{
    return qemu_name;
}

bool defaults_enabled(void)
{
    return false;
}

static QemuOptsList qemu_smp_opts = {
    .name = "smp-opts",
    .implied_opt_name = "cpus",
    .merge_lists = true,
    .head = QTAILQ_HEAD_INITIALIZER(qemu_smp_opts.head),
    .desc = {
        {
            .name = "cpus",
            .type = QEMU_OPT_NUMBER,
        }, {
            .name = "sockets",
            .type = QEMU_OPT_NUMBER,
        }, {
            .name = "dies",
            .type = QEMU_OPT_NUMBER,
        }, {
            .name = "cores",
            .type = QEMU_OPT_NUMBER,
        }, {
            .name = "threads",
            .type = QEMU_OPT_NUMBER,
        }, {
            .name = "maxcpus",
            .type = QEMU_OPT_NUMBER,
        },
        { /*End of list */ }
    },
};

static void realtime_init(void)
{
    if (enable_mlock) {
        if (os_mlock() < 0) {
            error_report("locking memory failed");
            exit(1);
        }
    }
}


/***********************************************************/
/* machine registration */

static MachineClass *find_machine(const char *name, GSList *machines)
{
    GSList *el;

    for (el = machines; el; el = el->next) {
        MachineClass *mc = el->data;

        if (!strcmp(mc->name, name) || !g_strcmp0(mc->alias, name)) {
            return mc;
        }
    }

    return NULL;
}

static MachineClass *find_default_machine(GSList *machines)
{
    GSList *el;
    MachineClass *default_machineclass = NULL;

    for (el = machines; el; el = el->next) {
        MachineClass *mc = el->data;

        if (mc->is_default) {
            assert(default_machineclass == NULL && "Multiple default machines");
            default_machineclass = mc;
        }
    }

    return default_machineclass;
}

typedef struct VGAInterfaceInfo {
    const char *opt_name;    /* option name */
    const char *name;        /* human-readable name */
    /* Class names indicating that support is available.
     * If no class is specified, the interface is always available */
    const char *class_names[2];
} VGAInterfaceInfo;

DisplayOptions *qmp_query_display_options(Error **errp)
{
    return QAPI_CLONE(DisplayOptions, &dpy);
}

static void qemu_create_default_devices(void)
{
    if (dpy.type == DISPLAY_TYPE_DEFAULT && !display_remote) {
        if (!qemu_display_find_default(&dpy)) {
            dpy.type = DISPLAY_TYPE_NONE;
#if defined(CONFIG_VNC)
            vnc_parse("localhost:0,to=99,id=default");
#endif
        }
    }
    if (dpy.type == DISPLAY_TYPE_DEFAULT) {
        dpy.type = DISPLAY_TYPE_NONE;
    }

    /* HACK: hardcoded VGA device */
    vga_interface_type = VGA_STD;
}

Chardev *serial_hd(int i)
{
    return NULL;
}

static const char *pid_file;
static Notifier qemu_unlink_pidfile_notifier;

static void qemu_unlink_pidfile(Notifier *n, void *data)
{
    if (pid_file) {
        unlink(pid_file);
    }
}

static MachineClass *select_machine(Error **errp)
{
    const char *optarg = NULL;
    GSList *machines = object_class_get_list(TYPE_MACHINE, false);
    MachineClass *machine_class;
    Error *local_err = NULL;

    if (optarg) {
        machine_class = find_machine(optarg, machines);
        if (!machine_class) {
            error_setg(&local_err, "unsupported machine type");
        }
    } else {
        machine_class = find_default_machine(machines);
        if (!machine_class) {
            error_setg(&local_err, "No machine specified, and there is no default");
        }
    }

    g_slist_free(machines);
    if (local_err) {
        error_append_hint(&local_err, "Use -machine help to list supported machines\n");
        error_propagate(errp, local_err);
    }
    return machine_class;
}

static void qemu_apply_machine_options(QDict *qdict)
{
    MachineClass *machine_class = MACHINE_GET_CLASS(current_machine);
    const char *boot_order = NULL;
    const char *boot_once = NULL;

    current_machine->ram_size = ram_size;
    current_machine->maxram_size = maxram_size;
    current_machine->ram_slots = ram_slots;

    if (!boot_order) {
        boot_order = machine_class->default_boot_order;
    }

    current_machine->boot_order = boot_order;
    current_machine->boot_once = boot_once;

    if (current_machine->smp.cpus > 1) {
        Error *blocker = NULL;
        error_setg(&blocker, QERR_REPLAY_NOT_SUPPORTED, "smp");
        replay_add_blocker(blocker);
    }
}

static void qemu_create_early_backends(void)
{
    qemu_display_early_init(&dpy);
    qemu_console_early_init();

    if (dpy.has_gl && dpy.gl != DISPLAYGL_MODE_OFF && display_opengl == 0) {
#if defined(CONFIG_OPENGL)
        error_report("OpenGL is not supported by the display");
#else
        error_report("OpenGL support is disabled");
#endif
        exit(1);
    }

    /* spice needs the timers to be initialized by this point */
    /* spice must initialize before audio as it changes the default auiodev */
    /* spice must initialize before chardevs (for spicevmc and spiceport) */
    qemu_spice.init();

    /* HACK: hardcoded monitor chardev */
    qmp_chardev_add("compat_monitor0", &(ChardevBackend){
            .type = CHARDEV_BACKEND_KIND_STDIO,
            .u.stdio = {
                .data = &(ChardevStdio){},
            },
        }, &error_abort);
}


static void qemu_create_late_backends(void)
{
    net_init_clients(&error_fatal);

    if (tpm_init() < 0) {
        exit(1);
    }

    /* HACK: hardcoded monitor */
    monitor_init(&(MonitorOptions){
            .chardev = (char *)"compat_monitor0",
        }, false, &error_abort);

    /* now chardevs have been created we may have semihosting to connect */
    qemu_semihosting_connect_chardevs();
    qemu_semihosting_console_init();
}

static bool have_custom_ram_size(void)
{
    return false;
}

static void qemu_resolve_machine_memdev(void)
{
    if (current_machine->ram_memdev_id) {
        Object *backend;
        ram_addr_t backend_size;

        backend = object_resolve_path_type(current_machine->ram_memdev_id,
                                           TYPE_MEMORY_BACKEND, NULL);
        if (!backend) {
            error_report("Memory backend '%s' not found",
                         current_machine->ram_memdev_id);
            exit(EXIT_FAILURE);
        }
        backend_size = object_property_get_uint(backend, "size",  &error_abort);
        if (have_custom_ram_size() && backend_size != ram_size) {
                error_report("Size specified by -m option must match size of "
                             "explicitly specified 'memory-backend' property");
                exit(EXIT_FAILURE);
        }
        ram_size = backend_size;
    }

    if (!xen_enabled()) {
        /* On 32-bit hosts, QEMU is limited by virtual address space */
        if (ram_size > (2047 << 20) && HOST_LONG_BITS == 32) {
            error_report("at most 2047 MB RAM can be simulated");
            exit(1);
        }
    }
}

static void set_memory_options(MachineClass *mc)
{
    uint64_t sz;
    const ram_addr_t default_ram_size = mc->default_ram_size;

    sz = 0;

    /* backward compatibility behaviour for case "-m 0" */
    if (sz == 0) {
        sz = default_ram_size;
    }

    sz = QEMU_ALIGN_UP(sz, 8192);
    if (mc->fixup_ram_size) {
        sz = mc->fixup_ram_size(sz);
    }
    ram_size = sz;
    if (ram_size != sz) {
        error_report("ram size too large");
        exit(EXIT_FAILURE);
    }

    maxram_size = ram_size;
}

static void qemu_create_machine(void)
{
    MachineClass *machine_class = select_machine(&error_fatal);
    object_set_machine_compat_props(machine_class->compat_props);

    set_memory_options(machine_class);

    current_machine = MACHINE(object_new_with_class(OBJECT_CLASS(machine_class)));
    object_property_add_child(object_get_root(), "machine",
                              OBJECT(current_machine));
    object_property_add_child(container_get(OBJECT(current_machine),
                                            "/unattached"),
                              "sysbus", OBJECT(sysbus_get_default()));

    if (machine_class->minimum_page_bits) {
        if (!set_preferred_target_page_bits(machine_class->minimum_page_bits)) {
            /* This would be a board error: specifying a minimum smaller than
             * a target's compile-time fixed setting.
             */
            g_assert_not_reached();
        }
    }

    cpu_exec_init_all();
    page_size_init();

    if (machine_class->hw_version) {
        qemu_set_hw_version(machine_class->hw_version);
    }

    /*
     * Get the default machine options from the machine if it is not already
     * specified either by the configuration file or by the command line.
     */
    if (machine_class->default_machine_opts) {
        QDict *default_opts =
            keyval_parse(machine_class->default_machine_opts, NULL, NULL,
                         &error_abort);
        object_set_properties_from_keyval(OBJECT(current_machine), default_opts,
                                          false, &error_abort);
        qobject_unref(default_opts);
    }
}

static int do_configure_accelerator(void *opaque, QemuOpts *opts, Error **errp)
{
    bool *p_init_failed = opaque;
    const char *acc = qemu_opt_get(opts, "accel");
    AccelClass *ac = accel_find(acc);
    AccelState *accel;
    int ret;
    bool qtest_with_kvm;

    qtest_with_kvm = false;

    if (!ac) {
        *p_init_failed = true;
        if (!qtest_with_kvm) {
            error_report("invalid accelerator %s", acc);
        }
        return 0;
    }
    accel = ACCEL(object_new_with_class(OBJECT_CLASS(ac)));
    object_apply_compat_props(OBJECT(accel));

    ret = accel_init_machine(accel, current_machine);
    if (ret < 0) {
        *p_init_failed = true;
        if (!qtest_with_kvm || ret != -ENOENT) {
            error_report("failed to initialize %s: %s", acc, strerror(-ret));
        }
        return 0;
    }

    return 1;
}

static void configure_accelerators(const char *progname)
{
    bool init_failed = false;

    if (QTAILQ_EMPTY(&qemu_accel_opts.head)) {
        char **accel_list, **tmp;

        if (accelerators == NULL) {
            /* Select the default accelerator */
            bool have_tcg = accel_find("tcg");
            bool have_kvm = accel_find("kvm");

            if (have_tcg && have_kvm) {
                if (g_str_has_suffix(progname, "kvm")) {
                    /* If the program name ends with "kvm", we prefer KVM */
                    accelerators = "kvm:tcg";
                } else {
                    accelerators = "tcg:kvm";
                }
            } else if (have_kvm) {
                accelerators = "kvm";
            } else if (have_tcg) {
                accelerators = "tcg";
            } else {
                error_report("No accelerator selected and"
                             " no default accelerator available");
                exit(1);
            }
        }
        accel_list = g_strsplit(accelerators, ":", 0);

        for (tmp = accel_list; *tmp; tmp++) {
            /*
             * Filter invalid accelerators here, to prevent obscenities
             * such as "-machine accel=tcg,,thread=single".
             */
            if (accel_find(*tmp)) {
                qemu_opts_parse_noisily(qemu_find_opts("accel"), *tmp, true);
            } else {
                init_failed = true;
                error_report("invalid accelerator %s", *tmp);
            }
        }
        g_strfreev(accel_list);
    } else {
        if (accelerators != NULL) {
            error_report("The -accel and \"-machine accel=\" options are incompatible");
            exit(1);
        }
    }

    if (!qemu_opts_foreach(qemu_find_opts("accel"),
                           do_configure_accelerator, &init_failed, &error_fatal)) {
        if (!init_failed) {
            error_report("no accelerator found");
        }
        exit(1);
    }

    if (init_failed) {
        AccelClass *ac = ACCEL_GET_CLASS(current_accel());
        error_report("falling back to %s", ac->name);
    }

    if (icount_enabled() && !tcg_enabled()) {
        error_report("-icount is not allowed with hardware virtualization");
        exit(1);
    }
}

static void create_default_memdev(MachineState *ms, const char *path)
{
    Object *obj;
    MachineClass *mc = MACHINE_GET_CLASS(ms);

    obj = object_new(path ? TYPE_MEMORY_BACKEND_FILE : TYPE_MEMORY_BACKEND_RAM);
    if (path) {
        object_property_set_str(obj, "mem-path", path, &error_fatal);
    }
    object_property_set_int(obj, "size", ms->ram_size, &error_fatal);
    object_property_add_child(object_get_objects_root(), mc->default_ram_id,
                              obj);
    /* Ensure backend's memory region name is equal to mc->default_ram_id */
    object_property_set_bool(obj, "x-use-canonical-path-for-ramblock-id",
                             false, &error_fatal);
    user_creatable_complete(USER_CREATABLE(obj), &error_fatal);
    object_unref(obj);
    object_property_set_str(OBJECT(ms), "memory-backend", mc->default_ram_id,
                            &error_fatal);
}

static void qemu_process_early_options(void)
{
    qemu_set_log(0);

    qemu_add_default_firmwarepath();
}

static void qemu_maybe_daemonize(const char *pid_file)
{
    Error *err = NULL;

    os_daemonize();
    rcu_disable_atfork();

    if (pid_file && !qemu_write_pidfile(pid_file, &err)) {
        error_reportf_err(err, "cannot create PID file: ");
        exit(1);
    }

    qemu_unlink_pidfile_notifier.notify = qemu_unlink_pidfile;
    qemu_add_exit_notifier(&qemu_unlink_pidfile_notifier);
}

static void qemu_init_displays(void)
{
    DisplayState *ds;

    /* init local displays */
    ds = init_displaystate();
    qemu_display_init(ds, &dpy);

    /* must be after terminal init, SDL library changes signal handlers */
    os_setup_signal_handling();

    if (using_spice) {
        qemu_spice.display_init();
    }
}

static void qemu_init_board(void)
{
    MachineClass *machine_class = MACHINE_GET_CLASS(current_machine);

    if (machine_class->default_ram_id && current_machine->ram_size &&
        numa_uses_legacy_mem() && !current_machine->ram_memdev_id) {
        create_default_memdev(current_machine, NULL);
    }

    /* From here on we enter MACHINE_PHASE_INITIALIZED.  */
    machine_run_board_init(current_machine);

    drive_check_orphaned();

    realtime_init();

    if (hax_enabled()) {
        /* FIXME: why isn't cpu_synchronize_all_post_init enough? */
        hax_sync_vcpus();
    }
}

static void qemu_machine_creation_done(void)
{
    MachineState *machine = MACHINE(qdev_get_machine());

    /* Did we create any drives that we failed to create a device for? */
    drive_check_orphaned();

    net_check_clients();

    qdev_prop_check_globals();

    qdev_machine_creation_done();

    if (machine->cgs) {
        /*
         * Verify that Confidential Guest Support has actually been initialized
         */
        assert(machine->cgs->ready);
    }
}

static void qemu_until_phase(MachineInitPhase phase);

void qemu_init(int argc, char **argv, char **envp)
{
    qemu_add_opts(&qemu_drive_opts);
    qemu_add_drive_opts(&qemu_legacy_drive_opts);
    qemu_add_drive_opts(&qemu_common_drive_opts);
    qemu_add_drive_opts(&qemu_drive_opts);
    qemu_add_drive_opts(&bdrv_runtime_opts);
    qemu_add_opts(&qemu_chardev_opts);
    qemu_add_opts(&qemu_device_opts);
    qemu_add_opts(&qemu_netdev_opts);
    qemu_add_opts(&qemu_nic_opts);
    qemu_add_opts(&qemu_net_opts);
    qemu_add_opts(&qemu_rtc_opts);
    qemu_add_opts(&qemu_global_opts);
    qemu_add_opts(&qemu_mon_opts);
    qemu_add_opts(&qemu_trace_opts);
    qemu_plugin_add_opts();
    qemu_add_opts(&qemu_option_rom_opts);
    qemu_add_opts(&qemu_accel_opts);
    qemu_add_opts(&qemu_mem_opts);
    qemu_add_opts(&qemu_smp_opts);
    qemu_add_opts(&qemu_boot_opts);
    qemu_add_opts(&qemu_add_fd_opts);
    qemu_add_opts(&qemu_object_opts);
    qemu_add_opts(&qemu_tpmdev_opts);
    qemu_add_opts(&qemu_overcommit_opts);
    qemu_add_opts(&qemu_msg_opts);
    qemu_add_opts(&qemu_name_opts);
    qemu_add_opts(&qemu_numa_opts);
    qemu_add_opts(&qemu_icount_opts);
    qemu_add_opts(&qemu_semihosting_config_opts);
    qemu_add_opts(&qemu_fw_cfg_opts);
    qemu_add_opts(&qemu_action_opts);
    module_call_init(MODULE_INIT_OPTS);

    error_init(argv[0]);
    qemu_init_exec_dir(argv[0]);

#ifdef CONFIG_MODULES
    module_init_info(qemu_modinfo);
    module_allow_arch(TARGET_NAME);
#endif

    qemu_init_subsystems();

    /*
     * HACK to demonstrate feeding CLI to QMP
     * Missing: translate CLI to QMP.  Instead, each CLI argument is
     * parsed as a QMP command.
     */
    {
        int i;
        QObject *req;
        QDict *resp, *error;

        for (i = 1; argv[i]; i++) {
            loc_set_cmdline(argv, i, 1);
            req = qobject_from_json(argv[i], &error_fatal);
            resp = qmp_dispatch(&qmp_commands, req, false, NULL);
            error = qdict_get_qdict(resp, "error");
            if (error) {
                error_report("%s", qdict_get_str(error, "desc"));
                exit(1);
            }
            /* TODO do something with the command's return valud? */
            qobject_unref(resp);
            qobject_unref(req);
        }
    }

    qemu_until_phase(PHASE_MACHINE_READY);
}

void qemu_until_phase(MachineInitPhase phase)
{
    MachineClass *machine_class;
    FILE *vmstate_dump_file = NULL;

    assert(phase >= phase_get());

    switch (phase_get()) {
    case PHASE_NO_MACHINE:
    if (phase == PHASE_NO_MACHINE) {
        break;
    }

    qemu_process_early_options();

    qemu_maybe_daemonize(pid_file);

    /*
     * The trace backend must be initialized after daemonizing.
     * trace_init_backends() will call st_init(), which will create the
     * trace thread in the parent, and also register st_flush_trace_buffer()
     * in atexit(). This function will force the parent to wait for the
     * writeout thread to finish, which will not occur, and the parent
     * process will be left in the host.
     */
    if (!trace_init_backends()) {
        exit(1);
    }
    trace_init_file();

    qemu_init_main_loop(&error_fatal);
    cpu_timers_init();

    configure_rtc(qemu_find_opts_singleton("rtc"));

    qemu_create_machine();

    suspend_mux_open();

    qemu_create_default_devices();
    qemu_create_early_backends();

    qemu_apply_machine_options(NULL);
    phase_advance(PHASE_MACHINE_CREATED);

    /* fall through */
    case PHASE_MACHINE_CREATED:
    if (phase == PHASE_MACHINE_CREATED) {
        break;
    }

    /*
     * Note: uses machine properties such as kernel-irqchip, must run
     * after qemu_apply_machine_options.
     */
    configure_accelerators("FIXME");
    phase_advance(PHASE_ACCEL_CREATED);

    /* fall through */
    case PHASE_ACCEL_CREATED:
    if (phase == PHASE_ACCEL_CREATED) {
        break;
    }

    /*
     * Beware, QOM objects created before this point miss global and
     * compat properties.
     *
     * Global properties get set up by qdev_prop_register_global(),
     * called from user_register_global_props(), and certain option
     * desugaring.  Also in CPU feature desugaring (buried in
     * parse_cpu_option()), which happens below this point, but may
     * only target the CPU type, which can only be created after
     * parse_cpu_option() returned the type.
     *
     * Machine compat properties: object_set_machine_compat_props().
     * Accelerator compat props: object_set_accelerator_compat_props(),
     * called from do_configure_accelerator().
     */

    machine_class = MACHINE_GET_CLASS(current_machine);
    if (!qtest_enabled() && machine_class->deprecation_reason) {
        error_report("Machine type '%s' is deprecated: %s",
                     machine_class->name, machine_class->deprecation_reason);
    }

    /*
     * Note: creates a QOM object, must run only after global and
     * compat properties have been set up.
     */
    migration_object_init();

    qemu_create_late_backends();

    /* parse features once if machine provides default cpu_type */
    current_machine->cpu_type = machine_class->default_cpu_type;
    /* NB: for machine none cpu_type could STILL be NULL here! */

    qemu_resolve_machine_memdev();

    if (vmstate_dump_file) {
        /* dump and exit */
        module_load_qom_all();
        dump_vmstate_json_to_file(vmstate_dump_file);
        exit(0);
    }

    qemu_init_board();
    assert(phase_get() == PHASE_MACHINE_INITIALIZED);

    /* fall through */
    case PHASE_MACHINE_INITIALIZED:
    if (phase == PHASE_MACHINE_INITIALIZED) {
        break;
    }

    qemu_machine_creation_done();
    assert(phase_get() == PHASE_MACHINE_READY);

    if (replay_mode != REPLAY_MODE_NONE) {
        replay_vmstate_init();
    }

    if (incoming) {
        Error *local_err = NULL;
        if (strcmp(incoming, "defer") != 0) {
            qmp_migrate_incoming(incoming, &local_err);
            if (local_err) {
                error_reportf_err(local_err, "-incoming %s: ", incoming);
                exit(1);
            }
        }
    } else if (autostart) {
        qmp_cont(NULL);
    }

    qemu_init_displays();
    accel_setup_post(current_machine);
    os_setup_post();
    resume_mux_open();

    case PHASE_MACHINE_READY:
        break;
    }
}
