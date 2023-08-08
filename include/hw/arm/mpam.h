#ifndef _MPAM_H_
#define _MPAM_H_

#include "qom/object.h"
#include "qapi/qapi-commands-mpam.h"

#define TYPE_MPAM_MSC_MEM "mpam-msc-mem"
#define TYPE_MPAM_MSC_CACHE "mpam-msc-cache"

#define MPAM_SIZE 0x4000 /* Big enough for anyone ;) */
void mpam_cache_fill_info(Object *obj, MpamCacheInfo *info);

#endif /* _MPAM_H_ */
