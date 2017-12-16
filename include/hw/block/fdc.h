#ifndef HW_FDC_H
#define HW_FDC_H

#include "qemu-common.h"
#include "hw/block/block.h"
#include "hw/isa/isa.h"

/* fdc.c */
#define MAX_FD 2

typedef struct FDCtrl FDCtrl;

/* Floppy disk drive emulation */
typedef enum FDiskFlags {
    FDISK_DBL_SIDES  = 0x01,
} FDiskFlags;

typedef struct FDrive {
    FDCtrl *fdctrl;
    BlockBackend *blk;
    BlockConf *conf;
    /* Drive status */
    FloppyDriveType drive;    /* CMOS drive type        */
    uint8_t perpendicular;    /* 2.88 MB access mode    */
    /* Position */
    uint8_t head;
    uint8_t track;
    uint8_t sect;
    /* Media */
    FloppyDriveType disk;     /* Current disk type      */
    FDiskFlags flags;
    uint8_t last_sect;        /* Nb sector per track    */
    uint8_t max_track;        /* Nb of tracks           */
    uint16_t bps;             /* Bytes per sector       */
    uint8_t ro;               /* Is read-only           */
    uint8_t media_changed;    /* Is media changed       */
    uint8_t media_rate;       /* Data rate of medium    */

    bool media_validated;     /* Have we validated the media? */
} FDrive;

typedef struct FloppyBus {
    BusState bus;
    FDCtrl *fdc;
} FloppyBus;

struct FDCtrl {
    MemoryRegion iomem;
    qemu_irq irq;
    /* Controller state */
    QEMUTimer *result_timer;
    int dma_chann;
    uint8_t phase;
    IsaDma *dma;
    /* Controller's identification */
    uint8_t version;
    /* HW */
    uint8_t sra;
    uint8_t srb;
    uint8_t dor;
    uint8_t dor_vmstate; /* only used as temp during vmstate */
    uint8_t tdr;
    uint8_t dsr;
    uint8_t msr;
    uint8_t cur_drv;
    uint8_t status0;
    uint8_t status1;
    uint8_t status2;
    /* Command FIFO */
    uint8_t *fifo;
    int32_t fifo_size;
    uint32_t data_pos;
    uint32_t data_len;
    uint8_t data_state;
    uint8_t data_dir;
    uint8_t eot; /* last wanted sector */
    /* States kept only to be returned back */
    /* precompensation */
    uint8_t precomp_trk;
    uint8_t config;
    uint8_t lock;
    /* Power down config (also with status regB access mode */
    uint8_t pwrd;
    /* Floppy drives */
    FloppyBus bus;
    uint8_t num_floppies;
    FDrive drives[MAX_FD];
    struct {
        BlockBackend *blk;
        FloppyDriveType type;
    } qdev_for_drives[MAX_FD];
    int reset_sensei;
    uint32_t check_media_rate;
    FloppyDriveType fallback; /* type=auto failure fallback */
    /* Timers state */
    uint8_t timer0;
    uint8_t timer1;
    PortioList portio_list;
};

#define TYPE_ISA_FDC "isa-fdc"

typedef struct FDCtrlISABus {
    ISADevice parent_obj;

    uint32_t iobase;
    uint32_t irq;
    uint32_t dma;
    struct FDCtrl state;
    int32_t bootindexA;
    int32_t bootindexB;
} FDCtrlISABus;

ISADevice *fdctrl_init_isa(ISABus *bus, DriveInfo **fds);
void fdctrl_init_sysbus(qemu_irq irq, int dma_chann,
                        hwaddr mmio_base, DriveInfo **fds);
void sun4m_fdctrl_init(qemu_irq irq, hwaddr io_base,
                       DriveInfo **fds, qemu_irq *fdc_tc);

FloppyDriveType isa_fdc_get_drive_type(ISADevice *fdc, int i);
void isa_fdc_get_drive_max_chs(FloppyDriveType type,
                               uint8_t *maxc, uint8_t *maxh, uint8_t *maxs);

#endif
