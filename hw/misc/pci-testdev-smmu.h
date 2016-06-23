#ifndef HW_MISC_PCI_TESTDEV_SMMU
#define HW_MISC_PCI_TESTDEV_SMMU

enum reg {
        TST_REG_COMMAND  = 0x0,
        TST_REG_STATUS   = 0x4,
        TST_REG_SRC_ADDR = 0x8,
        TST_REG_SIZE     = 0x10,
        TST_REG_DST_ADDR = 0x18,

        TST_REG_LAST     = 0x30,
};

#define CMD_READ    0x100
#define CMD_WRITE   0x200
#define CMD_RW      (CMD_READ | CMD_WRITE)

#define STATUS_OK   (1 << 0)
#define STATUS_CMD_ERROR (1 << 1)
#define STATUS_CMD_INVALID (1 << 2)

#endif
