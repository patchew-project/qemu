/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef QEMU_MESSAGE_H
#define QEMU_MESSAGE_H

enum QMessageFormatFlags {
    QMESSAGE_FORMAT_TIMESTAMP = (1 << 0),
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
 * qmessage_context_print:
 * @fp: file to emit the prefix on
 *
 * Emit a message prefix with the information selected by
 * an earlier call to qmessage_set_format.
 */
void qmessage_context_print(FILE *fp);

#endif /* QEMU_MESSAGE_H */
