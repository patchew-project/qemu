/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef QEMU_MESSAGE_H
#define QEMU_MESSAGE_H

enum QMessageFormatFlags {
    QMESSAGE_FORMAT_TIMESTAMP = (1 << 0),
    QMESSAGE_FORMAT_WORKLOAD_NAME = (1 << 1),
    QMESSAGE_FORMAT_PROGRAM_NAME = (1 << 2),
    QMESSAGE_FORMAT_THREAD_INFO = (1 << 3),
};

/**
 * qmessage_set_format:
 * @flags: the message information to emit
 *
 * Select which pieces of information to
 * emit for messages
 */
void qmessage_set_format(int flags);

/**
 * qmessage_set_workload_name:
 * @name: the name of the workload
 *
 * Set the workload name, which for a system emulator
 * will be the guest VM name.
 */
void qmessage_set_workload_name(const char *name);

enum QMessageContextFlags {
    QMESSAGE_CONTEXT_SKIP_MONITOR = (1 << 0),
};

/**
 * qmessage_context:
 * @flags: the message formatting control flags
 *
 * Format a message prefix with the information
 * previously selected by a call to
 * qmessage_set_format.
 *
 * If @flags contains QMESSAGE_CONTEXT_SKIP_MONITOR
 * an empty string will be returned if running in
 * the context of a HMP command
 *
 * Returns: a formatted message prefix, or empty string;
 * to be freed by the caller.
 */
char *qmessage_context(int flags);

#endif /* QEMU_MESSAGE_H */
