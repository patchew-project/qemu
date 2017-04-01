use libc::{c_char,c_int,c_uint,c_ulong,c_void,size_t};


const BLOCK_OP_TYPE_MAX: usize = 16;


#[repr(C)]
pub struct BlockDriver {
    pub format_name: *const c_char,
    pub instance_size: c_int,

    pub is_filter: bool,

    pub bdrv_recurse_is_first_non_filter: Option<
            extern fn(bs: *mut BlockDriverState,
                      candidate: *mut BlockDriverState)
                -> bool
        >,

    pub bdrv_probe: Option<
            extern fn(buf: *const c_char, buf_size: c_int,
                      filename: *const c_char)
                -> c_int
        >,
    pub bdrv_probe_device: Option<extern fn(filename: *const c_char) -> c_int>,

    pub bdrv_parse_filename: Option<
            extern fn(filename: *const c_char,
                      options: *mut QDict,
                      errp: *mut *mut Error)
        >,
    pub bdrv_needs_filename: bool,

    pub supports_backing: bool,

    pub bdrv_reopen_prepare: Option<
            extern fn(reopen_state: *mut BDRVReopenState,
                      queue: *mut BlockReopenQueue,
                      errp: *mut *mut Error)
                -> c_int
        >,
    pub bdrv_reopen_commit: Option<
            extern fn(reopen_state: *mut BDRVReopenState)
        >,
    pub bdrv_reopen_abort: Option<
            extern fn(reopen_state: *mut BDRVReopenState)
        >,
    pub bdrv_join_options: Option<
            extern fn(options: *mut QDict, old_options: *mut QDict)
        >,

    pub bdrv_open: Option<
            extern fn(bs: *mut BlockDriverState, options: *mut QDict,
                      flags: c_int, errp: *mut *mut Error)
                -> c_int
        >,
    pub bdrv_file_open: Option<
            extern fn(bs: *mut BlockDriverState, options: *mut QDict,
                      flags: c_int, errp: *mut *mut Error)
                -> c_int
        >,
    pub bdrv_close: Option<extern fn(bs: *mut BlockDriverState)>,
    pub bdrv_create: Option<
            extern fn(filename: *const c_char, opts: *mut QemuOpts,
                      errp: *mut *mut Error)
                -> c_int
        >,
    pub bdrv_set_key: Option<DeprecatedFn>,
    pub bdrv_make_empty: Option<extern fn(bs: *mut BlockDriverState)>,

    pub bdrv_refresh_filename: Option<
            extern fn(bs: *mut BlockDriverState, options: *mut QDict)
        >,

    pub bdrv_aio_readv: Option<DeprecatedFn>,
    pub bdrv_aio_writev: Option<DeprecatedFn>,
    pub bdrv_aio_flush: Option<DeprecatedFn>,
    pub bdrv_aio_pdiscard: Option<DeprecatedFn>,

    pub bdrv_co_readv: Option<DeprecatedFn>,
    pub bdrv_co_preadv: Option<
            extern fn(bs: *mut BlockDriverState, offset: u64, bytes: u64,
                      qiov: *mut QEMUIOVector, flags: c_int)
                -> c_int
        >,
    pub bdrv_co_writev: Option<DeprecatedFn>,
    pub bdrv_co_writev_flags: Option<DeprecatedFn>,
    pub bdrv_co_pwritev: Option<
            extern fn(bs: *mut BlockDriverState, offset: u64, bytes: u64,
                      qiov: *mut QEMUIOVector, flags: c_int)
                -> c_int
        >,

    pub bdrv_co_pwrite_zeroes: Option<
            extern fn(bs: *mut BlockDriverState, offset: i64, count: c_int,
                      flags: c_int /* BdrvRequestFlags */)
                -> c_int
        >,
    pub bdrv_co_pdiscard: Option<
            extern fn(bs: *mut BlockDriverState, offset: i64, count: c_int)
                -> c_int
        >,
    pub bdrv_co_get_block_status: Option<
            extern fn(bs: *mut BlockDriverState, sector_num: i64,
                      nb_sectors: c_int, pnum: *mut c_int,
                      file: *mut *mut BlockDriverState)
                -> i64
        >,

    pub bdrv_invalidate_cache: Option<
            extern fn(bs: *mut BlockDriverState, errp: *mut *mut Error)
        >,
    pub bdrv_inactivate: Option<extern fn(bs: *mut BlockDriverState) -> c_int>,

    pub bdrv_co_flush: Option<extern fn(bs: *mut BlockDriverState) -> c_int>,

    pub bdrv_co_flush_to_disk: Option<
            extern fn(bs: *mut BlockDriverState) -> c_int
        >,

    pub bdrv_co_flush_to_os: Option<
            extern fn(bs: *mut BlockDriverState) -> c_int
        >,

    pub protocol_name: *const c_char,
    pub bdrv_truncate: Option<
            extern fn(bs: *mut BlockDriverState, offset: i64,
                      errp: *mut *mut Error)
                -> c_int
        >,

    pub bdrv_getlength: Option<extern fn(bs: *mut BlockDriverState) -> c_int>,
    pub has_variable_length: bool,
    pub bdrv_get_allocated_file_size: Option<
            extern fn(bs: *mut BlockDriverState) -> i64
        >,

    pub bdrv_co_pwritev_compressed: Option<
            extern fn(bs: *mut BlockDriverState, offset: u64, bytes: u64,
                      qiov: *mut QEMUIOVector)
                -> c_int
        >,

    pub bdrv_snapshot_create: Option<
            extern fn(bs: *mut BlockDriverState, sn_info: *mut QEMUSnapshotInfo)
                -> c_int
        >,
    pub bdrv_snapshot_goto: Option<
            extern fn(bs: *mut BlockDriverState, snapshot_id: *const c_char)
                -> c_int
        >,
    pub bdrv_snapshot_delete: Option<
            extern fn(bs: *mut BlockDriverState, snapshot_id: *const c_char,
                      name: *const c_char, errp: *mut *mut Error)
                -> c_int
        >,
    pub bdrv_snapshot_list: Option<
            extern fn(bs: *mut BlockDriverState,
                      psn_info: *mut *mut QEMUSnapshotInfo)
                -> c_int
        >,
    pub bdrv_snapshot_load_tmp: Option<
            extern fn(bs: *mut BlockDriverState, snapshot_id: *const c_char,
                      name: *const c_char, errp: *mut *mut Error)
                -> c_int
        >,

    pub bdrv_get_info: Option<
            extern fn(bs: *mut BlockDriverState, bdi: *mut BlockDriverInfo)
                -> c_int
        >,
    /* Note that the return object should be allocated in the C program */
    pub bdrv_get_specific_info: Option<
            extern fn(bs: *mut BlockDriverState) -> *mut ImageInfoSpecific
        >,

    pub bdrv_save_vmstate: Option<
            extern fn(bs: *mut BlockDriverState, qiov: *mut QEMUIOVector,
                      pos: i64)
                -> c_int
        >,
    pub bdrv_load_vmstate: Option<
            extern fn(bs: *mut BlockDriverState, qiov: *mut QEMUIOVector,
                      pos: i64)
                -> c_int
        >,

    pub bdrv_change_backing_file: Option<
            extern fn(bs: *mut BlockDriverState,
                      backing_file: *const c_char, backing_fmt: *const c_char)
                -> c_int
        >,

    pub bdrv_is_inserted: Option<extern fn(bs: *mut BlockDriverState) -> bool>,
    pub bdrv_media_changed: Option<
            extern fn(bs: *mut BlockDriverState) -> c_int
        >,
    pub bdrv_eject: Option<
            extern fn(bs: *mut BlockDriverState, eject_flag: bool)
        >,
    pub bdrv_lock_medium: Option<
            extern fn(bs: *mut BlockDriverState, locked: bool)
        >,

    pub bdrv_aio_ioctl: Option<DeprecatedFn>,
    pub bdrv_co_ioctl: Option<
            extern fn(bs: *mut BlockDriverState, req: c_ulong, buf: *mut c_void)
                -> c_int
        >,

    pub create_opts: *mut QemuOptsList,

    pub bdrv_check: Option<
            extern fn(bs: *mut BlockDriverState, result: *mut BdrvCheckResult,
                      fix: c_int /* BdrvCheckResult */)
                -> c_int
        >,

    pub bdrv_amend_options: Option<
            extern fn(bs: *mut BlockDriverState, opts: *mut QemuOpts,
                      status_cb: BlockDriverAmendStatusCB,
                      cb_opaque: *mut c_void)
                -> c_int
        >,

    pub bdrv_debug_event: Option<
            extern fn(bs: *mut BlockDriverState,
                      event: c_int /* BlkdebugEvent */)
        >,

    pub bdrv_debug_breakpoint: Option<
            extern fn(bs: *mut BlockDriverState, event: *const c_char,
                      tag: *const c_char)
                -> c_int
        >,
    pub bdrv_debug_remove_breakpoint: Option<
            extern fn(bs: *mut BlockDriverState, tag: *const c_char) -> c_int
        >,
    pub bdrv_debug_resume: Option<
            extern fn(bs: *mut BlockDriverState, tag: *const c_char) -> c_int
        >,
    pub bdrv_debug_is_suspended: Option<
            extern fn(bs: *mut BlockDriverState, tag: *const c_char) -> bool
        >,

    pub bdrv_refresh_limits: Option<
            extern fn(bs: *mut BlockDriverState, errp: *mut *mut Error)
        >,

    pub bdrv_has_zero_init: Option<
            extern fn(bs: *mut BlockDriverState) -> c_int
        >,

    pub bdrv_detach_aio_context: Option<extern fn(bs: *mut BlockDriverState)>,

    pub bdrv_attach_aio_context: Option<
            extern fn(bs: *mut BlockDriverState, new_context: *mut AioContext)
        >,

    pub bdrv_io_plug: Option<extern fn(bs: *mut BlockDriverState)>,
    pub bdrv_io_unplug: Option<extern fn(bs: *mut BlockDriverState)>,

    pub bdrv_probe_blocksizes: Option<
            extern fn(bs: *mut BlockDriverState, bsz: *mut BlockSizes)
                -> c_int
        >,
    pub bdrv_probe_geometry: Option<
            extern fn(bs: *mut BlockDriverState, geo: *mut HDGeometry)
                -> c_int
        >,

    pub bdrv_drain: Option<extern fn(bs: *mut BlockDriverState)>,

    pub bdrv_add_child: Option<
            extern fn(parent: *mut BlockDriverState,
                      child: *mut BlockDriverState,
                      errp: *mut *mut Error)
        >,
    pub bdrv_del_child: Option<
            extern fn(parent: *mut BlockDriverState,
                      child: *mut BlockDriverState,
                      errp: *mut *mut Error)
        >,

    pub bdrv_check_perm: Option<
            extern fn(bs: *mut BlockDriverState, perm: u64, shared: u64,
                      errp: *mut *mut Error)
                -> c_int
        >,
    pub bdrv_set_perm: Option<
            extern fn(bs: *mut BlockDriverState, perm: u64, shared: u64)
        >,
    pub bdrv_abort_perm_update: Option<extern fn(bs: *mut BlockDriverState)>,

    pub bdrv_child_perm: Option<
            extern fn(bs: *mut BlockDriverState, c: *mut BdrvChild,
                      role: *const BdrvChildRole,
                      parent_perm: u64, parent_shared: u64,
                      nperm: *mut u64, nshared: *mut u64)
        >,

    pub list: QListEntry<BlockDriver>,
}

#[repr(C)]
pub struct BlockDriverState {
    pub open_flags: c_int,
    pub read_only: bool,
    pub encrypted: bool,
    pub valid_key: bool,
    pub sg: bool,
    pub probed: bool,

    pub drv: *mut BlockDriver,
    pub opaque: *mut c_void,

    pub aio_context: *mut AioContext,
    pub aio_notifiers: *mut BdrvAioNotifier,
    pub walking_aio_notifiers: bool,

    pub filename: [c_char; 4096],
    pub backing_file: [c_char; 4096],

    pub backing_format: [c_char; 16],

    pub full_open_options: *mut QDict,
    pub exact_filename: [c_char; 4096],

    pub backing: *mut BdrvChild,
    pub file: *mut BdrvChild,

    pub bl: BlockLimits,

    pub supported_write_flags: c_uint,
    pub supported_zero_flags: c_uint,

    pub node_name: [c_char; 32],
    pub node_list: QTailQEntry<BlockDriverState>,
    pub bs_list: QTailQEntry<BlockDriverState>,
    pub monitor_list: QTailQEntry<BlockDriverState>,
    pub refcnt: c_int,

    pub op_blockers: [QListHead<BdrvOpBlocker>; BLOCK_OP_TYPE_MAX],

    pub job: *mut BlockJob,

    pub inherits_from: *mut BlockDriverState,
    pub children: QListHead<BdrvChild>,
    pub parents: QListHead<BdrvChild>,

    pub options: *mut QDict,
    pub explicit_options: *mut QDict,
    pub detect_zeroes: c_int, /* BlockdevDetectZeroesOptions */

    pub backing_blocker: *mut Error,

    pub copy_on_read: c_int,

    pub total_sectors: i64,

    pub before_write_notifiers: NotifierWithReturnList,

    pub in_flight: c_uint,
    pub serialising_in_flight: c_uint,

    pub wakeup: bool,

    pub wr_highest_offset: u64,

    pub write_threshold_offset: u64,
    pub write_threshold_notifier: NotifierWithReturn,

    pub io_plugged: c_uint,

    pub tracked_requests: QListHead<BdrvTrackedRequest>,
    pub flush_queue: CoQueue,
    pub active_flush_req: bool,
    pub write_gen: c_uint,
    pub flushed_gen: c_uint,

    pub dirty_bitmaps: QListHead<BdrvDirtyBitmap>,

    pub enable_write_cache: c_int,

    pub quiesce_counter: c_int,
}

#[repr(C)]
pub struct BDRVReopenState {
    pub bs: *mut BlockDriverState,
    pub flags: c_int,
    pub options: *mut QDict,
    pub explicit_options: *mut QDict,
    pub opaque: *mut c_void,
}

#[repr(C)]
pub struct BdrvCheckResult {
    pub corruptions: c_int,
    pub leaks: c_int,
    pub check_errors: c_int,
    pub corruptions_fixed: c_int,
    pub leaks_fixed: c_int,
    pub image_end_offset: i64,
    pub bfi: c_int, /* BlockFragInfo */
}

#[repr(C)]
pub struct BlockSizes {
    pub phys: u32,
    pub log: u32,
}

#[repr(C)]
pub struct HDGeometry {
    pub heads: u32,
    pub sectors: u32,
    pub cylinders: u32,
}

#[repr(C)]
pub struct BlockLimits {
    pub request_alignment: u32,
    pub max_pdiscard: i32,
    pub pdiscard_alignment: u32,
    pub max_pwrite_zeroes: i32,
    pub pwrite_zeroes_alignment: u32,
    pub opt_transfer: u32,
    pub max_transfer: u32,
    pub min_mem_alignment: size_t,
    pub opt_mem_alignment: size_t,
    pub max_iov: c_int,
}

#[repr(C)]
pub struct BdrvChild {
    pub bs: *mut BlockDriverState,
    pub name: *mut c_char,
    pub role: *const BdrvChildRole,
    pub opaque: *mut c_void,

    pub perm: u64,

    pub shared_perm: u64,

    pub next: QListEntry<BdrvChild>,
    pub next_parent: QListEntry<BdrvChild>,
}

#[repr(C)]
pub struct BdrvChildRole {
    pub stay_at_node: bool,

    pub inherit_options: Option<
            extern fn(child_flags: *mut c_int, child_options: *mut QDict,
                      parent_flags: c_int, parent_options: *mut QDict)
        >,

    pub change_media: Option<extern fn(child: *mut BdrvChild, load: bool)>,
    pub resize: Option<extern fn(child: *mut BdrvChild)>,

    pub get_name: Option<extern fn(child: *mut BdrvChild) -> *const c_char>,

    /* Return value should probably be allocated in the C program */
    pub get_parent_desc: Option<
            extern fn(child: *mut BdrvChild) -> *mut c_char
        >,

    pub drained_begin: Option<extern fn(child: *mut BdrvChild)>,
    pub drained_end: Option<extern fn(child: *mut BdrvChild)>,

    pub attach: Option<extern fn(child: *mut BdrvChild)>,
    pub detach: Option<extern fn(child: *mut BdrvChild)>,
}

#[repr(C)]
pub struct BdrvAioNotifier {
    pub attached_aio_context: Option<
            extern fn(new_context: *mut AioContext, opaque: *mut c_void)
        >,
    pub detach_aio_context: Option<extern fn(opaque: *mut c_void)>,

    pub opaque: *mut c_void,
    pub deleted: bool,

    pub list: QListEntry<BdrvAioNotifier>,
}

#[repr(C)]
pub struct BlockDriverInfo {
    pub cluster_size: c_int,
    pub vm_state_offset: i64,
    pub is_dirty: bool,
    pub unallocated_blocks_are_zero: bool,
    pub can_write_zeroes_with_unmap: bool,
    pub needs_compressed_writes: bool,
}

#[repr(C)]
pub struct ImageInfoSpecific {
    pub kind: c_int, /* ImageInfoSpecificKind */
    pub date: *mut c_void, /* type depends on kind */
}

#[repr(C)]
pub struct QEMUIOVector {
    pub iov: *mut iovec,
    pub niov: c_int,
    pub nalloc: c_int,
    pub size: size_t,
}

#[repr(C)]
pub struct iovec {
    pub iov_base: *mut c_void,
    pub iov_len: size_t,
}

#[repr(C)]
pub struct QEMUSnapshotInfo {
    pub id_str: [c_char; 128],
    pub name: [c_char; 256],
    pub vm_state_size: u64,
    pub date_sec: u32,
    pub date_nsec: u32,
    pub vm_clock_nsec: u64,
}

#[repr(C)]
pub struct NotifierWithReturnList {
    pub notifiers: *mut NotifierWithReturn,
}

#[repr(C)]
pub struct NotifierWithReturn {
    pub notify: extern fn(notifier: *mut NotifierWithReturn, data: *mut c_void)
                    -> c_int,
    pub node: QListEntry<NotifierWithReturn>,
}

#[repr(C)]
pub struct CoQueue {
    pub entries: CoroutineQSimpleQHead,
}

#[repr(C)]
pub struct CoroutineQSimpleQHead {
    pub sqh_first: *mut Coroutine,
    pub sqh_last: *mut *mut Coroutine,
}

#[repr(C)]
pub struct QListHead<T> {
    pub lh_first: *mut T,
}

#[repr(C)]
pub struct QListEntry<T> {
    pub le_next: *mut T,
    pub le_prev: *mut *mut T,
}

#[repr(C)]
pub struct QTailQEntry<T> {
    pub tqe_next: *mut T,
    pub tqe_prev: *mut *mut T,
}

type BlockDriverAmendStatusCB = extern fn(bs: *mut BlockDriverState,
                                          offset: i64, total_work_size: i64,
                                          opaque: *mut c_void);

/* Opaque types */
pub enum AioContext {}
pub enum BdrvDirtyBitmap {}
pub enum BdrvOpBlocker {}
pub enum BdrvTrackedRequest {}
pub enum BlockReopenQueue {}
pub enum BlockJob {}
pub enum Coroutine {}
pub enum Error {}
pub enum QDict {}
pub enum QemuOpts {}
pub enum QemuOptsList {}

/* Used for deprecated function pointers */
type DeprecatedFn = extern fn();
