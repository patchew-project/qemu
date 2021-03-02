#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/rcu.h"
#include "qemu/coroutine.h"
#include "qemu/timer.h"
#include "io/channel.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "trace.h"
#include "confidential-ram.h"

enum cgs_mig_helper_cmd {
    /* Initialize migration helper in guest */
    CGS_MIG_HELPER_CMD_INIT = 0,

    /*
     * Fetch a page from gpa, encrypt it, and save result into the shared page
     */
    CGS_MIG_HELPER_CMD_ENCRYPT,

    /* Read the shared page, decrypt it, and save result into gpa */
    CGS_MIG_HELPER_CMD_DECRYPT,

    /* Reset migration helper in guest */
    CGS_MIG_HELPER_CMD_RESET,

    CGS_MIG_HELPER_CMD_MAX
};

struct QEMU_PACKED CGSMigHelperCmdParams {
    uint64_t cmd_type;
    uint64_t gpa;
    int32_t prefetch;
    int32_t ret;
    int32_t go;
    int32_t done;
};
typedef struct CGSMigHelperCmdParams CGSMigHelperCmdParams;

struct QEMU_PACKED CGSMigHelperPageHeader {
    uint32_t len;
    uint8_t data[0];
};
typedef struct CGSMigHelperPageHeader CGSMigHelperPageHeader;

struct CGSMigHelperState {
    CGSMigHelperCmdParams *cmd_params;
    CGSMigHelperPageHeader *io_page_hdr;
    uint8_t *io_page;
    bool initialized;
};
typedef struct CGSMigHelperState CGSMigHelperState;

static CGSMigHelperState cmhs = {0};

#define MH_BUSYLOOP_TIMEOUT       100000000LL
#define MH_REQUEST_TIMEOUT_MS     100
#define MH_REQUEST_TIMEOUT_NS     (MH_REQUEST_TIMEOUT_MS * 1000 * 1000)

/*
 * The migration helper shared area is hard-coded at gpa 0x820000 with size of
 * 2 pages (0x2000 bytes).  Instead of hard-coding, the address and size may be
 * fetched from OVMF itself using a pc_system_ovmf_table_find call to query
 * OVMF's GUIDed structure for a migration helper GUID.
 */
#define MH_SHARED_CMD_PARAMS_ADDR    0x820000
#define MH_SHARED_IO_PAGE_HDR_ADDR   (MH_SHARED_CMD_PARAMS_ADDR + 0x800)
#define MH_SHARED_IO_PAGE_ADDR       (MH_SHARED_CMD_PARAMS_ADDR + 0x1000)

void cgs_mh_init(void)
{
    RCU_READ_LOCK_GUARD();
    cmhs.cmd_params = qemu_map_ram_ptr(NULL, MH_SHARED_CMD_PARAMS_ADDR);
    cmhs.io_page_hdr = qemu_map_ram_ptr(NULL, MH_SHARED_IO_PAGE_HDR_ADDR);
    cmhs.io_page = qemu_map_ram_ptr(NULL, MH_SHARED_IO_PAGE_ADDR);
}

static int send_command_to_cgs_mig_helper(uint64_t cmd_type, uint64_t gpa)
{
    /*
     * The cmd_params struct is on a page shared with the guest migration
     * helper.  We use a volatile struct to force writes to memory so that the
     * guest can see them.
     */
    volatile CGSMigHelperCmdParams *params = cmhs.cmd_params;
    int64_t counter, request_timeout_at;

    /*
     * At this point io_page and io_page_hdr should be already filled according
     * to the requested cmd_type.
     */

    params->cmd_type = cmd_type;
    params->gpa = gpa;
    params->prefetch = 0;
    params->ret = -1;
    params->done = 0;

    /*
     * Force writes of all command parameters before writing the 'go' flag.
     * The guest migration handler waits for the go flag and then reads the
     * command parameters.
     */
    smp_wmb();

    /* Tell the migration helper to start working on this command */
    params->go = 1;

    /*
     * Wait for the guest migration helper to process the command and mark the
     * done flag
     */
    request_timeout_at = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) +
                         MH_REQUEST_TIMEOUT_NS;
    do {
        counter = 0;
        while (!params->done && (counter < MH_BUSYLOOP_TIMEOUT)) {
            counter++;
        }
    } while (!params->done &&
             qemu_clock_get_ns(QEMU_CLOCK_REALTIME) < request_timeout_at);

    if (!params->done) {
        error_report("Migration helper command %" PRIu64 " timed-out for "
                     "gpa 0x%" PRIx64, cmd_type, gpa);
        return -EIO;
    }

    return params->ret;
}

static void init_cgs_mig_helper_if_needed(void)
{
    int ret;

    if (cmhs.initialized) {
        return;
    }

    ret = send_command_to_cgs_mig_helper(CGS_MIG_HELPER_CMD_INIT, 0);
    if (ret == 0) {
        cmhs.initialized = true;
    }
}

void cgs_mh_cleanup(void)
{
    send_command_to_cgs_mig_helper(CGS_MIG_HELPER_CMD_RESET, 0);
}

int cgs_mh_save_encrypted_page(QEMUFile *f, ram_addr_t src_gpa, uint32_t size,
                               uint64_t *bytes_sent)
{
    int ret;

    init_cgs_mig_helper_if_needed();

    /* Ask the migration helper to encrypt the page at src_gpa */
    trace_encrypted_ram_save_page(size, src_gpa);
    ret = send_command_to_cgs_mig_helper(CGS_MIG_HELPER_CMD_ENCRYPT, src_gpa);
    if (ret) {
        error_report("Error cgs_mh_save_encrypted_page ret=%d", ret);
        return -1;
    }

    /* Sanity check for response header */
    if (cmhs.io_page_hdr->len > 1024) {
        error_report("confidential-ram: migration helper response is too large "
                     "(len=%u)", cmhs.io_page_hdr->len);
        return -EINVAL;
    }

    qemu_put_be32(f, cmhs.io_page_hdr->len);
    qemu_put_buffer(f, cmhs.io_page_hdr->data, cmhs.io_page_hdr->len);
    *bytes_sent = 4 + cmhs.io_page_hdr->len;

    qemu_put_be32(f, size);
    qemu_put_buffer(f, cmhs.io_page, size);
    *bytes_sent += 4 + size;

    return ret;
}
