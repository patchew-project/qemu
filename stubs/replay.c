#include "qemu/osdep.h"
#include "sysemu/replay.h"

ReplayMode replay_mode;

int64_t replay_save_clock(unsigned int kind, int64_t clock, int64_t raw_icount)
{
    abort();
    return 0;
}

int64_t replay_read_clock(unsigned int kind)
{
    abort();
    return 0;
}

bool replay_checkpoint(ReplayCheckpoint checkpoint)
{
    return true;
}

bool replay_events_enabled(void)
{
    return false;
}

void replay_finish(void)
{
}

void replay_register_char_driver(Chardev *chr)
{
}

void replay_chr_be_write(Chardev *s, uint8_t *buf, int len)
{
    abort();
}

void replay_char_write_event_save(int res, int offset)
{
    abort();
}

void replay_char_write_event_load(int *res, int *offset)
{
    abort();
}

int replay_char_read_all_load(uint8_t *buf)
{
    abort();
}

void replay_char_read_all_save_error(int res)
{
    abort();
}

void replay_char_read_all_save_buf(uint8_t *buf, int offset)
{
    abort();
}

void replay_block_event(QEMUBH *bh, uint64_t id)
{
}

uint64_t blkreplay_next_id(void)
{
    return 0;
}

void replay_mutex_lock(void)
{
}

void replay_mutex_unlock(void)
{
}

void replay_save_random(int ret, void *buf, size_t len)
{
}

int replay_read_random(void *buf, size_t len)
{
    return 0;
}

uint64_t replay_get_current_icount(void)
{
    return 0;
}

bool replay_reverse_step(void)
{
    return false;
}

bool replay_reverse_continue(void)
{
    return false;
}

/*
 * the following event-related stubs need to return false,
 * so that normal events processing can happen when the replay framework
 * is not available (!CONFIG_TCG)
 */
bool replay_input_event(QemuConsole *src, InputEvent *evt)
{
    return false;
}
bool replay_input_sync_event(void)
{
    return false;
}
bool replay_bh_schedule_event(QEMUBH *bh)
{
    return false;
}
bool replay_bh_schedule_oneshot_event(AioContext *ctx,
                                      QEMUBHFunc *cb, void *opaque)
{
    return false;
}

void replay_add_blocker(Error *reason)
{
}
void replay_audio_in(size_t *recorded, void *samples, size_t *wpos, size_t size)
{
}
void replay_audio_out(size_t *played)
{
}
void replay_breakpoint(void)
{
}
bool replay_can_snapshot(void)
{
    return false;
}
void replay_configure(struct QemuOpts *opts)
{
}
void replay_flush_events(void)
{
}
void replay_gdb_attached(void)
{
}
bool replay_running_debug(void)
{
    return false;
}
void replay_shutdown_request(ShutdownCause cause)
{
}
void replay_start(void)
{
}
void replay_vmstate_init(void)
{
}

#include "monitor/monitor.h"
#include "monitor/hmp.h"
#include "qapi/qapi-commands-replay.h"
#include "qapi/error.h"
#include "qemu/error-report.h"

void hmp_info_replay(Monitor *mon, const QDict *qdict)
{
    error_report("replay support not available");
}
void hmp_replay_break(Monitor *mon, const QDict *qdict)
{
    error_report("replay support not available");
}
void hmp_replay_delete_break(Monitor *mon, const QDict *qdict)
{
    error_report("replay support not available");
}
void hmp_replay_seek(Monitor *mon, const QDict *qdict)
{
    error_report("replay support not available");
}
ReplayInfo *qmp_query_replay(Error **errp)
{
    error_set(errp, ERROR_CLASS_COMMAND_NOT_FOUND,
              "replay support not available");
    return NULL;
}
void qmp_replay_break(int64_t icount, Error **errp)
{
    error_set(errp, ERROR_CLASS_COMMAND_NOT_FOUND,
              "replay support not available");
}
void qmp_replay_delete_break(Error **errp)
{
    error_set(errp, ERROR_CLASS_COMMAND_NOT_FOUND,
              "replay support not available");
}
void qmp_replay_seek(int64_t icount, Error **errp)
{
    error_set(errp, ERROR_CLASS_COMMAND_NOT_FOUND,
              "replay support not available");
}
