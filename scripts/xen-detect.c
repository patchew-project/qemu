/* SPDX-License-Identifier: GPL-2.0-or-later */

/* Test programs for Xen 4.11.0+.  */
#undef XC_WANT_COMPAT_DEVICEMODEL_API
#define __XEN_TOOLS__
#include <xendevicemodel.h>
#include <xenforeignmemory.h>
int main(void) {
  xendevicemodel_handle *xd;
  xenforeignmemory_handle *xfmem;

  xd = xendevicemodel_open(0, 0);
  xendevicemodel_pin_memory_cacheattr(xd, 0, 0, 0, 0);

  xfmem = xenforeignmemory_open(0, 0);
  xenforeignmemory_map_resource(xfmem, 0, 0, 0, 0, 0, NULL, 0, 0);

  return 0;
}
