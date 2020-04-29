#ifndef HW_SOUNDHW_H
#define HW_SOUNDHW_H

#include "qom/object.h"

void soundhw_init(void);
void select_soundhw(const char *optarg);

#define SOUNDHW_CMDLINE_INTERFACE "soundhw-deprecated"

#define SOUNDHW_CMDLINE_CLASS(class) \
    OBJECT_CLASS_CHECK(SoundHwCmdlineClass, (class), SOUNDHW_CMDLINE_INTERFACE)

typedef struct SoundHwCmdlineClass {
    /*< private >*/
    InterfaceClass parent_class;
    /*< public >*/

    const char *cmdline_name;
    bool option_used;
} SoundHwCmdlineClass;

#endif
