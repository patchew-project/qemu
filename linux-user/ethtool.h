#ifndef ETHTOOL_H
#define ETHTOOL_H

#include <linux/if.h>
#include "qemu.h"

extern const StructEntry struct_ethtool_rxnfc_get_set_rxfh_def;
extern const StructEntry struct_ethtool_sset_info_def;
extern const StructEntry struct_ethtool_rxfh_def;
extern const StructEntry struct_ethtool_link_settings_def;
extern const StructEntry struct_ethtool_per_queue_op_def;

/*
 * Takes the file descriptor and the buffer for temporarily storing data read
 * from / to be written to guest memory. `buf_temp` must now contain the host
 * representation of `struct ifreq`.
 */
abi_long dev_ethtool(int fd, uint8_t *buf_temp);

#endif /* ETHTOOL_H */
