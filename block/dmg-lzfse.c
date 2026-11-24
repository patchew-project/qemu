/*
 * DMG lzfse uncompression
 *
 * Copyright (c) 2018 Julio Cesar Faracco
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "dmg.h"

/* Work around a -Wstrict-prototypes warning in LZFSE headers */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include <lzfse.h>
#pragma GCC diagnostic pop

static int dmg_uncompress_lzfse_do(char *next_in, unsigned int avail_in,
                                   char *next_out, unsigned int avail_out)
{
    size_t out_size = lzfse_decode_buffer((uint8_t *) next_out, avail_out,
                                          (uint8_t *) next_in, avail_in,
                                          NULL);

    /*
     * lzfse_decode_buffer() returns the number of bytes written to the
     * destination buffer, or the destination capacity if the buffer is
     * too small.  Reject any decode that did not fill the full chunk:
     * a truncated decode would expose the uninitialized tail of the
     * uncompressed_chunk buffer to the guest.
     */
    if (out_size < avail_out) {
        return -1;
    }
    return out_size;
}

__attribute__((constructor))
static void dmg_lzfse_init(void)
{
    assert(!dmg_uncompress_lzfse);
    dmg_uncompress_lzfse = dmg_uncompress_lzfse_do;
}
