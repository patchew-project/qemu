#include "block/block_int.h"

typedef struct CowRequest {
    int64_t start;
    int64_t end;
    QLIST_ENTRY(CowRequest) list;
    CoQueue wait_queue; /* coroutines blocked on this request */
} CowRequest;

void backup_wait_for_overlapping_requests(BlockJob *job, int64_t sector_num,
                                          int nb_sectors);
void backup_cow_request_begin(CowRequest *req, BlockJob *job,
                              int64_t sector_num,
                              int nb_sectors);
void backup_cow_request_end(CowRequest *req);

void backup_do_checkpoint(BlockJob *job, Error **errp);
