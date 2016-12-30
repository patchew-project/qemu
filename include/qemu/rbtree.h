/*
  Red Black Trees
  (C) 1999  Andrea Arcangeli <andrea@suse.de>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  include/qemu/rbtree.h

  To use rbtrees you'll have to implement your own insert and search cores.
  This will avoid us to use callbacks and to drop drammatically performances.
  I know it's not the cleaner way,  but in C (not in C++) to get
  performances and genericity...
*/

#ifndef QEMU_RBTREE_H
#define QEMU_RBTREE_H

#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

struct RbNode {
    uintptr_t __rb_parent_color;
    struct RbNode *rb_right;
    struct RbNode *rb_left;
} __attribute__((aligned(sizeof(uintptr_t))));
    /* The alignment might seem pointless, but allegedly CRIS needs it */

struct RbRoot {
    struct RbNode *rb_node;
};


#define RB_PARENT(r) ((struct RbNode *)((r)->__rb_parent_color & ~3))

#define RB_ROOT (struct RbRoot) { NULL, }
#define RB_ENTRY(ptr, type, member) container_of(ptr, type, member)

#define RB_EMPTY_ROOT(root) ((root)->rb_node == NULL)

/* 'empty' nodes are nodes that are known not to be inserted in an rbtree */
#define RB_EMPTY_NODE(node)  \
    ((node)->__rb_parent_color == (uintptr_t)(node))
#define RB_CLEAR_NODE(node)  \
    ((node)->__rb_parent_color = (uintptr_t)(node))


extern void rb_insert_color(struct RbNode *, struct RbRoot *);
extern void rb_erase(struct RbNode *, struct RbRoot *);


/* Find logical next and previous nodes in a tree */
extern struct RbNode *rb_next(const struct RbNode *);
extern struct RbNode *rb_prev(const struct RbNode *);
extern struct RbNode *rb_first(const struct RbRoot *);
extern struct RbNode *rb_last(const struct RbRoot *);

/* Postorder iteration - always visit the parent after its children */
extern struct RbNode *rb_first_postorder(const struct RbRoot *);
extern struct RbNode *rb_next_postorder(const struct RbNode *);

/* Fast replacement of a single node without remove/rebalance/add/rebalance */
extern void rb_replace_node(struct RbNode *victim, struct RbNode *new,
                            struct RbRoot *root);

static inline void rb_link_node(struct RbNode *node, struct RbNode *parent,
                                struct RbNode **rb_link)
{
    node->__rb_parent_color = (uintptr_t)parent;
    node->rb_left = node->rb_right = NULL;

    *rb_link = node;
}

#define RB_ENTRY_SAFE(ptr, type, member)                 \
    ({ typeof(ptr) ____ptr = (ptr);                      \
       ____ptr ? rb_entry(____ptr, type, member) : NULL; \
    })

/**
 * rbtree_postorder_for_each_entry_safe - iterate over rb_root in post order of
 * given type safe against removal of rb_node entry
 *
 * @pos:   the 'type *' to use as a loop cursor.
 * @n:     another 'type *' to use as temporary storage
 * @root:  'rb_root *' of the rbtree.
 * @field: the name of the rb_node field within 'type'.
 */
#define RBTREE_POSTORDER_FOR_EACH_ENTRY_SAFE(pos, n, root, field)            \
    for (pos = rb_entry_safe(rb_first_postorder(root), typeof(*pos), field); \
         pos && ({ n = rb_entry_safe(rb_next_postorder(&pos->field),         \
         typeof(*pos), field); 1; });                                        \
         pos = n)

#endif  /* QEMU_RBTREE_H */
