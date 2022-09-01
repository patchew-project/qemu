#ifndef PLUGIN_QPP_H
#define PLUGIN_QPP_H

/*
 * Facilities for "QEMU plugin to plugin" (QPP) interactions between tcg
 * plugins.  These allow for an inter-plugin callback system as well
 * as direct function calls between loaded plugins. For more details see
 * docs/devel/plugin.rst.
 */

#include <dlfcn.h>
#include <gmodule.h>
#include <assert.h>
extern GModule * qemu_plugin_name_to_handle(const char *);

#define PLUGIN_CONCAT(x, y) _PLUGIN_CONCAT(x, y)
#define _PLUGIN_CONCAT(x, y) x##y
#define QPP_NAME(plugin, fn) PLUGIN_CONCAT(plugin, PLUGIN_CONCAT(_, fn))
#define QPP_MAX_CB 256
#define _QPP_SETUP_NAME(plugin, fn) PLUGIN_CONCAT(_qpp_setup_, \
                                    QPP_NAME(plugin, fn))

/*
 **************************************************************************
 * The QPP_CREATE_CB and QPP_RUN_CB macros are to be used by a plugin that
 * wishs to create and later trigger QPP-based callback events. These are
 * events that the plugin can detect (i.e., through analysis of guest state)
 * that may be of interest to other plugins.
 **************************************************************************
 */

/*
 * QPP_CREATE_CB(name) will create a callback by defining necessary internal
 * functions and variables based off the provided name. It must be run before
 * triggering the callback event (with QPP_RUN_CB). This macro will create the
 * following variables and functions, based off the provided name:
 *
 * 1) qpp_[name]_cb is an array of function pointers storing the
 *    registered callbacks.
 * 2) qpp_[name]_num_cb stores the number of functions stored with this
 *    callback.
 * 3) qpp_add_cb_[name] is a function to add a pointer into the qpp_[name]_cb
 *    array and increment qpp_[name]_num_cb.
 * 4) qpp_remove_cb_[name] finds a registered callback, deletes it, decrements
 *    _num_cb and shifts the _cb array appropriately to fill the gap.
 *
 * Important notes:
 *
 * 1) Multiple callbacks can be registered for the same event, however consumers
 *    can not control the order in which they are called and this order may
 *    change in the future.
 *
 * 2) If this macro is incorrectly used multiple times in the same plugin with
 *    the same callback name set, it will raise a compilation error since
 *    these variables would then be defined multiple times. The same callback
 *    name can, however, be created in distrinct plugins without issue.
 */
#define QPP_CREATE_CB(cb_name)                              \
void qpp_add_cb_##cb_name(cb_name##_t fptr);                \
bool qpp_remove_cb_##cb_name(cb_name##_t fptr);             \
cb_name##_t * qpp_##cb_name##_cb[QPP_MAX_CB];               \
int qpp_##cb_name##_num_cb;                                 \
                                                            \
void qpp_add_cb_##cb_name(cb_name##_t fptr)                 \
{                                                           \
  assert(qpp_##cb_name##_num_cb < QPP_MAX_CB);              \
  qpp_##cb_name##_cb[qpp_##cb_name##_num_cb] = fptr;        \
  qpp_##cb_name##_num_cb += 1;                              \
}                                                           \
                                                            \
bool qpp_remove_cb_##cb_name(cb_name##_t fptr)              \
{                                                           \
  int i = 0;                                                \
  bool found = false;                                       \
  for (; i < MIN(QPP_MAX_CB, qpp_##cb_name##_num_cb); i++) {\
    if (!found && fptr == qpp_##cb_name##_cb[i]) {          \
        found = true;                                       \
        qpp_##cb_name##_num_cb--;                           \
    }                                                       \
    if (found && i < QPP_MAX_CB - 2) {                      \
        qpp_##cb_name##_cb[i] = qpp_##cb_name##_cb[i + 1];  \
    }                                                       \
  }                                                         \
  return found;                                             \
}


/*
 * QPP_RUN_CB(name, args...) should be run by the plugin that created
 * a callback whenever it wishes to trigger the callback functions that
 * have been registered with its previously-created callback of the provided
 * name. If this macro is run with a callback name that was not previously
 * created, a compile time error will be raised.
 */
#define QPP_RUN_CB(cb_name, ...) do {                           \
  int cb_idx;                                                   \
  for (cb_idx = 0; cb_idx < qpp_##cb_name##_num_cb; cb_idx++) { \
    if (qpp_##cb_name##_cb[cb_idx] != NULL) {                   \
      qpp_##cb_name##_cb[cb_idx](__VA_ARGS__);                  \
    }                                                           \
  }                                                             \
} while (false)

/*
 * A header file that defines a callback function should use
 * the QPP_CB_PROTOTYPE macro to create the necessary types.
 */
#define QPP_CB_PROTOTYPE(fn_ret, name, ...) \
  typedef fn_ret(PLUGIN_CONCAT(name, _t))(__VA_ARGS__);

/*
 *****************************************************************************
 * The QPP_REG_CB and QPP_REMOVE_CB macros are to be used by plugins that
 * wish to run internal logic when another plugin determines that some event
 * has occured. The plugin name, target callback name, and a local function
 * are provided to these macros.
 *****************************************************************************
 */

/*
 * When a plugin wishes to register a function `cb_func` with a callback
 * `cb_name` provided `other_plugin`, it must use the QPP_REG_CB.
 */
#define QPP_REG_CB(other_plugin, cb_name, cb_func)      \
{                                                       \
  dlerror();                                            \
  void *h = qemu_plugin_name_to_handle(other_plugin);   \
  if (!h) {                                             \
    fprintf(stderr, "In trying to add plugin callback, "\
                    "couldn't load %s plugin\n",        \
                    other_plugin);                      \
    assert(h);                                          \
  }                                                     \
  void (*add_cb)(cb_name##_t fptr);                     \
  if (g_module_symbol(h, "qpp_add_cb_" #cb_name,        \
                      (gpointer *) &add_cb)) {          \
    add_cb(cb_func);                                    \
  } else {                                              \
    fprintf(stderr, "Could not find symbol " #cb_name   \
            " in " #other_plugin "\n");                 \
  }                                                     \
}

/*
 * If a plugin wishes to disable a previously-registered `cb_func` it should
 * use the QPP_REMOVE_CB macro.
 */
#define QPP_REMOVE_CB(other_plugin, cb_name, cb_func)            \
{                                                                \
  dlerror();                                                     \
  void *op = panda_get_plugin_by_name(other_plugin);             \
  if (!op) {                                                     \
    fprintf(stderr, "In trying to remove plugin callback, "      \
                    "couldn't load %s plugin\n", other_plugin);  \
    assert(op);                                                  \
  }                                                              \
  void (*rm_cb)(cb_name##_t fptr) = (void (*)(cb_name##_t))      \
                                    dlsym(op, "qpp_remove_cb_"   \
                                              #cb_name);         \
  assert(rm_cb != 0);                                            \
  rm_cb(cb_func);                                                \
}

/*
 *****************************************************************************
 * The QPP_FUN_PROTOTYPE enables a plugin to expose a local function to other
 * plugins. The macro should be used in a header file that is included
 * by both the plugin that defines the function as well as other plugins
 * that wish to call the function.
 *****************************************************************************
 */

/*
 * A header file that defines an exported function should use the
 * QPP_FUN_PROTOTYPE to create the necessary types. When other plugins
 * include this header, a function pointer named `[plugin_name]_[fn]` will
 * be created and available for the plugin to call.
 *
 * Note that these functions are not callbacks, but instead regular
 * functions that are exported by one plugin such that they can be called by
 * others.
 *
 * In particular, this macro will create a function pointer by combining the
 * `plugin_name` with an underscore and the name provided in `fn`. It will
 * create a function to run on plugin load, that initializes this function
 * pointer to the function named `fn` inside the plugin named `plugin_name`.
 * If this fails, an error will be printed and the plugin will abort.
 *
 * There are three distinct cases this macro must handle:
 * 1) When the header is loaded by the plugin that defines the function.
 *    In this case, we do not need to find the symbol externally.
 *    qemu_plugin_name_to_handle will return NULL, we see that the
 *    target plugin matches CURRENT_PLUGIN and do nothing.
 * 2) When the header is loaded by another plugin but the function
 *    is not available (i.e., the target plugin isn't loaded or the
 *    target plugin does not export the requested function). In this case we
 *    raise a fatal error.
 * 3) When the header is loaded by another plugin. In this case
 *    we need to get a handle to the target plugin and then use
 *    g_module_symbol to resolve the symbol and store it as the fn_name.
 *    If we get the handle, but can't resolve the symbol, raise a fatal error.
 *
 * This plugin creates the following local variables and functions:
 *
 * 1) `fn`: A prototype for the provided function, with the specified arguments.
 *    This is necessary for case 1 above and irrelevant for the others.
 * 2) A function pointer typedef for `[fn]_t` set to a pointer of the type of
 *    `fn`.
 * 3) A function pointer (of type `[fn]_t`) named `[plugin_name]_[fn]`
 * 4) A constructor function named "_qpp_setup_[plugin_name]_[fn]" that will
 *    run when the plugin is loaded. In case 1 above, it will do nothing. In
 *    case 2 it will print an error to stderr and abort. In case 3 it will
 *    update the function pointer "[plugin_name]_[fn]" to point to the target
 *    function.
 *
 * After this constructor runs, the plugin can directly call the target function
 * using `[plugin_name]_[fn]()`.
 *
 * For example, consider a plugin named "my_plugin" that wishes to export a
 * function  named "my_func" that returns void and takes a single integer arg.
 * This would work as follows:
 * 1) The header file for the plugin, my_plugin.h, should include
 *    QPP_FUN_PROTOTYPE(my_plugin, void, my_func, int) which will define the
 *    function prototype and necessary internal state.
 * 2) This header should be included by my_plugin.c which should also include
 *    QEMU_PLUGIN_EXPORT void my_func(int) {...} with the function definition.
 * 3) Other plugins can get access to this function by including my_plugin.h
 *    which will set up the function pointer `my_plugin_my_func` automatically
 *    using the internal state here.
 *
 */
#define QPP_FUN_PROTOTYPE(plugin_name, fn_ret, fn, args)                      \
  fn_ret fn(args);                                                            \
  typedef fn_ret(*PLUGIN_CONCAT(fn, _t))(args);                               \
  fn##_t QPP_NAME(plugin_name, fn);                                           \
  void _QPP_SETUP_NAME(plugin_name, fn) (void);                               \
                                                                              \
  void __attribute__ ((constructor)) _QPP_SETUP_NAME(plugin_name, fn) (void) {\
    GModule *h = qemu_plugin_name_to_handle(#plugin_name);                    \
    if (!h && strcmp(CURRENT_PLUGIN, #plugin_name) != 0) {        \
      fprintf(stderr, "Error plugin %s cannot access %s. Is it loaded?\n",    \
                       CURRENT_PLUGIN, #plugin_name);             \
      abort();                                                                \
    } else if (h && !g_module_symbol(h, #fn,                                  \
                           (gpointer *)&QPP_NAME(plugin_name, fn))) {         \
      fprintf(stderr, "Error loading symbol " # fn " from plugin "            \
                      # plugin_name "\n");                                    \
      abort();                                                                \
    }                                                                         \
  }

#endif /* PLUGIN_QPP_H */
