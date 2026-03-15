/*
 * Stub for replay-audio.c
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "system/replay.h"

void replay_audio_in_start(size_t *nsamples)
{
}
void replay_audio_in_sample_lr(uint64_t *left, uint64_t *right)
{
}
void replay_audio_in_finish(void)
{
}
void replay_audio_out(size_t *played)
{
}
