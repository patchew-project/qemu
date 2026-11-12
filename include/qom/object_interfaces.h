#ifndef OBJECT_INTERFACES_H
#define OBJECT_INTERFACES_H

#include "qom/object.h"
#include "qapi/qapi-types-qom.h"
#include "qapi/visitor.h"

#define TYPE_USER_CREATABLE "user-creatable"

typedef struct UserCreatableClass UserCreatableClass;
DECLARE_CLASS_CHECKERS(UserCreatableClass, USER_CREATABLE,
                       TYPE_USER_CREATABLE)
#define USER_CREATABLE(obj) \
     INTERFACE_CHECK(UserCreatable, (obj), \
                     TYPE_USER_CREATABLE)

typedef struct UserCreatable UserCreatable;

#define TYPE_TARGET_SPECIFIC "target-specific"

typedef struct TargetSpecificClass TargetSpecificClass;
DECLARE_CLASS_CHECKERS(TargetSpecificClass, TARGET_SPECIFIC,
                       TYPE_TARGET_SPECIFIC)
#define TARGET_SPECIFIC(obj) \
     INTERFACE_CHECK(TargetSpecific, (obj), \
                     TYPE_TARGET_SPECIFIC)

typedef struct TargetSpecific TargetSpecific;

#define TYPE_ALIASED "aliased"

typedef struct AliasedClass AliasedClass;
DECLARE_CLASS_CHECKERS(AliasedClass, ALIASED,
                       TYPE_ALIASED)
#define ALIASED(obj) \
     INTERFACE_CHECK(Aliased, (obj), \
                     TYPE_ALIASED)

typedef struct Aliased Aliased;

/**
 * UserCreatableClass:
 * @parent_class: the base class
 * @complete: callback to be called after @obj's properties are set.
 * @prepare_delete: to be called before an attempt to delete @obj
 * to validate whether the object can be deleted and trigger any
 * cleanup of any resources which have to be dealt with before the
 * object is unparented and enters finalization.
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
 * complete() callback to be called. Similarly @user_creatable_prepare_delete()
 * should be called manually prior to an attempt to the delete the
 * object.
 */
struct UserCreatableClass {
    /* <private> */
    InterfaceClass parent_class;

    /* <public> */
    void (*complete)(UserCreatable *uc, Error **errp);
    bool (*prepare_delete)(UserCreatable *uc, Error **errp);
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
 * user_creatable_prepare_delete:
 * @uc: the user-creatable object whose prepare_delete() method is called
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Wrapper to call prepare_delete() class method if defined, otherwise
 * does nothing.
 *
 * Returns: %true on success or if prepare_delete() is not defined,
 *          %false on failure.
 */
bool user_creatable_prepare_delete(UserCreatable *uc, Error **errp);

/**
 * user_creatable_add_qapi:
 * @options: the object definition
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Create an instance of the user creatable object according to the
 * options passed in @opts as described in the QAPI schema documentation.
 */
void user_creatable_add_qapi(ObjectOptions *options, Error **errp);

/**
 * user_creatable_parse_str:
 * @str: the object definition string as passed on the command line
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Parses the option for the user creatable object with a keyval parser and
 * implicit key 'qom-type', converting the result to ObjectOptions.
 *
 * If a help option is given, print help instead.
 *
 * Returns: ObjectOptions on success, NULL when an error occurred (*errp is set
 * then) or help was printed (*errp is not set).
 */
ObjectOptions *user_creatable_parse_str(const char *str, Error **errp);

/**
 * user_creatable_add_from_str:
 * @str: the object definition string as passed on the command line
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Create an instance of the user creatable object by parsing @str
 * with a keyval parser and implicit key 'qom-type', converting the
 * result to ObjectOptions and calling into qmp_object_add().
 *
 * If a help option is given, print help instead.
 *
 * Returns: true when an object was successfully created, false when an error
 * occurred (*errp is set then) or help was printed (*errp is not set).
 */
bool user_creatable_add_from_str(const char *str, Error **errp);

/**
 * user_creatable_process_cmdline:
 * @cmdline: the object definition string as passed on the command line
 *
 * Create an instance of the user creatable object by parsing @cmdline
 * with a keyval parser and implicit key 'qom-type', converting the
 * result to ObjectOptions and calling into qmp_object_add().
 *
 * If a help option is given, print help instead and exit.
 *
 * This function is only meant to be called during command line parsing.
 * It exits the process on failure or after printing help.
 */
void user_creatable_process_cmdline(const char *cmdline);

/**
 * user_creatable_print_help:
 * @type: the QOM type to be added
 * @opts: options to create
 *
 * Prints help if requested in @type or @opts. Note that if @type is neither
 * "help"/"?" nor a valid user creatable type, no help will be printed
 * regardless of @opts.
 *
 * Returns: true if a help option was found and help was printed, false
 * otherwise.
 */
bool user_creatable_print_help(const char *type, QemuOpts *opts);

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
 * TargetSpecificClass:
 * @parent_class: the base class
 *
 * Interface for classes that want to declare they have target
 * specific restrictions. This is intended to allow classes to
 * be registered and introspected unconditionally, while
 * preventing creation of instances unless a compatible target
 * is selected.
 */
struct TargetSpecificClass {
    InterfaceClass parent_class;

    /*
     * Leave as 0 to indicate no target specific creation restrictions,
     * or set to a bitmask of QEMU_ARCH_nnn constants to limit what
     * targets it can be created under.
     */
    int targets;
};


/**
 * target_specific_class_creatable:
 * @oc: the object class to check
 *
 * Determine whether @oc implements the TargetSpecific
 * interface and if so, whether its declared targets
 * allow creation in the current system state.
 *
 * If @oc does not implement the TargetSpecific interface
 * it is assumed there are no creation restrictions.
 *
 * If @oc implements the TargetSpecific interface but leaves
 * the 'targets' field as zero, it is assumed there are
 * no creation restrictions.
 *
 * If @oc implements the TargetSpecific interface and the
 * 'targets' field is non-zero, it is compared to
 * target_mask_selected() to determine if it is valid to
 * create an instance of @oc.
 *
 * Returns: true if it is valid to create an instance of @oc
 * in the current system state, false otherwise.
 */
bool target_specific_class_creatable(ObjectClass *oc);


/**
 * target_specific_class_check:
 * @oc: the object class to check
 * @errp: pointer to a NULL-initialized error object
 *
 * Check whether it is permitted to create an instance
 * of @oc in the current system target state.
 *
 * This validates @oc against target_specific_class_creatable
 * and sets @errp if it is not creatable.
 *
 * Returns: @oc if it is creatable, NULL otherwise with @errp set.
 */
ObjectClass *target_specific_class_check(ObjectClass *oc,
                                         Error **errp);

/**
 * AliasedClass
 * @parent_class: the base class
 *
 * Interface for classes that want to declare they have aliases
 * for their primary type name.
 *
 * While QOM type names are required to be unique, aliases allow
 * for ambiguity across classes. The ambiguity is resolved at
 * runtime based on system state, typically by the selection of
 * a specific target. If ambiguity cannot be resolved, the user
 * will need to use the unique QOM type name instead.
 */
struct AliasedClass {
    InterfaceClass parent_class;

    const char *const *aliases; /* NULL terminated list of typenames */
};


/**
 * aliased_class_matches:
 * @oc: the class object to check
 * @typename: the possible type name alias to check
 *
 * Determine whether @oc implements the Aliased interface
 * and has declared an alias that matches @typename.
 *
 * Returns: true if @oc implements Aliased and has an
 * alias named @typename, false otherwise
 */
bool aliased_class_matches(ObjectClass *oc,
                           const char *typename);


/**
 * aliased_class_resolve:
 * @implements: a parent type to validate
 * @typename: a type name to search for
 * @errp: pointer to a NULL initialized error object
 *
 * Search all registered QOM types that implement
 * both @implements and the Aliased interface for
 * one that declares an alias @typename.
 *
 * If there is a single QOM class with a matching alias
 * it will be returned.
 *
 * If there are multiple QOM classes with a matching
 * alias an error will be raised about the ambiguous
 * match.
 *
 * If the QOM class also implements the TargetSpecific
 * interface and the current system target does not
 * match the declared supported targets, aliases for
 * that QOM class will be ignored. This is usually
 * sufficient to resolve ambiguity between types.
 *
 * Returns: a unique QOM class with an alias @typename,
 * or NULL if zero or multiple aliases match, with @errp
 * set.
 */
 ObjectClass *aliased_class_resolve(const char *implements,
                                    const char *typename,
                                    Error **errp);

#endif
