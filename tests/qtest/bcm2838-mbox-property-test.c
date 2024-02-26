/*
 * Tests set for BCM2838 mailbox property interface.
 *
 * Copyright (c) 2022 Auriga
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/registerfields.h"
#include "libqtest-single.h"
#include "bcm2838-mailbox.h"
#include "hw/arm/raspberrypi-fw-defs.h"

REG32(MBOX_SIZE_STAT,          0)
FIELD(MBOX_SIZE_STAT, SIZE,    0, 31)
FIELD(MBOX_SIZE_STAT, SUCCESS, 31, 1)

REG32(SET_POWER_STATE_CMD,        0)
FIELD(SET_POWER_STATE_CMD, EN,    0, 1)
FIELD(SET_POWER_STATE_CMD, WAIT,  1, 1)

REG32(GET_CLOCK_STATE_CMD,        0)
FIELD(GET_CLOCK_STATE_CMD, EN,    0, 1)
FIELD(GET_CLOCK_STATE_CMD, NPRES, 1, 1)

#define MBOX_TEST_MESSAGE_ADDRESS 0x10000000

#define TEST_TAG(x) RPI_FWREQ_ ## x
#define TEST_TAG_TYPE(x) TAG_##x##_t

#define TEST_FN_NAME(test, subtest) \
    test ## _ ## subtest ## _test

#define SETUP_FN_NAME(test, subtest) \
    test ## _ ## subtest ## _setup

#define CHECK_FN_NAME(test, subtest) \
    test ## _ ## subtest ## _spec_check

#define DECLARE_TEST_CASE_SETUP(testname, ...)              \
    void SETUP_FN_NAME(testname, __VA_ARGS__)               \
                             (TEST_TAG_TYPE(testname) * tag)

/*----------------------------------------------------------------------------*/
#define DECLARE_TEST_CASE(testname, ...)                                       \
    __attribute__((weak))                                                      \
    void SETUP_FN_NAME(testname, __VA_ARGS__)                                  \
                      (TEST_TAG_TYPE(testname) * tag);                         \
    static void CHECK_FN_NAME(testname, __VA_ARGS__)                           \
                             (TEST_TAG_TYPE(testname) *tag);                   \
    static void TEST_FN_NAME(testname, __VA_ARGS__)(void)                      \
    {                                                                          \
        struct {                                                               \
            MboxBufHeader header;                                              \
            TEST_TAG_TYPE(testname) tag;                                       \
            uint32_t end_tag;                                                  \
        } mailbox_buffer = { 0 };                                              \
                                                                               \
        QTestState *qts = qtest_init("-machine raspi4b-2g");                   \
                                                                               \
        mailbox_buffer.header.size = sizeof(mailbox_buffer);                   \
        mailbox_buffer.header.req_resp_code = MBOX_PROCESS_REQUEST;            \
                                                                               \
        mailbox_buffer.tag.id = TEST_TAG(testname);                            \
        mailbox_buffer.tag.value_buffer_size = MAX(                            \
            sizeof(mailbox_buffer.tag.request.value),                          \
            sizeof(mailbox_buffer.tag.response.value));                        \
        mailbox_buffer.tag.request.zero = 0;                                   \
                                                                               \
        mailbox_buffer.end_tag = RPI_FWREQ_PROPERTY_END;                       \
                                                                               \
        if (SETUP_FN_NAME(testname, __VA_ARGS__)) {                            \
            SETUP_FN_NAME(testname, __VA_ARGS__)(&mailbox_buffer.tag);         \
        }                                                                      \
                                                                               \
        qtest_memwrite(qts, MBOX_TEST_MESSAGE_ADDRESS,                         \
                    &mailbox_buffer, sizeof(mailbox_buffer));                  \
        qtest_mbox1_write_message(qts, MBOX_CHANNEL_ID_PROPERTY,               \
                            MBOX_TEST_MESSAGE_ADDRESS);                        \
                                                                               \
        qtest_mbox0_read_message(qts, MBOX_CHANNEL_ID_PROPERTY,                \
                            &mailbox_buffer, sizeof(mailbox_buffer));          \
                                                                               \
        g_assert_cmphex(mailbox_buffer.header.req_resp_code, ==, MBOX_SUCCESS);\
                                                                               \
        g_assert_cmphex(mailbox_buffer.tag.id, ==, TEST_TAG(testname));        \
                                                                               \
        uint32_t size = FIELD_EX32(mailbox_buffer.tag.response.size_stat,      \
                                   MBOX_SIZE_STAT, SIZE);                      \
        uint32_t success = FIELD_EX32(mailbox_buffer.tag.response.size_stat,   \
                                      MBOX_SIZE_STAT, SUCCESS);                \
        g_assert_cmpint(size, ==, sizeof(mailbox_buffer.tag.response.value));  \
        g_assert_cmpint(success, ==, 1);                                       \
                                                                               \
        CHECK_FN_NAME(testname, __VA_ARGS__)(&mailbox_buffer.tag);             \
                                                                               \
        qtest_quit(qts);                                                       \
    }                                                                          \
    static void CHECK_FN_NAME(testname, __VA_ARGS__)                           \
                             (TEST_TAG_TYPE(testname) * tag)

/*----------------------------------------------------------------------------*/

#define QTEST_ADD_TEST_CASE(testname, ...)                                     \
    qtest_add_func(stringify(/bcm2838/mbox/property/                           \
                   TEST_FN_NAME(testname, __VA_ARGS__)-test),                  \
                   TEST_FN_NAME(testname, __VA_ARGS__))

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(GET_FIRMWARE_REVISION) {
    g_assert_cmpint(tag->response.value.revision, ==, FIRMWARE_REVISION);
}

// /*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(GET_BOARD_REVISION) {
    g_assert_cmpint(tag->response.value.revision, ==, BOARD_REVISION);
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(GET_ARM_MEMORY) {
    g_assert_cmphex(tag->response.value.base, ==, ARM_MEMORY_BASE);
    g_assert_cmphex(tag->response.value.size, ==, ARM_MEMORY_SIZE);
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(GET_VC_MEMORY) {
    g_assert_cmphex(tag->response.value.base, ==, VC_MEMORY_BASE);
    g_assert_cmphex(tag->response.value.size, ==, VC_MEMORY_SIZE);
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(SET_POWER_STATE) {
    uint32_t enabled =
        FIELD_EX32(tag->response.value.cmd, SET_POWER_STATE_CMD, EN);
    uint32_t wait =
        FIELD_EX32(tag->response.value.cmd, SET_POWER_STATE_CMD, WAIT);
    g_assert_cmphex(tag->response.value.device_id, ==, DEVICE_ID_UART0);
    g_assert_cmpint(enabled, ==, 1);
    g_assert_cmpint(wait, ==, 0);
}
DECLARE_TEST_CASE_SETUP(SET_POWER_STATE) {
    tag->request.value.device_id = DEVICE_ID_UART0;
    tag->response.value.cmd =
        FIELD_DP32(tag->response.value.cmd, SET_POWER_STATE_CMD, EN, 1);
    tag->response.value.cmd =
        FIELD_DP32(tag->response.value.cmd, SET_POWER_STATE_CMD, WAIT, 1);
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(GET_CLOCK_STATE) {
    uint32_t enabled =
        FIELD_EX32(tag->response.value.cmd, GET_CLOCK_STATE_CMD, EN);
    uint32_t not_present =
        FIELD_EX32(tag->response.value.cmd, GET_CLOCK_STATE_CMD, NPRES);
    g_assert_cmphex(tag->response.value.clock_id, ==, CLOCK_ID_CORE);
    g_assert_cmphex(enabled, ==, 1);
    g_assert_cmphex(not_present, ==, 0);
}
DECLARE_TEST_CASE_SETUP(GET_CLOCK_STATE) {
   tag->request.value.clock_id = CLOCK_ID_CORE;
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(GET_CLOCK_RATE, EMMC) {
    g_assert_cmphex(tag->response.value.clock_id, ==, CLOCK_ID_EMMC);
    g_assert_cmphex(tag->response.value.rate, ==, CLOCK_RATE_EMMC);
}
DECLARE_TEST_CASE_SETUP(GET_CLOCK_RATE, EMMC) {
    tag->request.value.clock_id = CLOCK_ID_EMMC;
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(GET_MAX_CLOCK_RATE, EMMC) {
    g_assert_cmphex(tag->response.value.clock_id, ==, CLOCK_ID_EMMC);
    g_assert_cmphex(tag->response.value.rate, ==, CLOCK_RATE_EMMC);
}
DECLARE_TEST_CASE_SETUP(GET_MAX_CLOCK_RATE, EMMC) {
    tag->request.value.clock_id = CLOCK_ID_EMMC;
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(GET_MIN_CLOCK_RATE, EMMC) {
    g_assert_cmphex(tag->response.value.clock_id, ==, CLOCK_ID_EMMC);
    g_assert_cmphex(tag->response.value.rate, ==, CLOCK_RATE_EMMC);
}
DECLARE_TEST_CASE_SETUP(GET_MIN_CLOCK_RATE, EMMC) {
    tag->request.value.clock_id = CLOCK_ID_EMMC;
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(GET_CLOCK_RATE, UART) {
    g_assert_cmphex(tag->response.value.clock_id, ==, CLOCK_ID_UART);
    g_assert_cmphex(tag->response.value.rate, ==, CLOCK_RATE_UART);
}
DECLARE_TEST_CASE_SETUP(GET_CLOCK_RATE, UART) {
    tag->request.value.clock_id = CLOCK_ID_UART;
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(GET_MAX_CLOCK_RATE, UART) {
    g_assert_cmphex(tag->response.value.clock_id, ==, CLOCK_ID_UART);
    g_assert_cmphex(tag->response.value.rate, ==, CLOCK_RATE_UART);
}
DECLARE_TEST_CASE_SETUP(GET_MAX_CLOCK_RATE, UART) {
    tag->request.value.clock_id = CLOCK_ID_UART;
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(GET_MIN_CLOCK_RATE, UART) {
    g_assert_cmphex(tag->response.value.clock_id, ==, CLOCK_ID_UART);
    g_assert_cmphex(tag->response.value.rate, ==, CLOCK_RATE_UART);
}
DECLARE_TEST_CASE_SETUP(GET_MIN_CLOCK_RATE, UART) {
    tag->request.value.clock_id = CLOCK_ID_UART;
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(GET_CLOCK_RATE, CORE) {
    g_assert_cmphex(tag->response.value.clock_id, ==, CLOCK_ID_CORE);
    g_assert_cmphex(tag->response.value.rate, ==, CLOCK_RATE_CORE);
}
DECLARE_TEST_CASE_SETUP(GET_CLOCK_RATE, CORE) {
    tag->request.value.clock_id = CLOCK_ID_CORE;
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(GET_MAX_CLOCK_RATE, CORE) {
    g_assert_cmphex(tag->response.value.clock_id, ==, CLOCK_ID_CORE);
    g_assert_cmphex(tag->response.value.rate, ==, CLOCK_RATE_CORE);
}
DECLARE_TEST_CASE_SETUP(GET_MAX_CLOCK_RATE, CORE) {
    tag->request.value.clock_id = CLOCK_ID_CORE;
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(GET_MIN_CLOCK_RATE, CORE) {
    g_assert_cmphex(tag->response.value.clock_id, ==, CLOCK_ID_CORE);
    g_assert_cmphex(tag->response.value.rate, ==, CLOCK_RATE_CORE);
}
DECLARE_TEST_CASE_SETUP(GET_MIN_CLOCK_RATE, CORE) {
    tag->request.value.clock_id = CLOCK_ID_CORE;
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(GET_CLOCK_RATE, ANY) {
    g_assert_cmphex(tag->response.value.clock_id, ==, CLOCK_ID_UNDEFINED);
    g_assert_cmphex(tag->response.value.rate, ==, CLOCK_RATE_ANY);
}
DECLARE_TEST_CASE_SETUP(GET_CLOCK_RATE, ANY) {
    tag->request.value.clock_id = CLOCK_ID_UNDEFINED;
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(GET_MAX_CLOCK_RATE, ANY) {
    g_assert_cmphex(tag->response.value.clock_id, ==, CLOCK_ID_UNDEFINED);
    g_assert_cmphex(tag->response.value.rate, ==, CLOCK_RATE_ANY);
}
DECLARE_TEST_CASE_SETUP(GET_MAX_CLOCK_RATE, ANY) {
    tag->request.value.clock_id = CLOCK_ID_UNDEFINED;
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(GET_MIN_CLOCK_RATE, ANY) {
    g_assert_cmphex(tag->response.value.clock_id, ==, CLOCK_ID_UNDEFINED);
    g_assert_cmphex(tag->response.value.rate, ==, CLOCK_RATE_ANY);
}
DECLARE_TEST_CASE_SETUP(GET_MIN_CLOCK_RATE, ANY) {
    tag->request.value.clock_id = CLOCK_ID_UNDEFINED;
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(GET_TEMPERATURE) {
    g_assert_cmphex(tag->response.value.temperature_id, ==, TEMPERATURE_ID_SOC);
    g_assert_cmpint(tag->response.value.temperature, ==, TEMPERATURE_SOC);
}
DECLARE_TEST_CASE_SETUP(GET_TEMPERATURE) {
    tag->request.value.temperature_id = TEMPERATURE_ID_SOC;
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(GET_MAX_TEMPERATURE) {
    g_assert_cmphex(tag->response.value.temperature_id, ==, TEMPERATURE_ID_SOC);
    g_assert_cmpint(tag->response.value.temperature, ==, TEMPERATURE_SOC_MAX);
}
DECLARE_TEST_CASE_SETUP(GET_MAX_TEMPERATURE) {
    tag->request.value.temperature_id = TEMPERATURE_ID_SOC;
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(FRAMEBUFFER_ALLOCATE) {
    g_assert_cmphex(tag->response.value.base, ==, VC_FB_BASE);
    g_assert_cmphex(tag->response.value.size, ==, VC_FB_SIZE);
}
DECLARE_TEST_CASE_SETUP(FRAMEBUFFER_ALLOCATE) {
    tag->request.value.alignment = ALIGN_4K;
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(FRAMEBUFFER_RELEASE) {
    /* No special checks are needed for this test */
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(FRAMEBUFFER_BLANK) {
    g_assert_cmphex(tag->response.value.on, ==, 0);
}
DECLARE_TEST_CASE_SETUP(FRAMEBUFFER_BLANK) {
    tag->request.value.on = 0;
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(FRAMEBUFFER_TEST_PHYSICAL_WIDTH_HEIGHT) {
    g_assert_cmpint(tag->response.value.width, ==, DUMMY_VALUE);
    g_assert_cmpint(tag->response.value.height, ==, DUMMY_VALUE);
}
DECLARE_TEST_CASE_SETUP(FRAMEBUFFER_TEST_PHYSICAL_WIDTH_HEIGHT) {
    tag->request.value.width = DUMMY_VALUE;
    tag->request.value.height = DUMMY_VALUE;
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(FRAMEBUFFER_GET_PHYSICAL_WIDTH_HEIGHT, INITIAL) {
    g_assert_cmpint(tag->response.value.width, ==, 640);
    g_assert_cmpint(tag->response.value.height, ==, 480);
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(FRAMEBUFFER_SET_PHYSICAL_WIDTH_HEIGHT) {
    g_assert_cmpint(tag->response.value.width, ==, 800);
    g_assert_cmpint(tag->response.value.height, ==, 600);
}
DECLARE_TEST_CASE_SETUP(FRAMEBUFFER_SET_PHYSICAL_WIDTH_HEIGHT) {
    tag->request.value.width = 800;
    tag->request.value.height = 600;
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(FRAMEBUFFER_TEST_VIRTUAL_WIDTH_HEIGHT) {
    g_assert_cmpint(tag->response.value.width, ==, DUMMY_VALUE);
    g_assert_cmpint(tag->response.value.height, ==, DUMMY_VALUE);
}
DECLARE_TEST_CASE_SETUP(FRAMEBUFFER_TEST_VIRTUAL_WIDTH_HEIGHT) {
    tag->request.value.width = DUMMY_VALUE;
    tag->request.value.height = DUMMY_VALUE;
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(FRAMEBUFFER_GET_VIRTUAL_WIDTH_HEIGHT, INITIAL) {
    g_assert_cmpint(tag->response.value.width, ==, 640);
    g_assert_cmpint(tag->response.value.height, ==, 480);
}

/*----------------------------------------------------------------------------*/
DECLARE_TEST_CASE(FRAMEBUFFER_SET_VIRTUAL_WIDTH_HEIGHT) {
    g_assert_cmpint(tag->response.value.width, ==, 800);
    g_assert_cmpint(tag->response.value.height, ==, 600);
}
DECLARE_TEST_CASE_SETUP(FRAMEBUFFER_SET_VIRTUAL_WIDTH_HEIGHT) {
    tag->request.value.width = 800;
    tag->request.value.height = 600;
}

//==============================================================================
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    QTEST_ADD_TEST_CASE(GET_FIRMWARE_REVISION);
    QTEST_ADD_TEST_CASE(GET_BOARD_REVISION);
    QTEST_ADD_TEST_CASE(GET_ARM_MEMORY);
    QTEST_ADD_TEST_CASE(GET_VC_MEMORY);
    QTEST_ADD_TEST_CASE(SET_POWER_STATE);
    QTEST_ADD_TEST_CASE(GET_CLOCK_STATE);
    QTEST_ADD_TEST_CASE(GET_CLOCK_RATE, EMMC);
    QTEST_ADD_TEST_CASE(GET_MAX_CLOCK_RATE, EMMC);
    QTEST_ADD_TEST_CASE(GET_MIN_CLOCK_RATE, EMMC);
    QTEST_ADD_TEST_CASE(GET_CLOCK_RATE, UART);
    QTEST_ADD_TEST_CASE(GET_MAX_CLOCK_RATE, UART);
    QTEST_ADD_TEST_CASE(GET_MIN_CLOCK_RATE, UART);
    QTEST_ADD_TEST_CASE(GET_CLOCK_RATE, CORE);
    QTEST_ADD_TEST_CASE(GET_MAX_CLOCK_RATE, CORE);
    QTEST_ADD_TEST_CASE(GET_MIN_CLOCK_RATE, CORE);
    QTEST_ADD_TEST_CASE(GET_CLOCK_RATE, ANY);
    QTEST_ADD_TEST_CASE(GET_MAX_CLOCK_RATE, ANY);
    QTEST_ADD_TEST_CASE(GET_MIN_CLOCK_RATE, ANY);
    QTEST_ADD_TEST_CASE(GET_TEMPERATURE);
    QTEST_ADD_TEST_CASE(GET_MAX_TEMPERATURE);
    QTEST_ADD_TEST_CASE(FRAMEBUFFER_ALLOCATE);
    QTEST_ADD_TEST_CASE(FRAMEBUFFER_RELEASE);
    QTEST_ADD_TEST_CASE(FRAMEBUFFER_BLANK);
    QTEST_ADD_TEST_CASE(FRAMEBUFFER_TEST_PHYSICAL_WIDTH_HEIGHT);
    QTEST_ADD_TEST_CASE(FRAMEBUFFER_GET_PHYSICAL_WIDTH_HEIGHT, INITIAL);
    QTEST_ADD_TEST_CASE(FRAMEBUFFER_SET_PHYSICAL_WIDTH_HEIGHT);
    QTEST_ADD_TEST_CASE(FRAMEBUFFER_TEST_VIRTUAL_WIDTH_HEIGHT);
    QTEST_ADD_TEST_CASE(FRAMEBUFFER_GET_VIRTUAL_WIDTH_HEIGHT, INITIAL);
    QTEST_ADD_TEST_CASE(FRAMEBUFFER_SET_VIRTUAL_WIDTH_HEIGHT);

    return g_test_run();
}
