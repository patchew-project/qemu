/*
 * Virtio Sound device
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/iov.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "hw/virtio/virtio.h"
#include "audio/audio.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "qemu/option.h"
#include "qemu/option_int.h"
#include "qemu/config-file.h"
#include "qapi/qmp/qdict.h"
#include "hw/virtio/virtio-snd.h"
#include "hw/virtio/virtio-bus.h"
#include "qapi/error.h"
#include "qapi/qapi-events-audio.h"
#include "hw/qdev-properties.h"
#include "qapi/qapi-types-migration.h"
#include "qapi/qapi-events-migration.h"
#include "migration/misc.h"
#include "standard-headers/linux/ethtool.h"
#include "sysemu/sysemu.h"
#include "trace.h"
#include "monitor/qdev.h"
#include "hw/pci/pci.h"
#include "intel-hda-defs.h"

#define VIRTIO_SOUND_VM_VERSION 1

#define VIRTIO_SOUND_JACK_DEFAULT 0
#define VIRTIO_SOUND_STREAM_DEFAULT 1
#define VIRTIO_SOUND_CHMAP_DEFAULT 0

#define VIRTIO_SOUND_HDA_FN_NID_OUT 0
#define VIRTIO_SOUND_HDA_FN_NID_IN 1

static void virtio_snd_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VirtIOSound *s = VIRTIO_SOUND(vdev);
    virtio_snd_config sndcfg;

    memset(&sndcfg, 0, sizeof(virtio_snd_config));
    stl_le_p(&(sndcfg.jacks), s->snd_conf.jacks);
    stl_le_p(&(sndcfg.streams), s->snd_conf.streams);
    stl_le_p(&(sndcfg.chmaps), s->snd_conf.chmaps);

    memcpy(config, &sndcfg, sizeof(virtio_snd_config));
}

static void virtio_snd_set_config(VirtIODevice *vdev, const uint8_t *config)
{
    VirtIOSound *s = VIRTIO_SOUND(vdev);
    virtio_snd_config sndcfg;

    memcpy(&sndcfg, config, sizeof(virtio_snd_config));

    memcpy(&s->snd_conf.jacks, &sndcfg.jacks, sizeof(uint32_t));
    memcpy(&s->snd_conf.streams, &sndcfg.streams, sizeof(uint32_t));
    memcpy(&s->snd_conf.chmaps, &sndcfg.streams, sizeof(uint32_t));
}

static const VMStateDescription vmstate_virtio_snd_device = {
    .name = "virtio-snd-device",
    .version_id = VIRTIO_SOUND_VM_VERSION,
    .minimum_version_id = VIRTIO_SOUND_VM_VERSION,
};

static const VMStateDescription vmstate_virtio_snd = {
    .name = "virtio-sound",
    .minimum_version_id = VIRTIO_SOUND_VM_VERSION,
    .version_id = VIRTIO_SOUND_VM_VERSION,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property virtio_snd_properties[] = {
    DEFINE_AUDIO_PROPERTIES(VirtIOSound, card),
    DEFINE_PROP_UINT32("jacks", VirtIOSound, snd_conf.jacks,
                       VIRTIO_SOUND_JACK_DEFAULT),
    DEFINE_PROP_UINT32("streams", VirtIOSound, snd_conf.streams,
                       VIRTIO_SOUND_STREAM_DEFAULT),
    DEFINE_PROP_UINT32("chmaps", VirtIOSound, snd_conf.chmaps,
                       VIRTIO_SOUND_CHMAP_DEFAULT),
    DEFINE_PROP_END_OF_LIST(),
};

static uint64_t virtio_snd_get_features(VirtIODevice *vdev, uint64_t features,
                                        Error **errp)
{
    return vdev->host_features;
}

/*
 * Initializes the VirtIOSound card device. Validates the configuration
 * passed by the command line. Initializes the virtqueues. Allocates resources
 * for and initializes streams, jacks and chmaps.
 *
 * @dev: VirtIOSound card device
 * @errp: Set if there is an error
 */
static void virtio_snd_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOSound *s = VIRTIO_SOUND(dev);

    virtio_init(vdev, "virtio-snd", VIRTIO_ID_SOUND, sizeof(virtio_snd_config));

    /* set number of jacks and streams */
    if (s->snd_conf.jacks > 8) {
        error_setg(errp, "Invalid number of jacks: %d", s->snd_conf.jacks);
        return;
    }
    if (s->snd_conf.streams < 1 || s->snd_conf.streams > 10) {
        error_setg(errp, "Invalid number of streams: %d", s->snd_conf.streams);
        return;
    }

    if (s->snd_conf.chmaps > VIRTIO_SND_CHMAP_MAX_SIZE) {
        error_setg(errp, "Invalid number of channel maps: %d",
                   s->snd_conf.chmaps);
        return;
    }

    /* set up QEMUSoundCard and audiodev */
    AUD_register_card ("virtio_snd_card", &s->card);

    s->streams = g_new0(virtio_snd_pcm_stream *, s->snd_conf.streams);
    s->pcm_params = g_new0(virtio_snd_pcm_params *, s->snd_conf.streams);
    s->jacks = g_new0(virtio_snd_jack *, s->snd_conf.jacks);
}

static void virtio_snd_device_unrealize(DeviceState *dev)
{
}

static void virtio_snd_reset(VirtIODevice *vdev)
{
}

static void virtio_snd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, virtio_snd_properties);
    dc->vmsd = &vmstate_virtio_snd;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    vdc->realize = virtio_snd_device_realize;
    vdc->unrealize = virtio_snd_device_unrealize;
    vdc->get_config = virtio_snd_get_config;
    vdc->set_config = virtio_snd_set_config;
    vdc->get_features = virtio_snd_get_features;
    vdc->reset = virtio_snd_reset;
    vdc->legacy_features = 0;
    vdc->vmsd = &vmstate_virtio_snd_device;
}


static const TypeInfo virtio_snd_dev_info = {
    .name = TYPE_VIRTIO_SOUND,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOSound),
    .class_init = virtio_snd_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_snd_dev_info);
}

type_init(virtio_register_types)
