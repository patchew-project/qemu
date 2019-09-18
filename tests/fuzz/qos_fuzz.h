#ifndef _QOS_FUZZ_H_
#define _QOS_FUZZ_H_

#include "tests/libqos/qgraph.h"

int qos_fuzz(const unsigned char *Data, size_t Size);
void qos_setup(void);

extern void *fuzz_qos_obj;
extern QGuestAllocator *fuzz_qos_alloc;

void fuzz_add_qos_target(const char *name,
        const char *description,
        const char *interface,
        QOSGraphTestOptions *opts,
        FuzzTarget *fuzz_opts);

void qos_init_path(QTestState *);
#endif
