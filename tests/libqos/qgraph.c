/*
 * libqos driver framework
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/queue.h"
#include "qgraph.h"
#include "qgraph_extra.h"

#define QOS_ROOT ""
typedef struct QOSStackElement QOSStackElement;

/* Graph Edge.*/
struct QOSGraphEdge {
    QOSEdgeType type;
    char *dest;
    char *arg; /* just for CONTAIS and CONSUMED_BY */
    QSLIST_ENTRY(QOSGraphEdge) edge_list;
};

/* Linked list grouping all edges with the same source node */
QSLIST_HEAD(QOSGraphEdgeList, QOSGraphEdge);


/**
 * Stack used to keep track of the discovered path when using
 * the DFS algorithm
 */
struct QOSStackElement {
    QOSGraphNode *node;
    QOSStackElement *parent;
    QOSGraphEdge *parent_edge;
    int length;
};

/* Each enty in these hash table will consist of <string, node/edge> pair. */
static GHashTable *edge_table;
static GHashTable *node_table;

/* stack used by the DFS algorithm to store the path from machine to test */
static QOSStackElement qos_node_stack[QOS_PATH_MAX_ELEMENT_SIZE];
static int qos_node_tos;

/**
 * add_edge_arg(): creates an edge of type @type
 *  from @source to @dest node, and inserts it in the
 * edges hash table
 *
 * Nodes @source and @dest do not necessarily need to exist.
 * Adds also an optional command line arg.
 */
static void add_edge_arg(const char *source, const char *dest,
                            QOSEdgeType type, const char *arg)
{
    char *key;
    QOSGraphEdgeList *list = g_hash_table_lookup(edge_table, source);
    if (!list) {
        list = g_new0(QOSGraphEdgeList, 1);
        key = g_strdup(source);
        g_hash_table_insert(edge_table, key, list);
    }

    QOSGraphEdge *edge = g_new0(QOSGraphEdge, 1);
    edge->type = type;
    edge->dest = g_strdup(dest);
    if (arg) {
        edge->arg = g_strconcat(",", arg, NULL);
    }
    QSLIST_INSERT_HEAD(list, edge, edge_list);
}

/* add_edge(): same as add_edge_arg, but the arg is null */
static void add_edge(const char *source, const char *dest, QOSEdgeType type)
{
    add_edge_arg(source, dest, type, NULL);
}

/* remove_edges(): removes all edges inside a given @list */
static void remove_edges(void *list)
{
    QOSGraphEdge *temp;
    QOSGraphEdgeList *elist = list;

    while (!QSLIST_EMPTY(elist)) {
        temp = QSLIST_FIRST(elist);
        QSLIST_REMOVE_HEAD(elist, edge_list);
        free(temp);
    }
    g_free(elist);
}

/**
 * create_node(): creates a node @name of type @type
 * and inserts it to the nodes hash table.
 * By default, node is not available.
 */
static QOSGraphNode *create_node(const char *name, QOSNodeType type)
{
    if (g_hash_table_lookup(node_table, name)) {
        g_printerr("Node %s already created\n", name);
        abort();
    }

    QOSGraphNode *node = g_new0(QOSGraphNode, 1);
    node->type = type;
    node->available = FALSE;
    node->name = g_strdup(name);
    g_hash_table_insert(node_table, node->name, node);
    return node;
}

/**
 * remove_node(): removes a node @val from the nodes hash table.
 * Note that node->name is not free'd since it will represent the
 * hash table key
 */
static void remove_node(void *val)
{
    QOSGraphNode *node = (QOSGraphNode *) val;
    g_free(node->command_line);
    g_free(node);
}

/**
 * remove_string(): removes @key from the nodes hash table.
 * Actually frees the node->name
 */
static void remove_string(void *key)
{
    g_free(key);
}

/**
 * search_node(): search for a node @key in the nodes hash table
 * Returns the QOSGraphNode if found, fails otherwise
 */
static QOSGraphNode *search_node(const char *key)
{
    return g_hash_table_lookup(node_table, key);
}

/**
 * get_edgelist(): returns the edge list (value) assigned to
 * the @key in the edge hash table.
 * This list will contain all edges with source equal to @key
 *
 * Returns: on success: the %QOSGraphEdgeList
 *          otherwise: abort()
 */
static QOSGraphEdgeList *get_edgelist(const char *key)
{
    return g_hash_table_lookup(edge_table, key);
}

/**
 * search_list_edges(): search for an edge with destination @dest
 * in the given @edgelist.
 *
 * Returns: on success: the %QOSGraphEdge
 *          otherwise: #NULL
 */
static QOSGraphEdge *search_list_edges(QOSGraphEdgeList *edgelist,
                                        const char *dest)
{
    QOSGraphEdge *tmp, *next;
    if (!edgelist) {
        return NULL;
    }
    QSLIST_FOREACH_SAFE(tmp, edgelist, edge_list, next) {
        if (g_strcmp0(tmp->dest, dest) == 0) {
            break;
        }
    }
    return tmp;
}

/**
 * search_machine(): search for a machine @name in the node hash
 * table. A machine is the child of the root node.
 * This function forces the research in the childs of the root,
 * to check the node is a proper machine
 *
 * Returns: on success: the %QOSGraphNode
 *          otherwise: #NULL
 */
static QOSGraphNode *search_machine(const char *name)
{
    QOSGraphNode *n;
    QOSGraphEdgeList *root_list = get_edgelist(QOS_ROOT);
    QOSGraphEdge *e = search_list_edges(root_list, name);
    if (!e) {
        return NULL;
    }
    n = search_node(e->dest);
    if (n->type == MACHINE) {
        return n;
    }
    return NULL;
}

/**
 * build_machine_cmd_line(): builds the command line for the machine
 * @node. The node name must be a valid qemu identifier, since it
 * will be used to build the command line.
 *
 * It is also possible to pass an optional @args that will be
 * concatenated to the command line.
 *
 * For machines, prepend -M to the machine name.
 */
static void build_machine_cmd_line(QOSGraphNode *node, const char *args)
{
    char *arch, *machine;
    qos_separate_arch_machine(node->name, &arch, &machine);
    if (args) {
        node->command_line = g_strconcat("-M ", machine, ",", args, NULL);
    } else {
        node->command_line = g_strconcat("-M ", machine, NULL);
    }
}

/**
 * build_driver_cmd_line(): builds the command line for the driver
 * @node. The node name must be a valid qemu identifier, since it
 * will be used to build the command line.
 *
 * It is also possible to pass an optional @args that will be
 * concatenated to the command line.
 *
 * For drivers, prepend -device to the driver name.
 */
static void build_driver_cmd_line(QOSGraphNode *node, const char *args)
{
    if (args) {
        node->command_line = g_strconcat("-device ", node->name, ",",
                                          args, NULL);
    } else {
        node->command_line = g_strconcat("-device ", node->name, NULL);
    }
}

/**
 * build_test_cmd_line(): builds the command line for the test
 * @node. The node name need not to be a valid qemu identifier, since it
 * will not be used to build the command line.
 *
 * It is also possible to pass an optional @args that will be
 * used as additional command line.
 */
static void build_test_cmd_line(QOSGraphNode *node, const char *args)
{
    if (args) {
        node->command_line = g_strdup(args);
    } else {
        node->command_line = NULL;
    }
}

/* qos_print_cb(): callback prints all path found by the DFS algorithm. */
static void qos_print_cb(QOSGraphNode *path, int length)
{
    #if PRINT_DEBUG
        printf("%d elements\n", length);

        if (!path) {
            return;
        }

        while (path->path_edge) {
            printf("%s ", path->name);
            switch (path->path_edge->type) {
            case PRODUCES:
                printf("--PRODUCES--> ");
                break;
            case CONSUMED_BY:
                printf("--CONSUMED_BY--> ");
                break;
            case CONTAINS:
                printf("--CONTAINS--> ");
                break;
            }
            path = search_node(path->path_edge->dest);
        }

        printf("%s\n\n", path->name);
    #endif
}

/* qos_push(): push a node @el and edge @e in the qos_node_stack */
static void qos_push(QOSGraphNode *el, QOSStackElement *parent,
                        QOSGraphEdge *e)
{
    int len = 0; /* root is not counted */
    if (qos_node_tos == QOS_PATH_MAX_ELEMENT_SIZE) {
        g_printerr("QOSStack: full stack, cannot push");
        abort();
    }

    if (parent) {
        len = parent->length + 1;
    }
    qos_node_stack[qos_node_tos++] = (QOSStackElement) {el, parent, e, len};
}

/* qos_tos(): returns the top of stack, without popping */
static QOSStackElement *qos_tos(void)
{
    return &qos_node_stack[(qos_node_tos - 1)];
}

/* qos_pop(): pops an element from the tos, setting it unvisited*/
static QOSStackElement *qos_pop(void)
{
    if (qos_node_tos == 0) {
        g_printerr("QOSStack: empty stack, cannot pop");
        abort();
    }
    QOSStackElement *e = qos_tos();
    e->node->visited = FALSE;
    qos_node_tos--;
    return e;
}

/**
 * qos_reverse_path(): reverses the found path, going from
 * test-to-machine to machine-to-test
 */
static QOSGraphNode *qos_reverse_path(QOSStackElement *el)
{
    if (!el) {
        return NULL;
    }

    el->node->path_edge = NULL;

    while (el->parent->length > 0) {
        el->parent->node->path_edge = el->parent_edge;
        el = el->parent;
    }

    return el->node;
}

/**
 * qos_traverse_graph(): graph-walking algorithm, using Depth First Search it
 * starts from the root @machine and walks all possible path until it
 * reaches a test node.
 * At that point, it reverses the path found and invokes the @callback.
 *
 * Being Depth First Search, time complexity is O(|V| + |E|), while
 * space is O(|V|). In this case, the maximum stack size is set by
 * QOS_PATH_MAX_ELEMENT_SIZE.
 */
static void qos_traverse_graph(QOSGraphNode *machine, QOSTestCallback callback)
{
    QOSGraphNode *v, *dest_node, *path;
    QOSStackElement *s_el;
    QOSGraphEdge *e, *next;
    QOSGraphEdgeList *list;

    qos_push(machine, NULL, NULL);

    while (qos_node_tos > 0) {
        s_el = qos_tos();
        v = s_el->node;
        if (v->visited) {
            qos_pop();
            continue;
        }
        v->visited = TRUE;
        list = get_edgelist(v->name);
        if (!list) {
            qos_pop();
            if (v->type == TEST) {
                v->visited = FALSE;
                path = qos_reverse_path(s_el);
                callback(path, s_el->length);
            }
        } else {
            QSLIST_FOREACH_SAFE(e, list, edge_list, next) {
                dest_node = search_node(e->dest);

                if (!dest_node) {
                    printf("node %s in %s -> %s does not exist\n",
                            e->dest, v->name, e->dest);
                    abort();
                }

                if (!dest_node->visited && dest_node->available) {
                    qos_push(dest_node, s_el, e);
                }
            }
        }
    }
}

/* QGRAPH API*/

QOSGraphNode *qos_graph_get_node(const char *key)
{
    return search_node(key);
}

bool qos_graph_has_node(const char *node)
{
    QOSGraphNode *n = search_node(node);
    return n != NULL;
}

QOSNodeType qos_graph_get_node_type(const char *node)
{
    QOSGraphNode *n = search_node(node);
    if (n) {
        return n->type;
    }
    return -1;
}

bool qos_graph_get_node_availability(const char *node)
{
    QOSGraphNode *n = search_node(node);
    if (n) {
        return n->available;
    }
    return FALSE;
}

QOSGraphEdge *qos_graph_get_edge(const char *node, const char *dest)
{
    QOSGraphEdgeList *list = get_edgelist(node);
    return search_list_edges(list, dest);
}

QOSEdgeType qos_graph_get_edge_type(const char *node1, const char *node2)
{
    QOSGraphEdgeList *list = get_edgelist(node1);
    QOSGraphEdge *e = search_list_edges(list, node2);
    if (e) {
        return e->type;
    }
    return -1;
}

char *qos_graph_get_edge_dest(QOSGraphEdge *edge)
{
    if (!edge) {
        return NULL;
    }
    return edge->dest;
}

char *qos_graph_get_edge_arg(QOSGraphEdge *edge)
{
    if (!edge) {
        return NULL;
    }
    return edge->arg;
}

bool qos_graph_has_edge(const char *start, const char *dest)
{
    QOSGraphEdgeList *list = get_edgelist(start);
    QOSGraphEdge *e = search_list_edges(list, dest);
    if (e) {
        return TRUE;
    }
    return FALSE;
}

QOSGraphNode *qos_graph_get_machine(const char *node)
{
    return search_machine(node);
}

bool qos_graph_has_machine(const char *node)
{
    QOSGraphNode *m = search_machine(node);
    return m != NULL;
}

void qos_print_graph(void)
{
    qos_graph_foreach_test_path(qos_print_cb);
}

void qos_graph_init(void)
{
    if (!node_table) {
        node_table = g_hash_table_new_full(g_str_hash, g_str_equal,
                                        remove_string, remove_node);
        create_node(QOS_ROOT, DRIVER);
    }

    if (!edge_table) {
        edge_table = g_hash_table_new_full(g_str_hash, g_str_equal,
                                        remove_string, remove_edges);
    }
}

void qos_graph_destroy(void)
{
    if (node_table) {
        g_hash_table_destroy(node_table);
    }

    if (edge_table) {
        g_hash_table_destroy(edge_table);
    }

    node_table = NULL;
    edge_table = NULL;
}

void qos_node_destroy(void *key)
{
    g_hash_table_remove(node_table, key);
}

void qos_edge_destroy(void *key)
{
    g_hash_table_remove(edge_table, key);
}

void qos_add_test(const char *name, const char *driver, QOSTestFunc test_func)
{
    qos_add_test_data_args(name, driver, test_func, NULL, NULL);
}

void qos_add_test_args(const char *name, const char *driver,
                        QOSTestFunc test_func, const char *extra_args)
{
    qos_add_test_data_args(name, driver, test_func, NULL, extra_args);
}

void qos_add_test_data(const char *name, const char *driver,
                        QOSTestFunc test_func, void *arg)
{
    qos_add_test_data_args(name, driver, test_func, arg, NULL);
}

void qos_add_test_data_args(const char *name, const char *driver,
                            QOSTestFunc test_func, void *arg,
                            const char *extra_args)
{
    QOSGraphNode *node = create_node(name, TEST);
    build_test_cmd_line(node, extra_args);
    node->u.test.function = test_func;
    node->u.test.arg = arg;
    node->available = TRUE;
    add_edge(driver, name, CONSUMED_BY);
}

void qos_node_create_machine(const char *name, QOSCreateMachineFunc function)
{
    qos_node_create_machine_args(name, function, NULL);
}

void qos_node_create_machine_args(const char *name,
                                    QOSCreateMachineFunc function,
                                    const char *extra_args)
{
    QOSGraphNode *node = create_node(name, MACHINE);
    build_machine_cmd_line(node, extra_args);
    node->u.machine.constructor = function;
    add_edge(QOS_ROOT, name, CONTAINS);
}

void qos_node_create_driver(const char *name, QOSCreateDriverFunc function)
{
    qos_node_create_driver_args(name, function, NULL);
}

void qos_node_create_driver_args(const char *name,
                                    QOSCreateDriverFunc function,
                                    const char *extra_args)
{
    QOSGraphNode *node = create_node(name, DRIVER);
    build_driver_cmd_line(node, extra_args);
    node->u.driver.constructor = function;
}

void qos_node_create_interface(const char *name)
{
    create_node(name, INTERFACE);
}

void qos_node_contains(const char *container, const char *contained)
{
    add_edge(container, contained, CONTAINS);
}

void qos_node_contains_arg(const char *container, const char *contained,
                            const char *arg)
{
    add_edge_arg(container, contained, CONTAINS, arg);
}

void qos_node_produces(const char *producer, const char *produced)
{
    add_edge(producer, produced, PRODUCES);
}

void qos_node_consumes(const char *consumer, const char *consumed)
{
    add_edge(consumed, consumer, CONSUMED_BY);
}

void qos_node_consumes_arg(const char *consumer, const char *consumed,
                                const char *arg)
{
    add_edge_arg(consumed, consumer, CONSUMED_BY, arg);
}

void qos_graph_node_set_availability(const char *node, bool av)
{
    QOSGraphEdgeList *elist;
    QOSGraphNode *n = search_node(node);
    QOSGraphEdge *e, *next;
    if (!n) {
        return;
    }
    n->available = av;
    elist = get_edgelist(node);
    if (!elist) {
        return;
    }
    QSLIST_FOREACH_SAFE(e, elist, edge_list, next) {
        if (e->type == CONTAINS || e->type == PRODUCES) {
            qos_graph_node_set_availability(e->dest, av);
        }
    }
}

void qos_graph_foreach_test_path(QOSTestCallback fn)
{
    QOSGraphNode *root = qos_graph_get_node(QOS_ROOT);
    qos_traverse_graph(root, fn);
}

void qos_destroy_object(QOSGraphObject *obj)
{
    if (!obj || !(obj->destructor)) {
        return;
    }
    obj->destructor(obj);
}

void qos_separate_arch_machine(char *name, char **arch, char **machine)
{
    *arch = name;
    while (*name != '\0' && *name != '/') {
        name++;
    }

    if (*name == '/' && (*name + 1) != '\0') {
        *machine = name + 1;
    } else {
        printf("Machine name has to be of the form <arch>/<machine>\n");
        abort();
    }
}
