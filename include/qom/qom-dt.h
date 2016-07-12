/*
 * Device Tree (DeviceClass hierarchy)
 *
 * Copyright ISP RAS, 2016
 *
 * Created on: Jul 6, 2016
 *     Author: Oleg Goremykin <goremykin@ispras.ru>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef INCLUDE_QOM_QOM_DT_H_
#define INCLUDE_QOM_QOM_DT_H_

/**
 * dt_printf: writes Device Tree (DiveceClass hierarchy) to @file_name
 * @file_name: output file name
 */
int dt_printf(const char *file_name);

#endif /* INCLUDE_QOM_QOM_DT_H_ */
