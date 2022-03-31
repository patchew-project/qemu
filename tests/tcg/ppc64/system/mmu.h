#ifndef PPC64_MMU_H
#define PPC64_MMU_H

int test_read(long *addr, long *ret, long init);
int test_write(long *addr, long val);
int test_dcbz(long *addr);
int test_exec(int testno, unsigned long pc, unsigned long msr);

#endif
