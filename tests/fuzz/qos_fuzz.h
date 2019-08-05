#ifndef _QOS_FUZZ_H_
#define _QOS_FUZZ_H_

#include "tests/libqos/qgraph.h"

int qos_fuzz(const unsigned char *Data, size_t Size);
void qos_setup(void);

extern char **fuzz_path_vec;
extern int qos_argc;
extern char **qos_argv;
extern void *qos_obj;
extern QGuestAllocator *qos_alloc;


void fuzz_add_qos_target(const char *name,
        const char *description,
        const char *interface,
        QOSGraphTestOptions *opts,
        FuzzTarget *fuzz_opts);

void qos_init_path(void);
#endif
