/*
 * GLIB - Library of useful routines for C programming
 * Copyright (C) 1995-1997  Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Modified by the GLib Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GLib Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GLib at ftp://ftp.gtk.org/pub/gtk/.
 */

#ifndef QEMU_QTREE_H
#define QEMU_QTREE_H

typedef struct _QTree  QTree;

typedef struct _QTreeNode QTreeNode;

typedef gboolean (*QTraverseNodeFunc)(QTreeNode *node,
                                      gpointer user_data);

/*
 * Balanced binary trees
 */
QTree *q_tree_new(GCompareFunc key_compare_func);
QTree *q_tree_new_with_data(GCompareDataFunc key_compare_func,
                            gpointer key_compare_data);
QTree *q_tree_new_full(GCompareDataFunc key_compare_func,
                       gpointer key_compare_data,
                       GDestroyNotify key_destroy_func,
                       GDestroyNotify value_destroy_func);
QTreeNode *q_tree_node_first(QTree *tree);

QTreeNode *q_tree_node_last(QTree *tree);

QTreeNode *q_tree_node_previous(QTreeNode *node);

QTreeNode *q_tree_node_next(QTreeNode *node);

QTree *q_tree_ref(QTree *tree);

void q_tree_unref(QTree *tree);

void q_tree_destroy(QTree *tree);

QTreeNode *q_tree_insert_node(QTree *tree,
                              gpointer key,
                              gpointer value);

void q_tree_insert(QTree *tree,
                   gpointer key,
                   gpointer value);

QTreeNode *q_tree_replace_node(QTree *tree,
                               gpointer key,
                               gpointer value);

void q_tree_replace(QTree *tree,
                    gpointer key,
                    gpointer value);
gboolean q_tree_remove(QTree *tree,
                       gconstpointer key);

void q_tree_remove_all(QTree *tree);

gboolean q_tree_steal(QTree *tree,
                      gconstpointer key);
gpointer q_tree_node_key(QTreeNode *node);
gpointer q_tree_node_value(QTreeNode *node);
QTreeNode *q_tree_lookup_node(QTree *tree,
                              gconstpointer key);
gpointer q_tree_lookup(QTree *tree,
                       gconstpointer key);
gboolean q_tree_lookup_extended(QTree *tree,
                                gconstpointer lookup_key,
                                gpointer *orig_key,
                                gpointer *value);
void q_tree_foreach(QTree *tree,
                    GTraverseFunc func,
                    gpointer user_data);
void q_tree_foreach_node(QTree *tree,
                         QTraverseNodeFunc func,
                         gpointer user_data);

void q_tree_traverse(QTree *tree,
                     GTraverseFunc traverse_func,
                     GTraverseType traverse_type,
                     gpointer user_data);

QTreeNode *q_tree_search_node(QTree *tree,
                              GCompareFunc search_func,
                              gconstpointer user_data);
gpointer q_tree_search(QTree *tree,
                       GCompareFunc search_func,
                       gconstpointer user_data);
QTreeNode *q_tree_lower_bound(QTree *tree,
                              gconstpointer key);
QTreeNode *q_tree_upper_bound(QTree *tree,
                              gconstpointer key);
gint q_tree_height(QTree *tree);
gint q_tree_nnodes(QTree *tree);

#endif /* QEMU_QTREE_H */
