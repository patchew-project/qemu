/*
 * Test Server
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QTEST_H
#define QTEST_H


extern bool qtest_allowed;

static inline bool qtest_enabled(void)
{
    return qtest_allowed;
}

bool qtest_driver(void);
#ifdef CONFIG_FUZZ
/* Both the client and the server have qtest_init's, Rename on of them... */
void qtest_init_server(const char *qtest_chrdev, const char *qtest_log, Error **errp);
void qtest_server_recv(GString *inbuf); /* Client sends commands using this */
#else
void qtest_init(const char *qtest_chrdev, const char *qtest_log, Error **errp);
#endif

#endif
