#ifndef QOS_EXTERNAL_H
#define QOS_EXTERNAL_H

void apply_to_node(const char *name, bool is_machine, bool is_abstract);
void apply_to_qlist(QList *list, bool is_machine);
QGuestAllocator *get_machine_allocator(QOSGraphObject *obj);
void *allocate_objects(QTestState *qts, char **path, QGuestAllocator **p_alloc);
#endif
