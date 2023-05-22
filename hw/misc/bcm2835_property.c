/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/misc/bcm2835_property.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/misc/bcm2835_mbox_defs.h"
#include "sysemu/dma.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"
#include "hw/arm/raspi_platform.h"
#include "hw/misc/raspberrypi-fw-defs.h"

#define RPI_EXP_GPIO_BASE       128
#define VC4_GPIO_EXPANDER_COUNT 8

struct vc4_display_settings_t {
    uint32_t display_num;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint16_t pitch;
    uint32_t virtual_width;
    uint32_t virtual_height;
    uint16_t virtual_width_offset;
    uint32_t virtual_height_offset;
    unsigned long fb_bus_address;
} QEMU_PACKED;

enum rpi_firmware_clk_id {
    RPI_FIRMWARE_EMMC_CLK_ID = 1,
    RPI_FIRMWARE_UART_CLK_ID,
    RPI_FIRMWARE_ARM_CLK_ID,
    RPI_FIRMWARE_CORE_CLK_ID,
    RPI_FIRMWARE_V3D_CLK_ID,
    RPI_FIRMWARE_H264_CLK_ID,
    RPI_FIRMWARE_ISP_CLK_ID,
    RPI_FIRMWARE_SDRAM_CLK_ID,
    RPI_FIRMWARE_PIXEL_CLK_ID,
    RPI_FIRMWARE_PWM_CLK_ID,
    RPI_FIRMWARE_HEVC_CLK_ID,
    RPI_FIRMWARE_EMMC2_CLK_ID,
    RPI_FIRMWARE_M2MC_CLK_ID,
    RPI_FIRMWARE_PIXEL_BVB_CLK_ID,
    RPI_FIRMWARE_VEC_CLK_ID,
    RPI_FIRMWARE_NUM_CLK_ID,
};

struct vc4_gpio_expander_t {
    uint32_t direction;
    uint32_t polarity;
    uint32_t term_en;
    uint32_t term_pull_up;
    uint32_t state;
} vc4_gpio_expander[VC4_GPIO_EXPANDER_COUNT];


/* https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface */

static void bcm2835_property_mbox_push(BCM2835PropertyState *s, uint32_t value)
{
    uint32_t tag;
    uint32_t bufsize;
    uint32_t tot_len;
    size_t resplen;
    uint32_t tmp;
    int n;
    uint32_t offset, length, color;
    uint32_t gpio_num;

    /*
     * Copy the current state of the framebuffer config; we will update
     * this copy as we process tags and then ask the framebuffer to use
     * it at the end.
     */
    BCM2835FBConfig fbconfig = s->fbdev->config;
    bool fbconfig_updated = false;

    value &= ~0xf;

    s->addr = value;

    tot_len = ldl_le_phys(&s->dma_as, value);

    /* @(addr + 4) : Buffer response code */
    value = s->addr + 8;
    while (value + 8 <= s->addr + tot_len) {
        tag = ldl_le_phys(&s->dma_as, value);
        bufsize = ldl_le_phys(&s->dma_as, value + 4);
        /* @(value + 8) : Request/response indicator */
        resplen = 0;
        switch (tag) {
        case RPI_FWREQ_PROPERTY_END: /* End tag */
            break;
        case RPI_FWREQ_GET_FIRMWARE_REVISION: /* Get firmware revision */
            stl_le_phys(&s->dma_as, value + 12, 346337);
            resplen = 4;
            break;
        case RPI_FWREQ_GET_BOARD_MODEL: /* Get board model */
            qemu_log_mask(LOG_UNIMP,
                          "bcm2835_property: 0x%08x get board model NYI\n",
                          tag);
            resplen = 4;
            break;
        case RPI_FWREQ_GET_BOARD_REVISION: /* Get board revision */
            stl_le_phys(&s->dma_as, value + 12, s->board_rev);
            resplen = 4;
            break;
        case RPI_FWREQ_GET_BOARD_MAC_ADDRESS: /* Get board MAC address */
            resplen = sizeof(s->macaddr.a);
            dma_memory_write(&s->dma_as, value + 12, s->macaddr.a, resplen,
                             MEMTXATTRS_UNSPECIFIED);
            break;
        case RPI_FWREQ_GET_BOARD_SERIAL: /* Get board serial */
            qemu_log_mask(LOG_UNIMP,
                          "bcm2835_property: 0x%08x get board serial NYI\n",
                          tag);
            resplen = 8;
            break;
        case RPI_FWREQ_GET_ARM_MEMORY: /* Get ARM memory */
            /* base */
            stl_le_phys(&s->dma_as, value + 12, 0);
            /* size */
            stl_le_phys(&s->dma_as, value + 16, s->fbdev->vcram_base);
            resplen = 8;
            break;
        case RPI_FWREQ_GET_VC_MEMORY: /* Get VC memory */
            /* base */
            stl_le_phys(&s->dma_as, value + 12, s->fbdev->vcram_base);
            /* size */
            stl_le_phys(&s->dma_as, value + 16, s->fbdev->vcram_size);
            resplen = 8;
            break;
        case RPI_FWREQ_SET_POWER_STATE: /* Set power state */
            /* Assume that whatever device they asked for exists,
             * and we'll just claim we set it to the desired state
             */
            tmp = ldl_le_phys(&s->dma_as, value + 16);
            stl_le_phys(&s->dma_as, value + 16, (tmp & 1));
            resplen = 8;
            break;

        /* Clocks */

        case RPI_FWREQ_GET_CLOCK_STATE: /* Get clock state */
            stl_le_phys(&s->dma_as, value + 16, 0x1);
            resplen = 8;
            break;

        case RPI_FWREQ_SET_CLOCK_STATE: /* Set clock state */
            qemu_log_mask(LOG_UNIMP,
                          "bcm2835_property: 0x%08x set clock state NYI\n",
                          tag);
            resplen = 8;
            break;

        case RPI_FWREQ_GET_CLOCK_RATE: /* Get clock rate */
        case RPI_FWREQ_GET_MAX_CLOCK_RATE: /* Get max clock rate */
        case RPI_FWREQ_GET_MIN_CLOCK_RATE: /* Get min clock rate */
            switch (ldl_le_phys(&s->dma_as, value + 12)) {
            case RPI_FIRMWARE_EMMC_CLK_ID: /* EMMC */
                stl_le_phys(&s->dma_as, value + 16, RPI_FIRMWARE_EMMC_CLK_RATE);
                break;
            case RPI_FIRMWARE_UART_CLK_ID: /* UART */
                stl_le_phys(&s->dma_as, value + 16, RPI_FIRMWARE_UART_CLK_RATE);
                break;
            case RPI_FIRMWARE_CORE_CLK_ID: /* Core Clock */
                stl_le_phys(&s->dma_as, value + 16, RPI_FIRMWARE_CORE_CLK_RATE);
                break;
            default:
                stl_le_phys(&s->dma_as, value + 16, RPI_FIRMWARE_DEFAULT_CLK_RATE);
                break;
            }
            resplen = 8;
            break;

        case RPI_FWREQ_GET_CLOCKS: /* Get clocks */
            /* TODO: add more clock IDs if needed */
            stl_le_phys(&s->dma_as, value + 12, 0);
            stl_le_phys(&s->dma_as, value + 16, RPI_FIRMWARE_ARM_CLK_ID);
            resplen = 8;
            break;



        case RPI_FWREQ_SET_CLOCK_RATE: /* Set clock rate */
        case RPI_FWREQ_SET_MAX_CLOCK_RATE: /* Set max clock rate */
        case RPI_FWREQ_SET_MIN_CLOCK_RATE: /* Set min clock rate */
            qemu_log_mask(LOG_UNIMP,
                          "bcm2835_property: 0x%08x set clock rate NYI\n",
                          tag);
            resplen = 8;
            break;

        /* Temperature */

        case RPI_FWREQ_GET_TEMPERATURE: /* Get temperature */
            stl_le_phys(&s->dma_as, value + 16, 25000);
            resplen = 8;
            break;

        case RPI_FWREQ_GET_MAX_TEMPERATURE: /* Get max temperature */
            stl_le_phys(&s->dma_as, value + 16, 99000);
            resplen = 8;
            break;

        /* Frame buffer */

        case RPI_FWREQ_FRAMEBUFFER_ALLOCATE: /* Allocate buffer */
            stl_le_phys(&s->dma_as, value + 12, fbconfig.base);
            stl_le_phys(&s->dma_as, value + 16,
                        bcm2835_fb_get_size(&fbconfig));
            resplen = 8;
            break;
        case RPI_FWREQ_FRAMEBUFFER_RELEASE: /* Release buffer */
            resplen = 0;
            break;
        case RPI_FWREQ_FRAMEBUFFER_BLANK: /* Blank screen */
            resplen = 4;
            break;
        /* Test physical display width/height */
        case RPI_FWREQ_FRAMEBUFFER_TEST_PHYSICAL_WIDTH_HEIGHT:
        /* Test virtual display width/height */
        case RPI_FWREQ_FRAMEBUFFER_TEST_VIRTUAL_WIDTH_HEIGHT:
            resplen = 8;
            break;
        /* Set physical display width/height */
        case RPI_FWREQ_FRAMEBUFFER_SET_PHYSICAL_WIDTH_HEIGHT:
            fbconfig.xres = ldl_le_phys(&s->dma_as, value + 12);
            fbconfig.yres = ldl_le_phys(&s->dma_as, value + 16);
            bcm2835_fb_validate_config(&fbconfig);
            fbconfig_updated = true;
            /* fall through */
        /* Get physical display width/height */
        case RPI_FWREQ_FRAMEBUFFER_GET_PHYSICAL_WIDTH_HEIGHT:
            stl_le_phys(&s->dma_as, value + 12, fbconfig.xres);
            stl_le_phys(&s->dma_as, value + 16, fbconfig.yres);
            resplen = 8;
            break;
        /* Set virtual display width/height */
        case RPI_FWREQ_FRAMEBUFFER_SET_VIRTUAL_WIDTH_HEIGHT:
            fbconfig.xres_virtual = ldl_le_phys(&s->dma_as, value + 12);
            fbconfig.yres_virtual = ldl_le_phys(&s->dma_as, value + 16);
            bcm2835_fb_validate_config(&fbconfig);
            fbconfig_updated = true;
            /* fall through */
        /* Get virtual display width/height */
        case RPI_FWREQ_FRAMEBUFFER_GET_VIRTUAL_WIDTH_HEIGHT:
            stl_le_phys(&s->dma_as, value + 12, fbconfig.xres_virtual);
            stl_le_phys(&s->dma_as, value + 16, fbconfig.yres_virtual);
            resplen = 8;
            break;
        case RPI_FWREQ_FRAMEBUFFER_TEST_DEPTH: /* Test depth */
            resplen = 4;
            break;
        case RPI_FWREQ_FRAMEBUFFER_SET_DEPTH: /* Set depth */
            fbconfig.bpp = ldl_le_phys(&s->dma_as, value + 12);
            bcm2835_fb_validate_config(&fbconfig);
            fbconfig_updated = true;
            /* fall through */
        case RPI_FWREQ_FRAMEBUFFER_GET_DEPTH: /* Get depth */
            stl_le_phys(&s->dma_as, value + 12, fbconfig.bpp);
            resplen = 4;
            break;
        case RPI_FWREQ_FRAMEBUFFER_TEST_PIXEL_ORDER: /* Test pixel order */
            resplen = 4;
            break;
        case RPI_FWREQ_FRAMEBUFFER_SET_PIXEL_ORDER: /* Set pixel order */
            fbconfig.pixo = ldl_le_phys(&s->dma_as, value + 12);
            bcm2835_fb_validate_config(&fbconfig);
            fbconfig_updated = true;
            /* fall through */
        case RPI_FWREQ_FRAMEBUFFER_GET_PIXEL_ORDER: /* Get pixel order */
            stl_le_phys(&s->dma_as, value + 12, fbconfig.pixo);
            resplen = 4;
            break;
        case RPI_FWREQ_FRAMEBUFFER_TEST_ALPHA_MODE: /* Test pixel alpha */
            resplen = 4;
            break;
        case RPI_FWREQ_FRAMEBUFFER_SET_ALPHA_MODE: /* Set alpha */
            fbconfig.alpha = ldl_le_phys(&s->dma_as, value + 12);
            bcm2835_fb_validate_config(&fbconfig);
            fbconfig_updated = true;
            /* fall through */
        case RPI_FWREQ_FRAMEBUFFER_GET_ALPHA_MODE: /* Get alpha */
            stl_le_phys(&s->dma_as, value + 12, fbconfig.alpha);
            resplen = 4;
            break;
        case RPI_FWREQ_FRAMEBUFFER_GET_PITCH: /* Get pitch */
            stl_le_phys(&s->dma_as, value + 12,
                        bcm2835_fb_get_pitch(&fbconfig));
            resplen = 4;
            break;
        /* Test virtual offset */
        case RPI_FWREQ_FRAMEBUFFER_TEST_VIRTUAL_OFFSET:
            resplen = 8;
            break;
        case RPI_FWREQ_FRAMEBUFFER_SET_VIRTUAL_OFFSET: /* Set virtual offset */
            fbconfig.xoffset = ldl_le_phys(&s->dma_as, value + 12);
            fbconfig.yoffset = ldl_le_phys(&s->dma_as, value + 16);
            bcm2835_fb_validate_config(&fbconfig);
            fbconfig_updated = true;
            /* fall through */
        case RPI_FWREQ_FRAMEBUFFER_GET_VIRTUAL_OFFSET: /* Get virtual offset */
            stl_le_phys(&s->dma_as, value + 12, fbconfig.xoffset);
            stl_le_phys(&s->dma_as, value + 16, fbconfig.yoffset);
            resplen = 8;
            break;
        case RPI_FWREQ_FRAMEBUFFER_GET_OVERSCAN: /* Get/Test/Set overscan */
        case RPI_FWREQ_FRAMEBUFFER_TEST_OVERSCAN:
        case RPI_FWREQ_FRAMEBUFFER_SET_OVERSCAN:
            stl_le_phys(&s->dma_as, value + 12, 0);
            stl_le_phys(&s->dma_as, value + 16, 0);
            stl_le_phys(&s->dma_as, value + 20, 0);
            stl_le_phys(&s->dma_as, value + 24, 0);
            resplen = 16;
            break;
        case RPI_FWREQ_FRAMEBUFFER_SET_PALETTE: /* Set palette */
            offset = ldl_le_phys(&s->dma_as, value + 12);
            length = ldl_le_phys(&s->dma_as, value + 16);
            n = 0;
            while (n < length - offset) {
                color = ldl_le_phys(&s->dma_as, value + 20 + (n << 2));
                stl_le_phys(&s->dma_as,
                            s->fbdev->vcram_base + ((offset + n) << 2), color);
                n++;
            }
            stl_le_phys(&s->dma_as, value + 12, 0);
            resplen = 4;
            break;

        case RPI_FWREQ_GET_DMA_CHANNELS: /* Get DMA channels */
            /* channels 2-5 */
            stl_le_phys(&s->dma_as, value + 12, 0x003C);
            resplen = 4;
            break;

        case RPI_FWREQ_GET_COMMAND_LINE: /* Get command line */
            /*
             * We follow the firmware behaviour: no NUL terminator is
             * written to the buffer, and if the buffer is too short
             * we report the required length in the response header
             * and copy nothing to the buffer.
             */
            resplen = strlen(s->command_line);
            if (bufsize >= resplen)
                address_space_write(&s->dma_as, value + 12,
                                    MEMTXATTRS_UNSPECIFIED, s->command_line,
                                    resplen);
            break;

        case RPI_FWREQ_GET_THROTTLED: /* Get throttled */
            stl_le_phys(&s->dma_as, value + 12, 0);
            resplen = 4;
            break;

        /* Get number of displays */
        case RPI_FWREQ_FRAMEBUFFER_GET_NUM_DISPLAYS:
            stl_le_phys(&s->dma_as, value + 12, 1);
            resplen = 4;
            break;

        /* Get display settings*/
        case RPI_FWREQ_FRAMEBUFFER_GET_DISPLAY_SETTINGS:
            stl_le_phys(&s->dma_as, value + 12, 0); /* display_num */
            stl_le_phys(&s->dma_as, value + 16, 800); /* width */
            stl_le_phys(&s->dma_as, value + 20, 600); /* height */
            stl_le_phys(&s->dma_as, value + 24, 32); /* depth */
            stl_le_phys(&s->dma_as, value + 28, 32); /* pitch */
            stl_le_phys(&s->dma_as, value + 30, 0); /* virtual_width */
            stl_le_phys(&s->dma_as, value + 34, 0); /* virtual_height */
            stl_le_phys(&s->dma_as, value + 38, 0); /* virtual_width_offset */
            stl_le_phys(&s->dma_as, value + 40, 0); /* virtual_height_offset */
            stl_le_phys(&s->dma_as, value + 44, 0); /* fb_bus_address low */
            stl_le_phys(&s->dma_as, value + 48, 0); /* fb_bus_address hi */
            resplen = sizeof(struct vc4_display_settings_t);
            break;

        case RPI_FWREQ_FRAMEBUFFER_SET_PITCH: /* Set Pitch */
            resplen = 0;
            break;

        case RPI_FWREQ_GET_GPIO_CONFIG:
            if (ldl_le_phys(&s->dma_as, value + 12) < RPI_EXP_GPIO_BASE) {
                qemu_log_mask(LOG_UNIMP, "RPI_FWREQ_GET_GPIO_CONFIG "
                              "not implemented for gpiochip0\n");
            } else {
                gpio_num = ldl_le_phys(&s->dma_as, value + 12)
                           - RPI_EXP_GPIO_BASE;

                if (gpio_num < VC4_GPIO_EXPANDER_COUNT) {
                    stl_le_phys(&s->dma_as, value + 16,
                                vc4_gpio_expander[gpio_num].direction);
                    stl_le_phys(&s->dma_as, value + 20,
                                vc4_gpio_expander[gpio_num].polarity);
                    stl_le_phys(&s->dma_as, value + 24,
                                vc4_gpio_expander[gpio_num].term_en);
                    stl_le_phys(&s->dma_as, value + 28,
                                vc4_gpio_expander[gpio_num].term_pull_up);
                    /* must be equal 0 */
                    stl_le_phys(&s->dma_as, value + 12, 0);
                    resplen = 4 * 5;
                } else {
                    qemu_log_mask(LOG_GUEST_ERROR,
                                  "RPI_FWREQ_GET_GPIO_CONFIG "
                                  "gpio num must be < %d",
                                  RPI_EXP_GPIO_BASE + VC4_GPIO_EXPANDER_COUNT);
                }
            }
            break;

        case RPI_FWREQ_SET_GPIO_CONFIG:
            if (ldl_le_phys(&s->dma_as, value + 12) < RPI_EXP_GPIO_BASE) {
                qemu_log_mask(LOG_UNIMP, "RPI_FWREQ_SET_GPIO_CONFIG "
                              "not implemented for gpiochip0\n");
            } else {
                gpio_num = ldl_le_phys(&s->dma_as, value + 12)
                           - RPI_EXP_GPIO_BASE;

                if (gpio_num < VC4_GPIO_EXPANDER_COUNT) {
                    vc4_gpio_expander[gpio_num].direction =
                        ldl_le_phys(&s->dma_as, value + 16);
                    vc4_gpio_expander[gpio_num].polarity =
                        ldl_le_phys(&s->dma_as, value + 20);
                    vc4_gpio_expander[gpio_num].term_en =
                        ldl_le_phys(&s->dma_as, value + 24);
                    vc4_gpio_expander[gpio_num].term_pull_up =
                        ldl_le_phys(&s->dma_as, value + 28);
                    vc4_gpio_expander[gpio_num].state =
                        ldl_le_phys(&s->dma_as, value + 32);
                    /* must be equal 0 */
                    stl_le_phys(&s->dma_as, value + 12, 0);
                    resplen = 4;
                } else {
                    qemu_log_mask(LOG_GUEST_ERROR,
                                  "RPI_FWREQ_SET_GPIO_CONFIG "
                                  "gpio num must be < %d",
                                  RPI_EXP_GPIO_BASE + VC4_GPIO_EXPANDER_COUNT);
                }
            }
            break;

        case RPI_FWREQ_GET_GPIO_STATE:
            if (ldl_le_phys(&s->dma_as, value + 12) < RPI_EXP_GPIO_BASE) {
                qemu_log_mask(LOG_UNIMP, "RPI_FWREQ_GET_GPIO_STATE "
                              "not implemented for gpiochip0\n");
            } else {
                gpio_num = ldl_le_phys(&s->dma_as, value + 12)
                           - RPI_EXP_GPIO_BASE;

                if (gpio_num < VC4_GPIO_EXPANDER_COUNT) {
                    stl_le_phys(&s->dma_as, value + 16,
                                vc4_gpio_expander[gpio_num].state);
                    /* must be equal 0 */
                    stl_le_phys(&s->dma_as, value + 12, 0);
                    resplen = 8;
                } else {
                    qemu_log_mask(LOG_GUEST_ERROR,
                                  "RPI_FWREQ_GET_GPIO_STATE "
                                  "gpio num must be < %d",
                                  RPI_EXP_GPIO_BASE + VC4_GPIO_EXPANDER_COUNT);
                }
            }
            break;

        case RPI_FWREQ_SET_GPIO_STATE:
            if (ldl_le_phys(&s->dma_as, value + 12) < RPI_EXP_GPIO_BASE) {
                qemu_log_mask(LOG_UNIMP, "RPI_FWREQ_SET_GPIO_STATE not "
                              "implemented for gpiochip0\n");
            } else {
                gpio_num = ldl_le_phys(&s->dma_as, value + 12)
                           - RPI_EXP_GPIO_BASE;

                if (gpio_num < VC4_GPIO_EXPANDER_COUNT) {
                    vc4_gpio_expander[gpio_num].state = ldl_le_phys(&s->dma_as,
                                                                    value + 16);
                    /* must be equal 0 */
                    stl_le_phys(&s->dma_as, value + 12, 0);
                    resplen = 4;
                } else {
                    qemu_log_mask(LOG_GUEST_ERROR,
                                  "RPI_FWREQ_SET_GPIO_STATE "
                                  "gpio num must be < %d",
                                  RPI_EXP_GPIO_BASE + VC4_GPIO_EXPANDER_COUNT);
                }
            }
            break;

        case RPI_FWREQ_VCHIQ_INIT:
            stl_le_phys(&s->dma_as,
                        value + offsetof(rpi_firmware_prop_request_t, payload),
                        0);
            resplen = VCHI_BUSADDR_SIZE;
            break;

        default:
            qemu_log_mask(LOG_UNIMP,
                          "bcm2835_property: unhandled tag 0x%08x\n", tag);
            break;
        }

        trace_bcm2835_mbox_property(tag, bufsize, resplen);
        if (tag == 0) {
            break;
        }

        stl_le_phys(&s->dma_as, value + 8, (1 << 31) | resplen);
        value += bufsize + 12;
    }

    /* Reconfigure framebuffer if required */
    if (fbconfig_updated) {
        bcm2835_fb_reconfigure(s->fbdev, &fbconfig);
    }

    /* Buffer response code */
    stl_le_phys(&s->dma_as, s->addr + 4, (1 << 31));
}

static uint64_t bcm2835_property_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    BCM2835PropertyState *s = opaque;
    uint32_t res = 0;

    switch (offset) {
    case MBOX_AS_DATA:
        res = MBOX_CHAN_PROPERTY | s->addr;
        s->pending = false;
        qemu_set_irq(s->mbox_irq, 0);
        break;

    case MBOX_AS_PENDING:
        res = s->pending;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
        return 0;
    }

    return res;
}

static void bcm2835_property_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size)
{
    BCM2835PropertyState *s = opaque;

    switch (offset) {
    case MBOX_AS_DATA:
        /* bcm2835_mbox should check our pending status before pushing */
        assert(!s->pending);
        s->pending = true;
        bcm2835_property_mbox_push(s, value);
        qemu_set_irq(s->mbox_irq, 1);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
        return;
    }
}

static const MemoryRegionOps bcm2835_property_ops = {
    .read = bcm2835_property_read,
    .write = bcm2835_property_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const VMStateDescription vmstate_bcm2835_property = {
    .name = TYPE_BCM2835_PROPERTY,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_MACADDR(macaddr, BCM2835PropertyState),
        VMSTATE_UINT32(addr, BCM2835PropertyState),
        VMSTATE_BOOL(pending, BCM2835PropertyState),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_property_init(Object *obj)
{
    BCM2835PropertyState *s = BCM2835_PROPERTY(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &bcm2835_property_ops, s,
                          TYPE_BCM2835_PROPERTY, 0x10);

    /*
     * bcm2835_property_ops call into bcm2835_mbox, which in-turn reads from
     * iomem. As such, mark iomem as re-entracy safe.
     */
    s->iomem.disable_reentrancy_guard = true;

    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->mbox_irq);
}

static void bcm2835_property_reset(DeviceState *dev)
{
    BCM2835PropertyState *s = BCM2835_PROPERTY(dev);

    s->pending = false;
}

static void bcm2835_property_realize(DeviceState *dev, Error **errp)
{
    BCM2835PropertyState *s = BCM2835_PROPERTY(dev);
    Object *obj;

    obj = object_property_get_link(OBJECT(dev), "fb", &error_abort);
    s->fbdev = BCM2835_FB(obj);

    obj = object_property_get_link(OBJECT(dev), "dma-mr", &error_abort);
    s->dma_mr = MEMORY_REGION(obj);
    address_space_init(&s->dma_as, s->dma_mr, TYPE_BCM2835_PROPERTY "-memory");

    /* TODO: connect to MAC address of USB NIC device, once we emulate it */
    qemu_macaddr_default_if_unset(&s->macaddr);

    bcm2835_property_reset(dev);
}

static Property bcm2835_property_props[] = {
    DEFINE_PROP_UINT32("board-rev", BCM2835PropertyState, board_rev, 0),
    DEFINE_PROP_STRING("command-line", BCM2835PropertyState, command_line),
    DEFINE_PROP_END_OF_LIST()
};

static void bcm2835_property_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, bcm2835_property_props);
    dc->realize = bcm2835_property_realize;
    dc->vmsd = &vmstate_bcm2835_property;
}

static const TypeInfo bcm2835_property_info = {
    .name          = TYPE_BCM2835_PROPERTY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835PropertyState),
    .class_init    = bcm2835_property_class_init,
    .instance_init = bcm2835_property_init,
};

static void bcm2835_property_register_types(void)
{
    type_register_static(&bcm2835_property_info);
}

type_init(bcm2835_property_register_types)
