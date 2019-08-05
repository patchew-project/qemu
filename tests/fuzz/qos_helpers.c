#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qos_helpers.h"
#include "fuzz.h"
#include "qapi/qmp/qlist.h"
#include "libqtest.h"
#include "sysemu/qtest.h"
#include "libqos/qgraph.h"
#include "libqos/qgraph_internal.h"
#include "libqos/qos_external.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qapi-commands-qom.h"
#include <wordexp.h>
#include "sysemu/sysemu.h"
#include "sysemu/cpus.h"



/*
 * Replaced the qmp commands with direct qmp_marshal calls.
 * Probably there is a better way to do this
 */
void qos_set_machines_devices_available(void)
{
    QDict *req = qdict_new();
    QObject *response;
    QDict *args = qdict_new();
    QList *lst;
    Error *err = NULL;

    qmp_marshal_query_machines(NULL, &response, &err);
    assert(!err);
    lst = qobject_to(QList, response);
    apply_to_qlist(lst, true);

    qobject_unref(response);


    qdict_put_str(req, "execute", "qom-list-types");
    qdict_put_str(args, "implements", "device");
    qdict_put_bool(args, "abstract", true);
    qdict_put_obj(req, "arguments", (QObject *) args);

    qmp_marshal_qom_list_types(args, &response, &err);
    assert(!err);
    lst = qobject_to(QList, response);
    apply_to_qlist(lst, false);
    qobject_unref(response);
    qobject_unref(req);
}

static char **current_path;


void *qos_allocate_objects(QTestState *qts, QGuestAllocator **p_alloc)
{
    return allocate_objects(qts, current_path + 1, p_alloc);
}


char **fuzz_path_vec;
void *qos_obj;
QGuestAllocator *qos_alloc;

int qos_argc;
char **qos_argv;

void qos_build_main_args()
{
    char **path = fuzz_path_vec;
    QOSGraphNode *test_node;
    GString *cmd_line = g_string_new(path[0]);
    void *test_arg;

    /* Before test */
    current_path = path;
    test_node = qos_graph_get_node(path[(g_strv_length(path) - 1)]);
    test_arg = test_node->u.test.arg;
    if (test_node->u.test.before) {
        test_arg = test_node->u.test.before(cmd_line, test_arg);
    }

    /* Prepend the arguments that we need */
    g_string_prepend(cmd_line,
            "qemu-system-i386 -display none -machine accel=fuzz -m 16 ");
    wordexp_t result;
    wordexp(cmd_line->str, &result, 0);
    qos_argc = result.we_wordc;
    qos_argv = result.we_wordv;

    g_string_free(cmd_line, true);
}

/*
 * This function is largely a copy of qos-test.c:
 * TODO: Possibly add a callback argument to walk_path to use one function
 * for both fuzzing and normal testing
 */
void walk_path(QOSGraphNode *orig_path, int len)
{
    QOSGraphNode *path;
    QOSGraphEdge *edge;

    /* etype set to QEDGE_CONSUMED_BY so that machine can add to the command line */
    QOSEdgeType etype = QEDGE_CONSUMED_BY;

    /* twice QOS_PATH_MAX_ELEMENT_SIZE since each edge can have its arg */
    char **path_vec = g_new0(char *, (QOS_PATH_MAX_ELEMENT_SIZE * 2));
    int path_vec_size = 0;

    char *after_cmd, *before_cmd, *after_device;
    GString *after_device_str = g_string_new("");
    char *node_name = orig_path->name, *path_str;

    GString *cmd_line = g_string_new("");
    GString *cmd_line2 = g_string_new("");

    path = qos_graph_get_node(node_name); /* root */
    node_name = qos_graph_edge_get_dest(path->path_edge); /* machine name */

    path_vec[path_vec_size++] = node_name;
    path_vec[path_vec_size++] = qos_get_machine_type(node_name);

    for (;;) {
        path = qos_graph_get_node(node_name);
        if (!path->path_edge) {
            break;
        }

        node_name = qos_graph_edge_get_dest(path->path_edge);

        /* append node command line + previous edge command line */
        if (path->command_line && etype == QEDGE_CONSUMED_BY) {
            g_string_append(cmd_line, path->command_line);
            g_string_append(cmd_line, after_device_str->str);
            g_string_truncate(after_device_str, 0);
        }

        path_vec[path_vec_size++] = qos_graph_edge_get_name(path->path_edge);
        /* detect if edge has command line args */
        after_cmd = qos_graph_edge_get_after_cmd_line(path->path_edge);
        after_device = qos_graph_edge_get_extra_device_opts(path->path_edge);
        before_cmd = qos_graph_edge_get_before_cmd_line(path->path_edge);
        edge = qos_graph_get_edge(path->name, node_name);
        etype = qos_graph_edge_get_type(edge);

        if (before_cmd) {
            g_string_append(cmd_line, before_cmd);
        }
        if (after_cmd) {
            g_string_append(cmd_line2, after_cmd);
        }
        if (after_device) {
            g_string_append(after_device_str, after_device);
        }
    }

    path_vec[path_vec_size++] = NULL;
    g_string_append(cmd_line, after_device_str->str);
    g_string_free(after_device_str, true);

    g_string_append(cmd_line, cmd_line2->str);
    g_string_free(cmd_line2, true);

    /*
     * here position 0 has <arch>/<machine>, position 1 has <machine>.
     * The path must not have the <arch>, qtest_add_data_func adds it.
     */
    path_str = g_strjoinv("/", path_vec + 1);

    /* Check that this is the test we care about: */
    char *test_name = strrchr(path_str, '/') + 1;
    if (strcmp(test_name, fuzz_target->name->str) == 0) {
        /*
         * put arch/machine in position 1 so run_one_test can do its work
         * and add the command line at position 0.
         */
        path_vec[1] = path_vec[0];
        path_vec[0] = g_string_free(cmd_line, false);
        printf("path_str: %s path_vec[0]: %s [1]: %s\n", path_str, path_vec[0],
                path_vec[1]);

        fuzz_path_vec = path_vec;
    } else {
        g_free(path_vec);
    }

    g_free(path_str);
}
