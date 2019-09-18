#include "qemu/osdep.h"

#include "fuzz.h"
#include "tests/libqtest.h"
#include "fuzz/qos_fuzz.h"
#include "fuzz/fork_fuzz.h"
#include "qemu/main-loop.h"
#include "tests/libqos/pci.h"
#include "tests/libqos/pci-pc.h"

enum action_id {
    WRITEB,
    WRITEW,
    WRITEL,
    READB,
    READW,
    READL,
};

static void i440fx_fuzz_qtest(QTestState *s,
        const unsigned char *Data, size_t Size) {

    typedef struct QTestFuzzAction {
        uint8_t id;
        uint8_t addr;
        uint32_t value;
    } QTestFuzzAction;
    QTestFuzzAction *a = (QTestFuzzAction *)Data;
    while (Size >= sizeof(QTestFuzzAction)) {
        uint16_t addr = a->addr % 2 ? 0xcf8 : 0xcfc;
        switch (a->id) {
        case WRITEB:
            qtest_outb(s, addr, (uint8_t)a->value);
            break;
        case WRITEW:
            qtest_outw(s, addr, (uint16_t)a->value);
            break;
        case WRITEL:
            qtest_outl(s, addr, (uint32_t)a->value);
            break;
        case READB:
            qtest_inb(s, addr);
            break;
        case READW:
            qtest_inw(s, addr);
            break;
        case READL:
            qtest_inl(s, addr);
            break;
        }
        a++;
        Size -= sizeof(QTestFuzzAction);
    }
    qtest_clock_step_next(s);
    main_loop_wait(true);
    reboot(s);
}

static void i440fx_fuzz_qos(QTestState *s,
        const unsigned char *Data, size_t Size) {

    typedef struct QOSFuzzAction {
        uint8_t id;
        int devfn;
        uint8_t offset;
        uint32_t value;
    } QOSFuzzAction;

    QOSFuzzAction *a = (QOSFuzzAction *)Data;
    static QPCIBus *bus;
    if (!bus) {
        bus = qpci_new_pc(s, fuzz_qos_alloc);
    }

    while (Size >= sizeof(QOSFuzzAction)) {
        switch (a->id) {
        case WRITEB:
            bus->config_writeb(bus, a->devfn, a->offset, (uint8_t)a->value);
            break;
        case WRITEW:
            bus->config_writew(bus, a->devfn, a->offset, (uint16_t)a->value);
            break;
        case WRITEL:
            bus->config_writel(bus, a->devfn, a->offset, (uint32_t)a->value);
            break;
        case READB:
            bus->config_readb(bus, a->devfn, a->offset);
            break;
        case READW:
            bus->config_readw(bus, a->devfn, a->offset);
            break;
        case READL:
            bus->config_readl(bus, a->devfn, a->offset);
            break;
        }
        a++;
        Size -= sizeof(QOSFuzzAction);
    }
    qtest_clock_step_next(s);
    main_loop_wait(true);
}

static void i440fx_fuzz_qos_fork(QTestState *s,
        const unsigned char *Data, size_t Size) {
    if (fork() == 0) {
        i440fx_fuzz_qos(s, Data, Size);
        counter_shm_store();
        _Exit(0);
    } else {
        wait(NULL);
        counter_shm_load();
    }
}

static void fork_init(QTestState *s)
{
    counter_shm_init();
}
static const char *i440fx_qtest_argv[] = {"qemu_system_i386", "-machine", "accel=qtest"};

static void register_pci_fuzz_targets(void)
{
    /* Uses simple qtest commands and reboots to reset state */
    fuzz_add_target("i440fx-qtest-reboot-fuzz",
            "Fuzz the i440fx using raw qtest commands and rebooting"
            "after each run",
            &(FuzzTarget){
                .fuzz = i440fx_fuzz_qtest,
                .main_argc = 3,
                .main_argv = (char **)i440fx_qtest_argv,
                });

    /* Uses libqos and forks to prevent state leakage */
    fuzz_add_qos_target("i440fx-qos-fork-fuzz",
            "Fuzz the i440fx using qos helpers and forking"
            "for each run",
            "i440FX-pcihost",
            &(QOSGraphTestOptions){},
            &(FuzzTarget){
            .pre_main = &qos_setup,
            .pre_fuzz = &fork_init,
            .fuzz = &i440fx_fuzz_qos_fork
            });

    /* Uses libqos. Doesn't do anything to reset state. Note that if we were to
     reboot after each run, we would also have to redo the qos-related
     initialization (qos_init_path) */
    fuzz_add_qos_target("i440fx-qos-nocleanup-fuzz",
            "Fuzz the i440fx using qos helpers. No cleanup done after each run",
            "i440FX-pcihost",
            &(QOSGraphTestOptions){},
            &(FuzzTarget){
            .pre_main = &qos_setup,
            .fuzz = &i440fx_fuzz_qos
            });
}

fuzz_target_init(register_pci_fuzz_targets);
