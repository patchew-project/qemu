#ifndef FUZZ_H
#define FUZZ_H

bool fuzz_allowed;

static inline bool fuzz_enabled(void)
{
    return fuzz_allowed;
}

bool fuzz_driver(void);

void fuzz_init(const char *fuzz_chrdev, const char *fuzz_log, Error **errp);

#endif
