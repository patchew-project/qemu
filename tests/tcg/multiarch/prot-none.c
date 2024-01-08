/*
 * Test that GDB can access PROT_NONE pages.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

void break_here(long *p)
{
}

int main(void)
{
    long pagesize = sysconf(_SC_PAGESIZE);
    int err;
    long *p;

    p = mmap(NULL, pagesize, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(p != MAP_FAILED);
    *p = 42;

    err = mprotect(p, pagesize, PROT_NONE);
    assert(err == 0);

    break_here(p);

    err = mprotect(p, pagesize, PROT_READ);
    assert(err == 0);
    if (getenv("PROT_NONE_PY")) {
        assert(*p == 24);
    }

    return EXIT_SUCCESS;
}
