#include "qemu/osdep.h"
#include "block/qdict.h" /* for qdict_extract_subqdict() */
#include "qapi/error.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qom/object.h"

static QemuOptsList *vm_config_groups[48];
static QemuOptsList *drive_config_groups[5];

static QemuOptsList *find_list(QemuOptsList **lists, const char *group,
                               Error **errp)
{
    int i;

    qemu_load_module_for_opts(group);
    for (i = 0; lists[i] != NULL; i++) {
        if (strcmp(lists[i]->name, group) == 0)
            break;
    }
    if (lists[i] == NULL) {
        error_setg(errp, "There is no option group '%s'", group);
    }
    return lists[i];
}

QemuOptsList *qemu_find_opts(const char *group)
{
    QemuOptsList *ret;
    Error *local_err = NULL;

    ret = find_list(vm_config_groups, group, &local_err);
    if (local_err) {
        error_report_err(local_err);
    }

    return ret;
}

QemuOpts *qemu_find_opts_singleton(const char *group)
{
    QemuOptsList *list;
    QemuOpts *opts;

    list = qemu_find_opts(group);
    assert(list);
    opts = qemu_opts_find(list, NULL);
    if (!opts) {
        opts = qemu_opts_create(list, NULL, 0, &error_abort);
    }
    return opts;
}

static CommandLineParameterInfoList *query_option_descs(const QemuOptDesc *desc)
{
    CommandLineParameterInfoList *param_list = NULL;
    CommandLineParameterInfo *info;
    int i;

    for (i = 0; desc[i].name != NULL; i++) {
        info = g_malloc0(sizeof(*info));
        info->name = g_strdup(desc[i].name);

        switch (desc[i].type) {
        case QEMU_OPT_STRING:
            info->type = COMMAND_LINE_PARAMETER_TYPE_STRING;
            break;
        case QEMU_OPT_BOOL:
            info->type = COMMAND_LINE_PARAMETER_TYPE_BOOLEAN;
            break;
        case QEMU_OPT_NUMBER:
            info->type = COMMAND_LINE_PARAMETER_TYPE_NUMBER;
            break;
        case QEMU_OPT_SIZE:
            info->type = COMMAND_LINE_PARAMETER_TYPE_SIZE;
            break;
        }

        if (desc[i].help) {
            info->has_help = true;
            info->help = g_strdup(desc[i].help);
        }
        if (desc[i].def_value_str) {
            info->has_q_default = true;
            info->q_default = g_strdup(desc[i].def_value_str);
        }

        QAPI_LIST_PREPEND(param_list, info);
    }

    return param_list;
}

/* remove repeated entry from the info list */
static void cleanup_infolist(CommandLineParameterInfoList *head)
{
    CommandLineParameterInfoList *pre_entry, *cur, *del_entry;

    cur = head;
    while (cur->next) {
        pre_entry = head;
        while (pre_entry != cur->next) {
            if (!strcmp(pre_entry->value->name, cur->next->value->name)) {
                del_entry = cur->next;
                cur->next = cur->next->next;
                del_entry->next = NULL;
                qapi_free_CommandLineParameterInfoList(del_entry);
                break;
            }
            pre_entry = pre_entry->next;
        }
        cur = cur->next;
    }
}

/* merge the description items of two parameter infolists */
static void connect_infolist(CommandLineParameterInfoList *head,
                             CommandLineParameterInfoList *new)
{
    CommandLineParameterInfoList *cur;

    cur = head;
    while (cur->next) {
        cur = cur->next;
    }
    cur->next = new;
}

/* access all the local QemuOptsLists for drive option */
static CommandLineParameterInfoList *get_drive_infolist(void)
{
    CommandLineParameterInfoList *head = NULL, *cur;
    int i;

    for (i = 0; drive_config_groups[i] != NULL; i++) {
        if (!head) {
            head = query_option_descs(drive_config_groups[i]->desc);
        } else {
            cur = query_option_descs(drive_config_groups[i]->desc);
            connect_infolist(head, cur);
        }
    }
    cleanup_infolist(head);

    return head;
}

static CommandLineParameterInfoList *query_machine_properties(void)
{
    CommandLineParameterInfoList *param_list = NULL;
    CommandLineParameterInfo *info;
    ObjectPropertyIterator iter;
    ObjectProperty *prop;
    ObjectClass *oc;

    oc = object_get_class(container_get(object_get_root(), "/machine"));
    assert(oc);

    /* Add entry for the "type" parameter */
    info = g_malloc0(sizeof(*info));
    info->name = g_strdup("type");
    info->type = COMMAND_LINE_PARAMETER_TYPE_STRING;
    info->has_help = true;
    info->help = g_strdup("emulated machine");
    QAPI_LIST_PREPEND(param_list, info);

    /* Now loop over the properties */
    object_class_property_iter_init(&iter, oc);
    while ((prop = object_property_iter_next(&iter))) {
        if (!prop->set) {
            continue;
        }

        info = g_malloc0(sizeof(*info));
        info->name = g_strdup(prop->name);

        if (g_str_equal(prop->type, "bool")) {
            info->type = COMMAND_LINE_PARAMETER_TYPE_BOOLEAN;
        } else if (g_str_equal(prop->type, "int")) {
            info->type = COMMAND_LINE_PARAMETER_TYPE_NUMBER;
        } else if (g_str_equal(prop->type, "size")) {
            info->type = COMMAND_LINE_PARAMETER_TYPE_SIZE;
        } else {
            info->type = COMMAND_LINE_PARAMETER_TYPE_STRING;
        }

        if (prop->description) {
            info->has_help = true;
            info->help = g_strdup(prop->description);
        }

        QAPI_LIST_PREPEND(param_list, info);
    }

    return param_list;
}

CommandLineOptionInfoList *qmp_query_command_line_options(bool has_option,
                                                          const char *option,
                                                          Error **errp)
{
    CommandLineOptionInfoList *conf_list = NULL;
    CommandLineOptionInfo *info;
    int i;

    for (i = 0; vm_config_groups[i] != NULL; i++) {
        if (!has_option || !strcmp(option, vm_config_groups[i]->name)) {
            info = g_malloc0(sizeof(*info));
            info->option = g_strdup(vm_config_groups[i]->name);
            if (!strcmp("drive", vm_config_groups[i]->name)) {
                info->parameters = get_drive_infolist();
            } else {
                info->parameters =
                    query_option_descs(vm_config_groups[i]->desc);
            }
            QAPI_LIST_PREPEND(conf_list, info);
        }
    }

    if (!has_option || !strcmp(option, "machine")) {
        info = g_malloc0(sizeof(*info));
        info->option = g_strdup("machine");
        info->parameters = query_machine_properties();
        QAPI_LIST_PREPEND(conf_list, info);
    }

    if (conf_list == NULL) {
        error_setg(errp, "invalid option name: %s", option);
    }

    return conf_list;
}

QemuOptsList *qemu_find_opts_err(const char *group, Error **errp)
{
    return find_list(vm_config_groups, group, errp);
}

void qemu_add_drive_opts(QemuOptsList *list)
{
    int entries, i;

    entries = ARRAY_SIZE(drive_config_groups);
    entries--; /* keep list NULL terminated */
    for (i = 0; i < entries; i++) {
        if (drive_config_groups[i] == NULL) {
            drive_config_groups[i] = list;
            return;
        }
    }
    fprintf(stderr, "ran out of space in drive_config_groups");
    abort();
}

void qemu_add_opts(QemuOptsList *list)
{
    int entries, i;

    entries = ARRAY_SIZE(vm_config_groups);
    entries--; /* keep list NULL terminated */
    for (i = 0; i < entries; i++) {
        if (vm_config_groups[i] == NULL) {
            vm_config_groups[i] = list;
            return;
        }
    }
    fprintf(stderr, "ran out of space in vm_config_groups");
    abort();
}

/* Returns number of config groups on success, -errno on error */
static int qemu_config_foreach(FILE *fp, QEMUConfigCB *cb, void *opaque,
                               const char *fname, Error **errp)
{
    char line[1024], prev_group[64], group[64], arg[64], value[1024];
    Location loc;
    Error *local_err = NULL;
    QDict *qdict = NULL;
    int res = -EINVAL, lno = 0;
    int count = 0;

    loc_push_none(&loc);
    while (fgets(line, sizeof(line), fp) != NULL) {
        ++lno;
        if (line[0] == '\n') {
            /* skip empty lines */
            continue;
        }
        if (line[0] == '#') {
            /* comment */
            continue;
        }
        if (line[0] == '[') {
            QDict *prev = qdict;
            if (sscanf(line, "[%63s \"%63[^\"]\"]", group, value) == 2) {
                qdict = qdict_new();
                qdict_put_str(qdict, "id", value);
                count++;
            } else if (sscanf(line, "[%63[^]]]", group) == 1) {
                qdict = qdict_new();
                count++;
            }
            if (qdict != prev) {
                if (prev) {
                    cb(prev_group, prev, opaque, &local_err);
                    qobject_unref(prev);
                    if (local_err) {
                        error_propagate(errp, local_err);
                        goto out;
                    }
                }
                strcpy(prev_group, group);
                continue;
            }
        }
        loc_set_file(fname, lno);
        value[0] = '\0';
        if (sscanf(line, " %63s = \"%1023[^\"]\"", arg, value) == 2 ||
            sscanf(line, " %63s = \"\"", arg) == 1) {
            /* arg = value */
            if (qdict == NULL) {
                error_setg(errp, "no group defined");
                goto out;
            }
            qdict_put_str(qdict, arg, value);
            continue;
        }
        error_setg(errp, "parse error");
        goto out;
    }
    if (ferror(fp)) {
        loc_pop(&loc);
        error_setg_errno(errp, errno, "Cannot read config file");
        goto out_no_loc;
    }
    res = count;
    if (qdict) {
        cb(group, qdict, opaque, errp);
    }
out:
    loc_pop(&loc);
out_no_loc:
    qobject_unref(qdict);
    return res;
}

void qemu_config_do_parse(const char *group, QDict *qdict, void *opaque, Error **errp)
{
    QemuOptsList **lists = opaque;
    QemuOptsList *list;

    list = find_list(lists, group, errp);
    if (!list) {
        return;
    }

    qemu_opts_from_qdict(list, qdict, errp);
}

int qemu_config_parse(FILE *fp, QemuOptsList **lists, const char *fname, Error **errp)
{
    return qemu_config_foreach(fp, qemu_config_do_parse, lists, fname, errp);
}

int qemu_read_config_file(const char *filename, QEMUConfigCB *cb, Error **errp)
{
    FILE *f = fopen(filename, "r");
    int ret;

    if (f == NULL) {
        error_setg_file_open(errp, errno, filename);
        return -errno;
    }

    ret = qemu_config_foreach(f, cb, vm_config_groups, filename, errp);
    fclose(f);
    return ret;
}

static void config_parse_qdict_section(QDict *options, QemuOptsList *opts,
                                       Error **errp)
{
    QemuOpts *subopts;
    QDict *subqdict;
    QList *list = NULL;
    size_t orig_size, enum_size;
    char *prefix;

    prefix = g_strdup_printf("%s.", opts->name);
    qdict_extract_subqdict(options, &subqdict, prefix);
    g_free(prefix);
    orig_size = qdict_size(subqdict);
    if (!orig_size) {
        goto out;
    }

    subopts = qemu_opts_create(opts, NULL, 0, errp);
    if (!subopts) {
        goto out;
    }

    if (!qemu_opts_absorb_qdict(subopts, subqdict, errp)) {
        goto out;
    }

    enum_size = qdict_size(subqdict);
    if (enum_size < orig_size && enum_size) {
        error_setg(errp, "Unknown option '%s' for [%s]",
                   qdict_first(subqdict)->key, opts->name);
        goto out;
    }

    if (enum_size) {
        /* Multiple, enumerated sections */
        QListEntry *list_entry;
        unsigned i = 0;

        /* Not required anymore */
        qemu_opts_del(subopts);

        qdict_array_split(subqdict, &list);
        if (qdict_size(subqdict)) {
            error_setg(errp, "Unused option '%s' for [%s]",
                       qdict_first(subqdict)->key, opts->name);
            goto out;
        }

        QLIST_FOREACH_ENTRY(list, list_entry) {
            QDict *section = qobject_to(QDict, qlist_entry_obj(list_entry));
            char *opt_name;

            if (!section) {
                error_setg(errp, "[%s] section (index %u) does not consist of "
                           "keys", opts->name, i);
                goto out;
            }

            opt_name = g_strdup_printf("%s.%u", opts->name, i++);
            subopts = qemu_opts_create(opts, opt_name, 1, errp);
            g_free(opt_name);
            if (!subopts) {
                goto out;
            }

            if (!qemu_opts_absorb_qdict(subopts, section, errp)) {
                qemu_opts_del(subopts);
                goto out;
            }

            if (qdict_size(section)) {
                error_setg(errp, "[%s] section doesn't support the option '%s'",
                           opts->name, qdict_first(section)->key);
                qemu_opts_del(subopts);
                goto out;
            }
        }
    }

out:
    qobject_unref(subqdict);
    qobject_unref(list);
}

void qemu_config_parse_qdict(QDict *options, QemuOptsList **lists,
                             Error **errp)
{
    int i;
    Error *local_err = NULL;

    for (i = 0; lists[i]; i++) {
        config_parse_qdict_section(options, lists[i], &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }
}
