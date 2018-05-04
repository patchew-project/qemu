/*
 * An very simplified interval tree implementation based on GTree.
 *
 * Copyright 2018 Red Hat, Inc.
 *
 * Authors:
 *  Peter Xu <peterx@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 */
#ifndef INTERVAL_TREE_H
#define INTERVAL_TREE_H

/*
 * Currently the interval tree will only allow to keep ranges
 * information, and no extra user data is allowed for each element.  A
 * benefit is that we can merge adjacent ranges internally within the
 * tree.  It can save a lot of memory when the ranges are splitted but
 * mostly continuous.
 *
 * Note that current implementation does not provide any thread
 * protections.  Callers of the interval tree should be responsible
 * for the thread safety issue.
 */

#include <glib.h>

#define  IT_OK           (0)
#define  IT_ERR_OVERLAP  (-1)

typedef unsigned long long ITValue;
typedef struct ITTree ITTree;
typedef gboolean (*it_tree_iterator)(ITValue start, ITValue end);

struct ITRange {
    ITValue start;
    ITValue end;
};
typedef struct ITRange ITRange;

/**
 * it_tree_new:
 *
 * Create a new interval tree.
 *
 * Returns: the tree pointer when succeeded, or NULL if error.
 */
ITTree *it_tree_new(void);

/**
 * it_tree_insert:
 *
 * @tree: the interval tree to insert
 * @start: the start of range, inclusive
 * @end: the end of range, inclusive
 *
 * Insert an interval range to the tree.  If there is overlapped
 * ranges, IT_ERR_OVERLAP will be returned.
 *
 * Return: 0 if succeeded, or <0 if error.
 */
int it_tree_insert(ITTree *tree, ITValue start, ITValue end);

/**
 * it_tree_remove:
 *
 * @tree: the interval tree to remove range from
 * @start: the start of range, inclusive
 * @end: the end of range, inclusive
 *
 * Remove an range from the tree.  The range does not need to be
 * exactly what has inserted.  All the ranges that are included in the
 * provided range will be removed from the tree.
 *
 * Return: 0 if succeeded, or <0 if error.
 */
int it_tree_remove(ITTree *tree, ITValue start, ITValue end);

/**
 * it_tree_find:
 *
 * @tree: the interval tree to search from
 * @start: the start of range, inclusive
 * @end: the end of range, inclusive
 *
 * Search for a range in the interval tree that overlaps with the
 * range specified.  Only the first found range will be returned.
 *
 * Return: ITRange if found, or NULL if not found.  Note: the returned
 * ITRange pointer is maintained internally.  User should only read
 * the content but never modify or free the content.
 */
ITRange *it_tree_find(ITTree *tree, ITValue start, ITValue end);

/**
 * it_tree_find_value:
 *
 * @tree: the interval tree to search from
 * @value: the value to find
 *
 * Similar to it_tree_find(), but it tries to find range (value, value).
 *
 * Return: same as it_tree_find().
 */
ITRange *it_tree_find_value(ITTree *tree, ITValue value);

/**
 * it_tree_foreach:
 *
 * @tree: the interval tree to iterate on
 * @iterator: the interator for the ranges, return true to stop
 *
 * Search for a range in the interval tree.
 *
 * Return: 1 if found any overlap, 0 if not, <0 if error.
 */
void it_tree_foreach(ITTree *tree, it_tree_iterator iterator);

/**
 * it_tree_destroy:
 *
 * @tree: the interval tree to destroy
 *
 * Destroy an existing interval tree.
 *
 * Return: None.
 */
void it_tree_destroy(ITTree *tree);

#endif
