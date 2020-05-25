
#ifndef YANK_H
#define YANK_H

typedef void (YankFn) (void *opaque);

/**
 * yank_register_instance: Register a new instance.
 *
 * This registers a new instance for yanking. Must be called before any yank
 * function is registered for this instance.
 *
 * This function is thread-safe.
 *
 * @instance_name: The globally unique name of the instance.
 */
void yank_register_instance(char *instance_name);

/**
 * yank_unregister_instance: Unregister a instance.
 *
 * This unregisters a instance. Must be called only after every yank function
 * of the instance has been unregistered.
 *
 * This function is thread-safe.
 *
 * @instance_name: The name of the instance.
 */
void yank_unregister_instance(char *instance_name);

/**
 * yank_register_function: Register a yank function
 *
 * This registers a yank function. All limitations of qmp oob commands apply
 * to the yank function as well.
 *
 * This function is thread-safe.
 *
 * @instance_name: The name of the instance
 * @func: The yank function
 * @opaque: Will be passed to the yank function
 */
void yank_register_function(char *instance_name, YankFn *func, void *opaque);

/**
 * yank_unregister_function: Unregister a yank function
 *
 * This unregisters a yank function.
 *
 * This function is thread-safe.
 *
 * @instance_name: The name of the instance
 * @func: func that was passed to yank_register_function
 * @opaque: opaque that was passed to yank_register_function
 */
void yank_unregister_function(char *instance_name, YankFn *func, void *opaque);

/**
 * yank_unregister_function: Generic yank function for iochannel
 *
 * This is a generic yank function which will call qio_channel_shutdown on the
 * provided QIOChannel.
 *
 * @opaque: QIOChannel to shutdown
 */
void yank_generic_iochannel(void *opaque);
#endif
