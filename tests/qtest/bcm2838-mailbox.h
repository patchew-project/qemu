/*
 * Declarations for BCM2838 mailbox test.
 *
 * Copyright (c) 2023 Auriga LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#define MBOX0_BASE 0xFE00B880
#define MBOX1_BASE 0xFE00B8A0

#define MBOX_REG_READ   0x00
#define MBOX_REG_WRITE  0x00
#define MBOX_REG_PEEK   0x10
#define MBOX_REG_SENDER 0x14
#define MBOX_REG_STATUS 0x18
#define MBOX_REG_CONFIG 0x1C

#define MBOX_READ_EMPTY 0x40000000

#define MBOX_CHANNEL_ID_PROPERTY 8

#define MBOX_PROCESS_REQUEST      0x00000000
#define MBOX_SUCCESS              0x80000000
#define MBOX_ERROR_PARSING_BUFFER 0x80000001

#define BOARD_REVISION    0xB03115
#define FIRMWARE_REVISION 0x548E1
#define FIRMWARE_VARIANT  0x77777777 /* TODO: Find the real value */

#define ARM_MEMORY_BASE 0x00000000
#define ARM_MEMORY_SIZE 0x3c000000
#define VC_MEMORY_BASE  0x3c000000
#define VC_MEMORY_SIZE  0x04000000
#define VC_FB_BASE      0x3c100000
#define VC_FB_SIZE      0x00096000

#define CLOCK_ID_ROOT      0x00000000
#define CLOCK_ID_EMMC      0x00000001
#define CLOCK_ID_UART      0x00000002
#define CLOCK_ID_ARM       0x00000003
#define CLOCK_ID_CORE      0x00000004
#define CLOCK_ID_UNDEFINED 0x12345678

#define CLOCK_RATE_EMMC 50000000
#define CLOCK_RATE_UART 3000000
#define CLOCK_RATE_CORE 350000000
#define CLOCK_RATE_ANY  700000000

#define DEVICE_ID_SD_CARD   0x00000000
#define DEVICE_ID_UART0     0x00000001
#define DEVICE_ID_UART1     0x00000002
#define DEVICE_ID_USB HCD   0x00000003
#define DEVICE_ID_I2C0      0x00000004
#define DEVICE_ID_I2C1      0x00000005
#define DEVICE_ID_I2C2      0x00000006
#define DEVICE_ID_SPI       0x00000007
#define DEVICE_ID_CCP2TX    0x00000008
#define DEVICE_ID_UNKNOWN_0 0x00000009
#define DEVICE_ID_UNKNOWN_1 0x0000000a

#define TEMPERATURE_ID_SOC 0x00000000

#define TEMPERATURE_SOC     25000
#define TEMPERATURE_SOC_MAX 99000

#define ALIGN_4K 4096

#define PIXEL_ORDER_BGR 0
#define PIXEL_ORDER_RGB 1

#define ALPHA_MODE_ENABLED  0
#define ALPHA_MODE_REVERSED 1
#define ALPHA_MODE_IGNORED  2

#define GPIO_MASK 0x003c

#define GPIO_0 0x00000080

#define GPIO_DIRECTION_IN  0
#define GPIO_DIRECTION_OUT 1

#define GPIO_TERMINATION_DISABLED 0
#define GPIO_TERMINATION_ENABLED  1

#define GPIO_TERMINATION_PULLUP_DISABLED 0
#define GPIO_TERMINATION_PULLUP_ENABLED  1

#define GPIO_POLARITY_LOW  0
#define GPIO_POLARITY_HIGH 1

#define GPIO_STATE_DOWN 0

/* Used to test stubs that don't perform actual work */
#define DUMMY_VALUE 0x12345678

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
            RequestValueType value;                                     \
        } request;                                                      \
        struct {                                                        \
            uint32_t size_stat;                                         \
            ResponseValueType value;                                    \
        } response;                                                     \
    };                                                                  \
} TypeName

DECLARE_TAG_TYPE(TAG_GET_FIRMWARE_REVISION_t,
    struct {},
    struct {
        uint32_t revision;
    });

DECLARE_TAG_TYPE(TAG_GET_FIRMWARE_VARIANT_t,
    struct {},
    struct {
        uint32_t variant;
    });

DECLARE_TAG_TYPE(TAG_GET_BOARD_REVISION_t,
    struct {},
    struct {
        uint32_t revision;
    });

DECLARE_TAG_TYPE(TAG_GET_ARM_MEMORY_t,
    struct {},
    struct {
        uint32_t base;
        uint32_t size;
    });

DECLARE_TAG_TYPE(TAG_GET_VC_MEMORY_t,
    struct {},
    struct {
        uint32_t base;
        uint32_t size;
    });

DECLARE_TAG_TYPE(TAG_SET_POWER_STATE_t,
    struct {
        uint32_t device_id;
        uint32_t cmd;
    },
    struct {
        uint32_t device_id;
        uint32_t cmd;
    });

DECLARE_TAG_TYPE(TAG_GET_CLOCK_STATE_t,
    struct {
        uint32_t clock_id;
    },
    struct {
        uint32_t clock_id;
        uint32_t cmd;
    });

DECLARE_TAG_TYPE(TAG_GET_CLOCK_RATE_t,
    struct {
        uint32_t clock_id;
    },
    struct {
        uint32_t clock_id;
        uint32_t rate;
    });

DECLARE_TAG_TYPE(TAG_GET_MAX_CLOCK_RATE_t,
    struct {
        uint32_t clock_id;
    },
    struct {
        uint32_t clock_id;
        uint32_t rate;
    });

DECLARE_TAG_TYPE(TAG_GET_MIN_CLOCK_RATE_t,
    struct {
        uint32_t clock_id;
    },
    struct {
        uint32_t clock_id;
        uint32_t rate;
    });

DECLARE_TAG_TYPE(TAG_GET_CLOCKS_t,
    struct {},
    struct {
        uint32_t root_clock;
        uint32_t arm_clock;
    });

DECLARE_TAG_TYPE(TAG_GET_TEMPERATURE_t,
    struct {
        uint32_t temperature_id;
    },
    struct {
        uint32_t temperature_id;
        uint32_t temperature;
    });

DECLARE_TAG_TYPE(TAG_GET_MAX_TEMPERATURE_t,
    struct {
        uint32_t temperature_id;
    },
    struct {
        uint32_t temperature_id;
        uint32_t temperature;
    });

DECLARE_TAG_TYPE(TAG_FRAMEBUFFER_ALLOCATE_t,
    struct {
        uint32_t alignment;
    },
    struct {
        uint32_t base;
        uint32_t size;
    });

DECLARE_TAG_TYPE(TAG_FRAMEBUFFER_RELEASE_t,
    struct {},
    struct {});

DECLARE_TAG_TYPE(TAG_FRAMEBUFFER_BLANK_t,
    struct {
        uint32_t on;
    },
    struct {
        uint32_t on;
    });

DECLARE_TAG_TYPE(TAG_FRAMEBUFFER_GET_PHYSICAL_WIDTH_HEIGHT_t,
    struct {},
    struct {
        uint32_t width;
        uint32_t height;
    });

DECLARE_TAG_TYPE(TAG_FRAMEBUFFER_TEST_PHYSICAL_WIDTH_HEIGHT_t,
    struct {
        uint32_t width;
        uint32_t height;
    },
    struct {
        uint32_t width;
        uint32_t height;
    });

DECLARE_TAG_TYPE(TAG_FRAMEBUFFER_SET_PHYSICAL_WIDTH_HEIGHT_t,
    struct {
        uint32_t width;
        uint32_t height;
    },
    struct {
        uint32_t width;
        uint32_t height;
    });

DECLARE_TAG_TYPE(TAG_FRAMEBUFFER_GET_VIRTUAL_WIDTH_HEIGHT_t,
    struct {},
    struct {
        uint32_t width;
        uint32_t height;
    });

DECLARE_TAG_TYPE(TAG_FRAMEBUFFER_TEST_VIRTUAL_WIDTH_HEIGHT_t,
    struct {
        uint32_t width;
        uint32_t height;
    },
    struct {
        uint32_t width;
        uint32_t height;
    });

DECLARE_TAG_TYPE(TAG_FRAMEBUFFER_SET_VIRTUAL_WIDTH_HEIGHT_t,
    struct {
        uint32_t width;
        uint32_t height;
    },
    struct {
        uint32_t width;
        uint32_t height;
    });

int mbox0_has_data(void);
void mbox0_read_message(uint8_t channel, void *msgbuf, size_t msgbuf_size);
void mbox1_write_message(uint8_t channel, uint32_t msg_addr);
int qtest_mbox0_has_data(QTestState *s);
void qtest_mbox0_read_message(QTestState *s, uint8_t channel, void *msgbuf, size_t msgbuf_size);
void qtest_mbox1_write_message(QTestState *s, uint8_t channel, uint32_t msg_addr);
