#ifndef QEMU_TSAN_H
#define QEMU_TSAN_H
/*
 * tsan.h
 *
 * This file defines macros used to give ThreadSanitizer
 * additional information to help suppress warnings.
 * This is necessary since TSan does not provide a header file
 * for these annotations.  The standard way to include these
 * is via the below macros.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifdef CONFIG_TSAN
#define TSAN_ANNOTATE_HAPPENS_BEFORE(addr) \
    AnnotateHappensBefore(__FILE__, __LINE__, (void *)(addr))
#define TSAN_ANNOTATE_HAPPENS_AFTER(addr) \
    AnnotateHappensAfter(__FILE__, __LINE__, (void *)(addr))
#define TSAN_ANNOTATE_THREAD_NAME(name) \
    AnnotateThreadName(__FILE__, __LINE__, (void *)(name))
#define TSAN_ANNOTATE_IGNORE_READS_BEGIN() \
    AnnotateIgnoreReadsBegin(__FILE__, __LINE__)
#define TSAN_ANNOTATE_IGNORE_READS_END() \
    AnnotateIgnoreReadsEnd(__FILE__, __LINE__)
#define TSAN_ANNOTATE_IGNORE_WRITES_BEGIN() \
    AnnotateIgnoreWritesBegin(__FILE__, __LINE__)
#define TSAN_ANNOTATE_IGNORE_WRITES_END() \
    AnnotateIgnoreWritesEnd(__FILE__, __LINE__)
#else
#define TSAN_ANNOTATE_HAPPENS_BEFORE(addr)
#define TSAN_ANNOTATE_HAPPENS_AFTER(addr)
#define TSAN_ANNOTATE_THREAD_NAME(name)
#define TSAN_ANNOTATE_IGNORE_READS_BEGIN()
#define TSAN_ANNOTATE_IGNORE_READS_END()
#define TSAN_ANNOTATE_IGNORE_WRITES_BEGIN()
#define TSAN_ANNOTATE_IGNORE_WRITES_END()
#endif

void AnnotateHappensBefore(const char *f, int l, void *addr);
void AnnotateHappensAfter(const char *f, int l, void *addr);
void AnnotateThreadName(const char *f, int l, char *name);
void AnnotateIgnoreReadsBegin(const char *f, int l);
void AnnotateIgnoreReadsEnd(const char *f, int l);
void AnnotateIgnoreWritesBegin(const char *f, int l);
void AnnotateIgnoreWritesEnd(const char *f, int l);
#endif
