#ifndef ENVLIST_H
#define ENVLIST_H

#include "qemu/queue.h"

struct envlist_entry {
    const char *ev_var;            /* actual env value */
    QLIST_ENTRY(envlist_entry) ev_link;
};

struct envlist {
    QLIST_HEAD(, envlist_entry) el_entries; /* actual entries */
    size_t el_count;                        /* number of entries */
};

typedef struct envlist envlist_t;

envlist_t *envlist_create(void);
void envlist_free(envlist_t *);
int envlist_setenv(envlist_t *, const char *);
int envlist_unsetenv(envlist_t *, const char *);
int envlist_appendenv(envlist_t *, const char *, const char *);
int envlist_parse_set(envlist_t *, const char *);
int envlist_parse_unset(envlist_t *, const char *);
char **envlist_to_environ(const envlist_t *, size_t *);

#endif /* ENVLIST_H */
