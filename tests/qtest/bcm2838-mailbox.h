/*
 * Declarations for BCM2838 mailbox test.
 *
 * Copyright (c) 2023 Auriga LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

typedef union {
    uint32_t value;
    struct {
        uint32_t channel: 4;
        uint32_t data   : 28;
    } fields;
} MboxRegWrite;

typedef MboxRegWrite MboxRegRead;

typedef struct {
    uint32_t size;
    uint32_t req_resp_code;
} MboxBufHeader;

#define DECLARE_TAG_TYPE(TypeName, RequestValueType, ResponseValueType) \
typedef struct {                                                        \
    uint32_t id;                                                        \
    uint32_t value_buffer_size;                                         \
    union {                                                             \
        struct {                                                        \
            uint32_t zero;                                              \
            RequestValueType __attribute__((packed)) value;             \
        } request;                                                      \
        struct {                                                        \
            uint32_t size   : 31;                                       \
            uint32_t success: 1;                                        \
            ResponseValueType __attribute__((packed)) value;            \
        } response;                                                     \
    };                                                                  \
} TypeName


int mbox0_has_data(void);
void mbox0_read_message(uint8_t channel, void *msgbuf, size_t msgbuf_size);
void mbox1_write_message(uint8_t channel, uint32_t msg_addr);
int qtest_mbox0_has_data(QTestState *s);
void qtest_mbox0_read_message(QTestState *s, uint8_t channel, void *msgbuf, size_t msgbuf_size);
void qtest_mbox1_write_message(QTestState *s, uint8_t channel, uint32_t msg_addr);
