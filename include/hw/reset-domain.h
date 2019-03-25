#ifndef HW_RESET_DOMAIN_H
#define HW_RESET_DOMAIN_H

#include "resettable.h"
#include "qemu/queue.h"

#define TYPE_RESET_DOMAIN "reset-domain"

#define RESET_DOMAIN(obj) OBJECT_CHECK(ResetDomain, (obj), TYPE_RESET_DOMAIN)

/**
 * ResetDomainClass:
 * A ResetDomain holds several Resettable objects and implement the Resettable
 * interface too.
 * Doing a reset on it will also reset all objects it contained. Phases of
 * every object will be executed in order: init_reset of all objects first, etc.
 */
typedef ObjectClass ResetDomainClass;

/**
 * ResetDomain:
 * @members is a list of ResetDomainEntry. Every entry hold a pointer to a
 * Resettable object.
 * To avoid object to disapear while in the ResetDomain, the ResetDomain
 * increases the refcount.
 */
struct ResetDomainEntry {
    Object *obj;
    QLIST_ENTRY(ResetDomainEntry) node;
};
typedef struct ResetDomain {
    Object parent_obj;

    QLIST_HEAD(, ResetDomainEntry) members;
} ResetDomain;

/**
 * reset_domain_register_objet:
 * Register the Resettable object @obj into a ResetDomain @domain.
 */
void reset_domain_register_object(ResetDomain *domain, Object *obj);

/**
 * reset_domain_unregister_objet:
 * Unregister the Resettable object @obj into a ResetDomain @domain.
 */
void reset_domain_unregister_object(ResetDomain *domain, Object *obj);

#endif
