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

#ifndef QGRAPH_H
#define QGRAPH_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <gmodule.h>
#include <glib.h>
#include "qemu/module.h"

/* maximum path length */
#define QOS_PATH_MAX_ELEMENT_SIZE 50

typedef struct QOSGraphMachine QOSGraphMachine;
typedef struct QOSGraphMachineList QOSGraphMachineList;
typedef struct QOSGraphObject QOSGraphObject;
typedef struct QOSGraphNode QOSGraphNode;
typedef struct QOSGraphEdge QOSGraphEdge;
typedef struct QOSGraphEdgeList QOSGraphEdgeList;
typedef enum QOSEdgeType QOSEdgeType;
typedef enum QOSNodeType QOSNodeType;

typedef void *(*QOSCreateDriverFunc) (void *parent, QOSGraphObject *alloc);
typedef void *(*QOSCreateMachineFunc) (void);
typedef void (*QOSTestFunc) (void *parent, void *arg);
typedef void (*QOSTestCallback) (QOSGraphNode *path, int len);

typedef void *(*QOSGetDriver) (void *object, const char *interface);
typedef QOSGraphObject *(*QOSGetDevice) (void *object, const char *name);
typedef void (*QOSDestructorFunc) (QOSGraphObject *object);

/* edge types*/
enum QOSEdgeType {
    CONTAINS,
    PRODUCES,
    CONSUMED_BY
};

/* node types*/
enum QOSNodeType {
    MACHINE,
    DRIVER,
    INTERFACE,
    TEST
};

/**
 * Each driver, test or machine will have this as first field.
 * Depending on the edge, the node will call the corresponding
 * function when walking the path.
 *
 * QOSGraphObject also provides a destructor, used to deallocate the
 * after the test has been executed.
 */
struct QOSGraphObject {
    /* for produces, returns void * */
    QOSGetDriver get_driver;
    /* for contains, returns a QOSGraphObject * */
    QOSGetDevice get_device;
    /* destroy this QOSGraphObject */
    QOSDestructorFunc destructor;
};

/* Graph Node */
struct QOSGraphNode {
    QOSNodeType type;
    bool available;     /* set by QEMU via QMP, used during graph walk */
    bool visited;       /* used during graph walk */
    char *name;         /* used to identify the node */
    char *command_line; /* used to start QEMU at test execution */
    union {
        struct {
            QOSCreateDriverFunc constructor;
        } driver;
        struct {
            QOSCreateMachineFunc constructor;
        } machine;
        struct {
            QOSTestFunc function;
            void *arg;
        } test;
    } u;

    /**
     * only used when traversing the path, never rely on that except in the
     * qos_traverse_graph callback function
     */
    QOSGraphEdge *path_edge;
};

/**
 * qos_graph_init(): initialize the framework, creates two hash
 * tables: one for the nodes and another for the edges.
 */
void qos_graph_init(void);

/**
 * qos_graph_destroy(): deallocates all the hash tables,
 * freeing all nodes and edges.
 */
void qos_graph_destroy(void);

/**
 * qos_node_destroy(): removes and frees a node from the,
 * nodes hash table.
 */
void qos_node_destroy(void *key);

/**
 * qos_edge_destroy(): removes and frees an edge from the,
 * edges hash table.
 */
void qos_edge_destroy(void *key);

/**
 * qos_add_test(): adds a test node @name to the nodes hash table.
 *
 * The test will consume a @driver node, and once the
 * graph walking algorithm has found it, the @test_func will be
 * executed.
 */
void qos_add_test(const char *name, const char *driver, QOSTestFunc test_func);

/**
 * qos_add_test_args(): same as qos_add_test, with the possibility to
 * add an optional @extra_args for the command line.
 */
void qos_add_test_args(const char *name, const char *driver,
                        QOSTestFunc test_func,
                        const char *extra_args);

/**
 * qos_add_test(): adds a test node @name to the nodes hash table.
 *
 * The test will consume a @driver node, and once the
 * graph walking algorithm has found it, the @test_func will be
 * executed passing @arg as parameter.
 */
void qos_add_test_data(const char *name, const char *driver,
                            QOSTestFunc test_func, void *arg);

/**
 * qos_add_test_data_args(): same as qos_add_test_data, with the possibility to
 * add an optional @extra_args for the command line.
 */
void qos_add_test_data_args(const char *name, const char *driver,
                            QOSTestFunc test_func, void *arg,
                            const char *extra_args);

/**
 * qos_node_create_machine(): creates the machine @name and
 * adds it to the node hash table.
 *
 * This node will be of type MACHINE and have @function as constructor
 */
void qos_node_create_machine(const char *name, QOSCreateMachineFunc function);

/**
 * qos_node_create_machine_args(): same as qos_node_create_machine,
 * but with the possibility to add an optional @extra_args to the
 * command line.
 */
void qos_node_create_machine_args(const char *name,
                                    QOSCreateMachineFunc function,
                                    const char *extra_args);

/**
 * qos_node_create_driver(): creates the driver @name and
 * adds it to the node hash table.
 *
 * This node will be of type DRIVER and have @function as constructor
 */
void qos_node_create_driver(const char *name, QOSCreateDriverFunc function);

/**
 * qos_node_create_driver_args(): same as qos_node_create_driver,
 * but with the possibility to add an optional @extra_args to the
 * command line.
 */
void qos_node_create_driver_args(const char *name,
                                    QOSCreateDriverFunc function,
                                    const char *extra_args);

/**
 * qos_node_create_driver(): creates the interface @name and
 * adds it to the node hash table.
 *
 * This node will be of type INTERFACE and won't have any constructor
 */
void qos_node_create_interface(const char *name);

/**
 * qos_node_contains(): creates the edge CONTAINS and
 * adds it to the edge list mapped to @container in the
 * edge hash table.
 *
 * This edge will have @container as source and @contained as destination.
 */
void qos_node_contains(const char *container, const char *contained);

/**
 * qos_node_contains_arg(): same as qos_node_contains,
 * but with the possibility to add an optional @arg to the
 * command line.
 */
void qos_node_contains_arg(const char *container, const char *contained,
                            const char *arg);

/**
 * qos_node_produces(): creates the edge PRODUCES and
 * adds it to the edge list mapped to @producer in the
 * edge hash table.
 *
 * This edge will have @producer as source and @produced as destination.
 */
void qos_node_produces(const char *producer, const char *produced);

/**
 * qos_node_consumes(): creates the edge CONSUMED_BY and
 * adds it to the edge list mapped to @consumed in the
 * edge hash table.
 *
 * This edge will have @consumed as source and @consumer as destination.
 */
void qos_node_consumes(const char *consumer, const char *consumed);

/**
 * qos_node_consumes_arg(): same as qos_node_consumes,
 * but with the possibility to add an optional @arg to the
 * command line.
 */
void qos_node_consumes_arg(const char *consumer, const char *consumed,
                                const char *arg);

/**
 * qos_graph_node_set_availability(): sets the node identified
 * by @node with availability @av.
 */
void qos_graph_node_set_availability(const char *node, bool av);

#endif
