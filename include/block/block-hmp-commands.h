#ifndef BLOCK_HMP_COMMANDS_H
#define BLOCK_HMP_COMMANDS_H

/* HMP commands related to the block layer*/

void hmp_drive_add(Monitor *mon, const QDict *qdict);

void hmp_commit(Monitor *mon, const QDict *qdict);
void hmp_drive_del(Monitor *mon, const QDict *qdict);

#endif
