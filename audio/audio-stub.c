/*
 * Stub for audio.c
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/qapi-commands-audio.h"
#include "qemu/audio.h"
#include "qapi/error.h"

void audio_cleanup(void) {}

AudioBackend *audio_be_by_name(const char *name, Error **errp)
{
  error_setg(errp, "audio disabled");
  return NULL;
}

void audio_init_audiodevs(void) {}

void audio_create_default_audiodevs(void) {}
