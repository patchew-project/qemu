#ifndef OBJECT_INTERFACES_H
#define OBJECT_INTERFACES_H

#include "qom/object.h"
#include "qapi/visitor.h"

#define TYPE_USER_CREATABLE "user-creatable"

typedef struct UserCreatableClass UserCreatableClass;
DECLARE_CLASS_CHECKERS(UserCreatableClass, USER_CREATABLE,
                       TYPE_USER_CREATABLE)
#define USER_CREATABLE(obj) \
     INTERFACE_CHECK(UserCreatable, (obj), \
                     TYPE_USER_CREATABLE)

typedef struct UserCreatable UserCreatable;

/**
 * UserCreatableClass:
 * @parent_class: the base class
 * @complete: callback to be called after @obj's properties are set.
 * @can_be_deleted: callback to be called before an object is removed
 * to check if @obj can be removed safely.
 *
 * Interface is designed to work with -object/object-add/object_add
 * commands.
 * Interface is mandatory for objects that are designed to be user
 * creatable (i.e. -object/object-add/object_add, will accept only
 * objects that inherit this interface).
 *
 * Interface also provides an optional ability to do the second
 * stage * initialization of the object after its properties were
 * set.
 *
 * For objects created without using -object/object-add/object_add,
 * @user_creatable_complete() wrapper should be called manually if
 * object's type implements USER_CREATABLE interface and needs
 * complete() callback to be called.
 */
struct UserCreatableClass {
    /* <private> */
    InterfaceClass parent_class;

    /* <public> */
    void (*complete)(UserCreatable *uc, Error **errp);
    bool (*can_be_deleted)(UserCreatable *uc);
};

/**
 * user_creatable_complete:
 * @uc: the user-creatable object whose complete() method is called if defined
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Wrapper to call complete() method if one of types it's inherited
 * from implements USER_CREATABLE interface, otherwise the call does
 * nothing.
 *
 * Returns: %true on success, %false on failure.
 */
bool user_creatable_complete(UserCreatable *uc, Error **errp);

/**
 * user_creatable_can_be_deleted:
 * @uc: the object whose can_be_deleted() method is called if implemented
 *
 * Wrapper to call can_be_deleted() method if one of types it's inherited
 * from implements USER_CREATABLE interface.
 */
bool user_creatable_can_be_deleted(UserCreatable *uc);

/**
 * user_creatable_add_type:
 * @type: the object type name
 * @id: the unique ID for the object
 * @qdict: the object properties
 * @v: the visitor
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Create an instance of the user creatable object @type, placing
 * it in the object composition tree with name @id, initializing
 * it with properties from @qdict
 *
 * Returns: the newly created object or NULL on error
 */
Object *user_creatable_add_type(const char *type, const char *id,
                                const QDict *qdict,
                                Visitor *v, Error **errp);

/**
 * user_creatable_add_dict:
 * @qdict: the object definition
 * @keyval: if true, use a keyval visitor for processing @qdict (i.e.
 *          assume that all @qdict values are strings); otherwise, use
 *          the normal QObject visitor (i.e. assume all @qdict values
 *          have the QType expected by the QOM object type)
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Create an instance of the user creatable object that is defined by
 * @qdict.  The object type is taken from the QDict key 'qom-type', its
 * ID from the key 'id'. The remaining entries in @qdict are used to
 * initialize the object properties.
 *
 * Returns: %true on success, %false on failure.
 */
bool user_creatable_add_dict(const QDict *qdict, bool keyval, Error **errp);

/**
 * user_creatable_print_types:
 *
 * Prints a list of user-creatable objects to stdout or the monitor.
 */
void user_creatable_print_types(void);

/**
 * user_creatable_print_help_from_qdict:
 * @args: options to create
 *
 * Prints help considering the other options given in @args (if "qom-type" is
 * given and valid, print properties for the type, otherwise print valid types).
 */
void user_creatable_print_help_from_qdict(const QDict *args);

/**
 * user_creatable_del:
 * @id: the unique ID for the object
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Delete an instance of the user creatable object identified
 * by @id.
 *
 * Returns: %true on success, %false on failure.
 */
bool user_creatable_del(const char *id, Error **errp);

/**
 * user_creatable_cleanup:
 *
 * Delete all user-creatable objects and the user-creatable
 * objects container.
 */
void user_creatable_cleanup(void);

/**
 * qmp_object_add:
 *
 * QMP command handler for object-add. See the QAPI schema for documentation.
 */
void qmp_object_add(QDict *qdict, QObject **ret_data, Error **errp);

#endif
