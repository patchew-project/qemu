#ifndef QEMU_SYSEMU_RESET_H
#define QEMU_SYSEMU_RESET_H

#include "qom/object.h"
#include "hw/reset-domain.h"

/**
 * qemu_get_system_reset_domain:
 * Get the global system reset domain object
 */
ResetDomain *qemu_get_system_reset_domain(void);

/**
 * qemu_delete_system_reset_domain:
 * Delete the global system reset domain object
 */
void qemu_delete_system_reset_domain(void);

/**
 * qemu_register_system_reset_domain_object:
 * Register @obj in the system reset domain
 */
void qemu_register_system_reset_domain_object(Object *obj);

/**
 * qemu_unregister_system_reset_domain_object:
 * Unregister @obj from the global reset domain
 */
void qemu_unregister_system_reset_domain_object(Object *obj);

/**
 * @qemu_system_reset_domain_reset:
 * Do a cold or warm system reset based on @cold
 */
void qemu_system_reset_domain_reset(bool cold);

typedef void QEMUResetHandler(void *opaque);

/**
 * qemu_resgiter_reset:
 * Register @func with @opaque in the global reset procedure.
 */
void qemu_register_reset(QEMUResetHandler *func, void *opaque);

/**
 * qemu_unregister_reset:
 * Unregister @func with @opaque from the global reset procedure.
 */
void qemu_unregister_reset(QEMUResetHandler *func, void *opaque);

/**
 * qemu_devices_reset:
 * Trigger a reset of registered handlers and objects.
 */
void qemu_devices_reset(void);

#endif
