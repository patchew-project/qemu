#ifndef QEMU_PR_HELPER_H
#define QEMU_PR_HELPER_H 1

#define PR_HELPER_CDB_SIZE     16
#define PR_HELPER_SENSE_SIZE   96
#define PR_HELPER_DATA_SIZE    8192

typedef struct PRHelperResponse {
    int32_t result;
    uint8_t sense[PR_HELPER_SENSE_SIZE];
} PRHelperResponse;

#endif
