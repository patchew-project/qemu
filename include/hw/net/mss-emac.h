#include "hw/sysbus.h"
#include "net/net.h"

#define TYPE_MSS_EMAC "mss-emac"
#define MSS_EMAC(obj) \
    OBJECT_CHECK(MSSEmacState, (obj), TYPE_MSS_EMAC)

#define R_MAX         (0x1a0 / 4)

typedef struct MSSEmacState {
    SysBusDevice parent;

    MemoryRegion mmio;
    qemu_irq irq;
    NICState *nic;
    NICConf conf;
    uint32_t tx_desc;
    uint32_t rx_desc;
    bool rx_enabled;
    uint16_t phy_regs[32];

    uint32_t regs[R_MAX];
} MSSEmacState;
