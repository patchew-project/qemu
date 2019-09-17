/*
 * vhost-user sim main application header file
 *
 * Copyright (c) 2019 Intel Corporation. All rights reserved.
 *
 * Author:
 *  Johannes Berg <johannes.berg@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 only.
 * See the COPYING file in the top-level directory.
 */
#ifndef _SIM_MAIN_H
#define _SIM_MAIN_H

gboolean simtime_client_connected(GIOChannel *src,
                                  GIOCondition cond,
                                  gpointer data);
gboolean vu_net_client_connected(GIOChannel *src,
                                 GIOCondition cond,
                                 gpointer data);

#endif /* _SIM_MAIN_H */
