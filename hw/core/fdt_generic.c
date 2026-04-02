/*
 * SPDX-License-Identifier: MIT
 *
 * Tables of FDT device models and their init functions. Keyed by compatibility
 * strings, device instance names.
 *
 * Copyright (c) 2010 PetaLogix Qld Pty Ltd.
 * Copyright (c) 2010 Peter A. G. Crosthwaite <peter.crosthwaite@petalogix.com>.
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
#include "hw/core/fdt_generic.h"
#include "hw/core/qdev-properties.h"
#include "qemu/log.h"


#define FDT_GENERIC_MAX_PATTERN_LEN 1024

typedef struct TableListNode {
    struct TableListNode *next;
    char key[FDT_GENERIC_MAX_PATTERN_LEN];
    FDTInitFn fdt_init;
    void *opaque;
} TableListNode;

/* add a node to the table specified by *head_p */

static void add_to_table(
        FDTInitFn fdt_init,
        const char *key,
        void *opaque,
        TableListNode **head_p)
{
    TableListNode *nn = malloc(sizeof(*nn));
    nn->next = *head_p;
    strcpy(nn->key, key);
    nn->fdt_init = fdt_init;
    nn->opaque = opaque;
    *head_p = nn;
}

/* FIXME: add return codes that differentiate between not found and error */

/*
 * search a table for a key string and call the fdt init function if found.
 * Returns 0 if a match is found, 1 otherwise
 */

static int fdt_init_search_table(
        char *node_path,
        FDTMachineInfo *fdti,
        const char *key, /* string to match */
        TableListNode **head) /* head of the list to search */
{
    TableListNode *iter;

    for (iter = *head; iter != NULL; iter = iter->next) {
        if (!strcmp(key, iter->key)) {
            if (iter->fdt_init) {
                return iter->fdt_init(node_path, fdti, iter->opaque);
            }
            return 0;
        }
    }

    return 1;
}

TableListNode *compat_list_head;

void add_to_compat_table(FDTInitFn fdt_init, const char *compat, void *opaque)
{
    add_to_table(fdt_init, compat, opaque, &compat_list_head);
}

int fdt_init_compat(char *node_path, FDTMachineInfo *fdti, const char *compat)
{
    return fdt_init_search_table(node_path, fdti, compat, &compat_list_head);
}

TableListNode *inst_bind_list_head;

void add_to_inst_bind_table(FDTInitFn fdt_init, const char *name, void *opaque)
{
    add_to_table(fdt_init, name, opaque, &inst_bind_list_head);
}

int fdt_init_inst_bind(char *node_path, FDTMachineInfo *fdti,
        const char *name)
{
    return fdt_init_search_table(node_path, fdti, name, &inst_bind_list_head);
}
