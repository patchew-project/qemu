#ifndef FD_TRANS_TYPE_H
#define FD_TRANS_TYPE_H

/*
 * Break out the TargetFdTrans typedefs into a separate file, to break
 * the circular dependency between qemu.h and fd-trans.h.
 */

typedef abi_long (*TargetFdDataFunc)(void *, size_t);
typedef abi_long (*TargetFdAddrFunc)(void *, abi_ulong, socklen_t);
typedef struct TargetFdTrans {
    TargetFdDataFunc host_to_target_data;
    TargetFdDataFunc target_to_host_data;
    TargetFdAddrFunc target_to_host_addr;
} TargetFdTrans;

#endif /* FD_TRANS_TYPE_H */
