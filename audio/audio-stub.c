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
  error_setg(
      errp,
      "trying to find audiodev '%s' by name with audio component disabled",
      name);
  return NULL;
}

const char *audio_be_get_id(AudioBackend *be) { return ""; }

bool audio_be_set_dbus_server(AudioBackend *be,
                              GDBusObjectManagerServer *server, bool p2p,
                              Error **errp)
{
  error_setg(errp, "trying to set dbus server with audio component disabled");
  return false;
}

void audio_init_audiodevs(void) {}

void audio_create_default_audiodevs(void) {}
