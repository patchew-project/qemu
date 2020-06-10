/*
 * QEMU Error Objects
 *
 * Copyright IBM, Corp. 2011
 * Copyright (C) 2011-2015 Red Hat, Inc.
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Markus Armbruster <armbru@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.  See
 * the COPYING.LIB file in the top-level directory.
 */

/*
 * Error reporting system loosely patterned after Glib's GError.
 *
 * = Deal with Error object =
 *
 * Create an error:
 *     error_setg(&err, "situation normal, all fouled up");
 *
 * Create an error and add additional explanation:
 *     error_setg(&err, "invalid quark");
 *     error_append_hint(&err, "Valid quarks are up, down, strange, "
 *                       "charm, top, bottom.\n");
 *
 * Do *not* contract this to
 *     error_setg(&err, "invalid quark\n"
 *                "Valid quarks are up, down, strange, charm, top, bottom.");
 *
 * Report an error to the current monitor if we have one, else stderr:
 *     error_report_err(err);
 * This frees the error object.
 *
 * Likewise, but with additional text prepended:
 *     error_reportf_err(err, "Could not frobnicate '%s': ", name);
 *
 * Report an error somewhere else:
 *     const char *msg = error_get_pretty(err);
 *     do with msg what needs to be done...
 *     error_free(err);
 * Note that this loses hints added with error_append_hint().
 *
 * Handle an error without reporting it (just for completeness):
 *     error_free(err);
 *
 * Assert that an expected error occurred, but clean it up without
 * reporting it (primarily useful in testsuites):
 *     error_free_or_abort(&err);
 *
 * = Deal with Error ** function parameter =
 *
 * A function may use the error system to return errors. In this case, the
 * function defines an Error **errp parameter, by convention the last one (with
 * exceptions for functions using ... or va_list).
 *
 * The caller may then pass in the following errp values:
 *
 * 1. &error_abort
 *    Any error will result in abort().
 * 2. &error_fatal
 *    Any error will result in exit() with a non-zero status.
 * 3. NULL
 *    No error reporting through errp parameter.
 * 4. The address of a NULL-initialized Error *err
 *    Any error will populate errp with an error object.
 *
 * The following rules then implement the correct semantics desired by the
 * caller.
 *
 * Create a new error to pass to the caller:
 *     error_setg(errp, "situation normal, all fouled up");
 *
 * Calling another errp-based function:
 *     f(..., errp);
 *
 * == Checking success of subcall ==
 *
 * If a function returns a value indicating an error in addition to setting
 * errp (which is recommended), then you don't need any additional code, just
 * do:
 *
 *     int ret = f(..., errp);
 *     if (ret < 0) {
 *         ... handle error ...
 *         return ret;
 *     }
 *
 * If a function returns nothing (not recommended for new code), the only way
 * to check success is by consulting errp; doing this safely requires the use
 * of the ERRP_AUTO_PROPAGATE macro, like this:
 *
 *     int our_func(..., Error **errp) {
 *         ERRP_AUTO_PROPAGATE();
 *         ...
 *         subcall(..., errp);
 *         if (*errp) {
 *             ...
 *             return -EINVAL;
 *         }
 *         ...
 *     }
 *
 * ERRP_AUTO_PROPAGATE takes care of wrapping the original errp as needed, so
 * that the rest of the function can directly use errp (including
 * dereferencing), where any errors will then be propagated on to the original
 * errp when leaving the function.
 *
 * In some cases, we need to check result of subcall, but do not want to
 * propagate the Error object to our caller. In such cases we don't need
 * ERRP_AUTO_PROPAGATE, but just a local Error object:
 *
 * Receive an error and not pass it:
 *     Error *err = NULL;
 *     subcall(arg, &err);
 *     if (err) {
 *         handle the error...
 *         error_free(err);
 *     }
 *
 * Note that older code that did not use ERRP_AUTO_PROPAGATE would instead need
 * a local Error * variable and the use of error_propagate() to properly handle
 * all possible caller values of errp. Now this is DEPRECATED* (see below).
 *
 * Note that any function that wants to modify an error object, such as by
 * calling error_append_hint or error_prepend, must use ERRP_AUTO_PROPAGATE, in
 * order for a caller's use of &error_fatal to see the additional information.
 *
 * In rare cases, we need to pass existing Error object to the caller by hand:
 *     error_propagate(errp, err);
 *
 * Pass an existing error to the caller with the message modified:
 *     error_propagate_prepend(errp, err);
 *
 *
 * Call a function ignoring errors:
 *     foo(arg, NULL);
 *
 * Call a function aborting on errors:
 *     foo(arg, &error_abort);
 *
 * Call a function treating errors as fatal:
 *     foo(arg, &error_fatal);
 *
 * Receive and accumulate multiple errors (first one wins):
 *     Error *err = NULL, *local_err = NULL;
 *     foo(arg, &err);
 *     bar(arg, &local_err);
 *     error_propagate(&err, local_err);
 *     if (err) {
 *         handle the error...
 *     }
 *
 * Do *not* "optimize" this to
 *     foo(arg, &err);
 *     bar(arg, &err); // WRONG!
 *     if (err) {
 *         handle the error...
 *     }
 * because this may pass a non-null err to bar().
 *
 * DEPRECATED*
 *
 * The following pattern of receiving, checking, and then forwarding an error
 * to the caller by hand is now deprecated:
 *
 *     Error *err = NULL;
 *     foo(arg, &err);
 *     if (err) {
 *         handle the error...
 *         error_propagate(errp, err);
 *     }
 *
 * Instead, use ERRP_AUTO_PROPAGATE macro.
 *
 * The old pattern is deprecated because of two things:
 *
 * 1. Issue with error_abort and error_propagate: when we wrap error_abort by
 * local_err+error_propagate, the resulting coredump will refer to
 * error_propagate and not to the place where error happened.
 *
 * 2. A lot of extra code of the same pattern
 *
 * How to update old code to use ERRP_AUTO_PROPAGATE?
 *
 * All you need is to add ERRP_AUTO_PROPAGATE() invocation at function start,
 * than you may safely dereference errp to check errors and do not need any
 * additional local Error variables or calls to error_propagate().
 *
 * Example:
 *
 * old code
 *
 *     void fn(..., Error **errp) {
 *         Error *err = NULL;
 *         foo(arg, &err);
 *         if (err) {
 *             handle the error...
 *             error_propagate(errp, err);
 *             return;
 *         }
 *         ...
 *     }
 *
 * updated code
 *
 *     void fn(..., Error **errp) {
 *         ERRP_AUTO_PROPAGATE();
 *         foo(arg, errp);
 *         if (*errp) {
 *             handle the error...
 *             return;
 *         }
 *         ...
 *     }
 */

#ifndef ERROR_H
#define ERROR_H

#include "qapi/qapi-types-error.h"

/*
 * Overall category of an error.
 * Based on the qapi type QapiErrorClass, but reproduced here for nicer
 * enum names.
 */
typedef enum ErrorClass {
    ERROR_CLASS_GENERIC_ERROR = QAPI_ERROR_CLASS_GENERICERROR,
    ERROR_CLASS_COMMAND_NOT_FOUND = QAPI_ERROR_CLASS_COMMANDNOTFOUND,
    ERROR_CLASS_DEVICE_NOT_ACTIVE = QAPI_ERROR_CLASS_DEVICENOTACTIVE,
    ERROR_CLASS_DEVICE_NOT_FOUND = QAPI_ERROR_CLASS_DEVICENOTFOUND,
    ERROR_CLASS_KVM_MISSING_CAP = QAPI_ERROR_CLASS_KVMMISSINGCAP,
} ErrorClass;

/*
 * Get @err's human-readable error message.
 */
const char *error_get_pretty(const Error *err);

/*
 * Get @err's error class.
 * Note: use of error classes other than ERROR_CLASS_GENERIC_ERROR is
 * strongly discouraged.
 */
ErrorClass error_get_class(const Error *err);

/*
 * Create a new error object and assign it to *@errp.
 * If @errp is NULL, the error is ignored.  Don't bother creating one
 * then.
 * If @errp is &error_abort, print a suitable message and abort().
 * If @errp is &error_fatal, print a suitable message and exit(1).
 * If @errp is anything else, *@errp must be NULL.
 * The new error's class is ERROR_CLASS_GENERIC_ERROR, and its
 * human-readable error message is made from printf-style @fmt, ...
 * The resulting message should be a single phrase, with no newline or
 * trailing punctuation.
 * Please don't error_setg(&error_fatal, ...), use error_report() and
 * exit(), because that's more obvious.
 * Likewise, don't error_setg(&error_abort, ...), use assert().
 */
#define error_setg(errp, fmt, ...)                              \
    error_setg_internal((errp), __FILE__, __LINE__, __func__,   \
                        (fmt), ## __VA_ARGS__)
void error_setg_internal(Error **errp,
                         const char *src, int line, const char *func,
                         const char *fmt, ...)
    GCC_FMT_ATTR(5, 6);

/*
 * Just like error_setg(), with @os_error info added to the message.
 * If @os_error is non-zero, ": " + strerror(os_error) is appended to
 * the human-readable error message.
 *
 * The value of errno (which usually can get clobbered by almost any
 * function call) will be preserved.
 */
#define error_setg_errno(errp, os_error, fmt, ...)                      \
    error_setg_errno_internal((errp), __FILE__, __LINE__, __func__,     \
                              (os_error), (fmt), ## __VA_ARGS__)
void error_setg_errno_internal(Error **errp,
                               const char *fname, int line, const char *func,
                               int os_error, const char *fmt, ...)
    GCC_FMT_ATTR(6, 7);

#ifdef _WIN32
/*
 * Just like error_setg(), with @win32_error info added to the message.
 * If @win32_error is non-zero, ": " + g_win32_error_message(win32_err)
 * is appended to the human-readable error message.
 */
#define error_setg_win32(errp, win32_err, fmt, ...)                     \
    error_setg_win32_internal((errp), __FILE__, __LINE__, __func__,     \
                              (win32_err), (fmt), ## __VA_ARGS__)
void error_setg_win32_internal(Error **errp,
                               const char *src, int line, const char *func,
                               int win32_err, const char *fmt, ...)
    GCC_FMT_ATTR(6, 7);
#endif

/*
 * Propagate error object (if any) from @local_err to @dst_errp.
 * If @local_err is NULL, do nothing (because there's nothing to
 * propagate).
 * Else, if @dst_errp is NULL, errors are being ignored.  Free the
 * error object.
 * Else, if @dst_errp is &error_abort, print a suitable message and
 * abort().
 * Else, if @dst_errp is &error_fatal, print a suitable message and
 * exit(1).
 * Else, if @dst_errp already contains an error, ignore this one: free
 * the error object.
 * Else, move the error object from @local_err to *@dst_errp.
 * On return, @local_err is invalid.
 * Please don't error_propagate(&error_fatal, ...), use
 * error_report_err() and exit(), because that's more obvious.
 */
void error_propagate(Error **dst_errp, Error *local_err);


/*
 * Propagate error object (if any) with some text prepended.
 * Behaves like
 *     error_prepend(&local_err, fmt, ...);
 *     error_propagate(dst_errp, local_err);
 */
void error_propagate_prepend(Error **dst_errp, Error *local_err,
                             const char *fmt, ...);

/*
 * Prepend some text to @errp's human-readable error message.
 * The text is made by formatting @fmt, @ap like vprintf().
 */
void error_vprepend(Error *const *errp, const char *fmt, va_list ap);

/*
 * Prepend some text to @errp's human-readable error message.
 * The text is made by formatting @fmt, ... like printf().
 */
void error_prepend(Error *const *errp, const char *fmt, ...)
    GCC_FMT_ATTR(2, 3);

/*
 * Append a printf-style human-readable explanation to an existing error.
 * If the error is later reported to a human user with
 * error_report_err() or warn_report_err(), the hints will be shown,
 * too.  If it's reported via QMP, the hints will be ignored.
 * Intended use is adding helpful hints on the human user interface,
 * e.g. a list of valid values.  It's not for clarifying a confusing
 * error message.
 * @errp may be NULL, but not &error_fatal or &error_abort.
 * Trivially the case if you call it only after error_setg() or
 * error_propagate().
 * May be called multiple times.  The resulting hint should end with a
 * newline.
 */
void error_append_hint(Error *const *errp, const char *fmt, ...)
    GCC_FMT_ATTR(2, 3);

/*
 * Convenience function to report open() failure.
 */
#define error_setg_file_open(errp, os_errno, filename)                  \
    error_setg_file_open_internal((errp), __FILE__, __LINE__, __func__, \
                                  (os_errno), (filename))
void error_setg_file_open_internal(Error **errp,
                                   const char *src, int line, const char *func,
                                   int os_errno, const char *filename);

/*
 * Return an exact copy of @err.
 */
Error *error_copy(const Error *err);

/*
 * Free @err.
 * @err may be NULL.
 */
void error_free(Error *err);

/*
 * Convenience function to assert that *@errp is set, then silently free it.
 */
void error_free_or_abort(Error **errp);

/*
 * Convenience function to warn_report() and free @err.
 * The report includes hints added with error_append_hint().
 */
void warn_report_err(Error *err);

/*
 * Convenience function to error_report() and free @err.
 * The report includes hints added with error_append_hint().
 */
void error_report_err(Error *err);

/*
 * Convenience function to error_prepend(), warn_report() and free @err.
 */
void warn_reportf_err(Error *err, const char *fmt, ...)
    GCC_FMT_ATTR(2, 3);

/*
 * Convenience function to error_prepend(), error_report() and free @err.
 */
void error_reportf_err(Error *err, const char *fmt, ...)
    GCC_FMT_ATTR(2, 3);

/*
 * Just like error_setg(), except you get to specify the error class.
 * Note: use of error classes other than ERROR_CLASS_GENERIC_ERROR is
 * strongly discouraged.
 */
#define error_set(errp, err_class, fmt, ...)                    \
    error_set_internal((errp), __FILE__, __LINE__, __func__,    \
                       (err_class), (fmt), ## __VA_ARGS__)
void error_set_internal(Error **errp,
                        const char *src, int line, const char *func,
                        ErrorClass err_class, const char *fmt, ...)
    GCC_FMT_ATTR(6, 7);

typedef struct ErrorPropagator {
    Error *local_err;
    Error **errp;
} ErrorPropagator;

static inline void error_propagator_cleanup(ErrorPropagator *prop)
{
    error_propagate(prop->errp, prop->local_err);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(ErrorPropagator, error_propagator_cleanup);

/*
 * ERRP_AUTO_PROPAGATE
 *
 * This macro exists to assist with proper error handling in a function which
 * uses an Error **errp parameter.  It must be used as the first line of a
 * function which modifies an error (with error_prepend, error_append_hint, or
 * similar) or which wants to dereference *errp.  It is still safe (but
 * useless) to use in other functions.
 *
 * If errp is NULL or points to error_fatal, it is rewritten to point to a
 * local Error object, which will be automatically propagated to the original
 * errp on function exit (see error_propagator_cleanup).
 *
 * After invocation of this macro it is always safe to dereference errp
 * (as it's not NULL anymore) and to add information by error_prepend or
 * error_append_hint (as, if it was error_fatal, we swapped it with a
 * local_error to be propagated on cleanup).
 *
 * Note: we don't wrap the error_abort case, as we want resulting coredump
 * to point to the place where the error happened, not to error_propagate.
 */
#define ERRP_AUTO_PROPAGATE() \
    g_auto(ErrorPropagator) _auto_errp_prop = {.errp = errp}; \
    do { \
        if (!errp || errp == &error_fatal) { \
            errp = &_auto_errp_prop.local_err; \
        } \
    } while (0)

/*
 * Special error destination to abort on error.
 * See error_setg() and error_propagate() for details.
 */
extern Error *error_abort;

/*
 * Special error destination to exit(1) on error.
 * See error_setg() and error_propagate() for details.
 */
extern Error *error_fatal;

#endif
