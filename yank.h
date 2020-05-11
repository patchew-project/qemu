
#ifndef YANK_H
#define YANK_H

typedef void (YankFn) (void *opaque);

void yank_register_function(YankFn *func, void *opaque);
void yank_unregister_function(YankFn *func, void *opaque);
void yank_call_functions(void);
void yank_init(void);
void qmp_yank(Error **errp);
#endif
