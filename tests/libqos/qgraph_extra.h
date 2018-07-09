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

#ifndef QGRAPH_EXTRA_H
#define QGRAPH_EXTRA_H

#include "qgraph.h"
#define PRINT_DEBUG 0

/**
 * qos_graph_get_node(): returns the node mapped to that @key.
 * It performs an hash map search O(1)
 *
 * Returns: on success: the %QOSGraphNode
 *          otherwise: #NULL
 */
QOSGraphNode *qos_graph_get_node(const char *key);

/**
 * qos_graph_has_node(): returns #TRUE if the node
 * has map has a node mapped to that @key.
 */
bool qos_graph_has_node(const char *node);

/**
 * qos_graph_get_node_type(): returns the %QOSNodeType
 * of the node @node.
 * It performs an hash map search O(1)
 * Returns: on success: the %QOSNodeType
 *          otherwise: #-1
 */
QOSNodeType qos_graph_get_node_type(const char *node);

/**
 * qos_graph_get_node_availability(): returns the availability (boolean)
 * of the node @node.
 */
bool qos_graph_get_node_availability(const char *node);

/**
 * qos_graph_get_edge(): returns the edge
 * linking of the node @node with @dest.
 *
 * Returns: on success: the %QOSGraphEdge
 *          otherwise: #NULL
 */
QOSGraphEdge *qos_graph_get_edge(const char *node, const char *dest);

/**
 * qos_graph_get_edge_type(): returns the edge type
 * of the edge linking of the node @start with @dest.
 *
 * Returns: on success: the %QOSEdgeType
 *          otherwise: #-1
 */
QOSEdgeType qos_graph_get_edge_type(const char *start, const char *dest);

/**
 * qos_graph_get_edge_dest(): returns the name of the node
 * pointed as destination of edge @edge.
 *
 * Returns: on success: the destination
 *          otherwise: #NULL
 */
char *qos_graph_get_edge_dest(QOSGraphEdge *edge);

/**
 * qos_graph_has_edge(): returns #TRUE if there
 * exists an edge from @start to @dest.
 */
bool qos_graph_has_edge(const char *start, const char *dest);

/**
 * qos_graph_get_edge_arg(): returns the args assigned
 * to that @edge.
 *
 * Returns: on success: the arg
 *          otherwise: #NULL
 */
char *qos_graph_get_edge_arg(QOSGraphEdge *edge);

/**
 * qos_graph_get_machine(): returns the machine assigned
 * to that @node name.
 *
 * It performs a search only trough the list of machines
 * (i.e. the QOS_ROOT child).
 *
 * Returns: on success: the %QOSGraphNode
 *          otherwise: #NULL
 */
QOSGraphNode *qos_graph_get_machine(const char *node);

/**
 * qos_graph_has_machine(): returns #TRUE if the node
 * has map has a node mapped to that @node.
 */
bool qos_graph_has_machine(const char *node);


/**
 * qos_print_graph(): walks the graph and prints
 * all machine-to-test paths.
 */
void qos_print_graph(void);

/**
 * qos_graph_foreach_test_path(): executes the Depth First search
 * algorithm and applies @fn to all discovered paths.
 *
 * See qos_traverse_graph() in qgraph.c for more info on
 * how it works.
 */
void qos_graph_foreach_test_path(QOSTestCallback fn);

/**
 * qos_destroy_object(): calls the destructor for @obj
 */
void qos_destroy_object(QOSGraphObject *obj);

/**
 * qos_separate_arch_machine(): separate arch from machine.
 * This function requires every machine @name to be in the form
 * <arch>/<machine_name>, like "arm/raspi2" or "x86_64/pc".
 *
 * The function will split then the string in two parts,
 * assigning @arch to point to <arch>/<machine_name>, and
 * @machine to <machine_name>.
 *
 * For example, "x86_64/pc" will be split in this way:
 * *arch = "x86_64/pc"
 * *machine = "pc"
 *
 * Note that this function *does not* allocate any new string,
 * but just sets the pointer *arch and *machine to the respective
 * part of the string.
 */
void qos_separate_arch_machine(char *name, char **arch, char **machine);

#endif
