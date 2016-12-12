#ifndef CHAR_MUX_H
#define CHAR_MUX_H

#include "sysemu/char.h"

extern bool muxes_realized;

#define MAX_MUX 4
#define MUX_BUFFER_SIZE 32 /* Must be a power of 2.  */
#define MUX_BUFFER_MASK (MUX_BUFFER_SIZE - 1)
typedef struct MuxChardev {
    Chardev parent;
    CharBackend *backends[MAX_MUX];
    CharBackend chr;
    int focus;
    int mux_cnt;
    int term_got_escape;
    int max_size;
    /* Intermediate input buffer allows to catch escape sequences even if the
       currently active device is not accepting any input - but only until it
       is full as well. */
    unsigned char buffer[MAX_MUX][MUX_BUFFER_SIZE];
    int prod[MAX_MUX];
    int cons[MAX_MUX];
    int timestamps;

    /* Protected by the Chardev chr_write_lock.  */
    int linestart;
    int64_t timestamps_start;
} MuxChardev;

#define MUX_CHARDEV(obj) OBJECT_CHECK(MuxChardev, (obj), TYPE_CHARDEV_MUX)
#define CHARDEV_IS_MUX(chr)                             \
    object_dynamic_cast(OBJECT(chr), TYPE_CHARDEV_MUX)

void mux_chr_set_handlers(Chardev *chr, GMainContext *context);
void mux_set_focus(MuxChardev *d, int focus);
void mux_chr_send_event(MuxChardev *d, int mux_nr, int event);

#endif /* CHAR_MUX_H */
