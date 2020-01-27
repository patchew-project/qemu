#ifndef BLOCK_HMP_COMMANDS_H
#define BLOCK_HMP_COMMANDS_H

/* HMP commands related to the block layer*/

void hmp_drive_add(Monitor *mon, const QDict *qdict);

void hmp_commit(Monitor *mon, const QDict *qdict);
void hmp_drive_del(Monitor *mon, const QDict *qdict);

void hmp_drive_mirror(Monitor *mon, const QDict *qdict);
void hmp_drive_backup(Monitor *mon, const QDict *qdict);

void hmp_block_job_set_speed(Monitor *mon, const QDict *qdict);
void hmp_block_job_cancel(Monitor *mon, const QDict *qdict);
void hmp_block_job_pause(Monitor *mon, const QDict *qdict);
void hmp_block_job_resume(Monitor *mon, const QDict *qdict);
void hmp_block_job_complete(Monitor *mon, const QDict *qdict);

#endif
