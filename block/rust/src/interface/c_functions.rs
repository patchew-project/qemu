use interface::c_structs::*;
use libc::{c_char,c_int,c_void,size_t};


extern {
    pub fn bdrv_is_read_only(bs: *mut BlockDriverState) -> bool;

    pub fn bdrv_open_child(filename: *const c_char, options: *mut QDict,
                           bdref_key: *const c_char,
                           parent: *mut BlockDriverState,
                           child_role: *const BdrvChildRole,
                           allow_none: bool, errp: *mut *mut Error)
        -> *mut BdrvChild;

    pub fn bdrv_pread(child: *mut BdrvChild, offset: i64, buf: *mut c_void,
                      bytes: c_int)
        -> c_int;

    pub fn bdrv_pwrite(child: *mut BdrvChild, offset: i64, buf: *const c_void,
                       bytes: c_int)
        -> c_int;

    pub fn bdrv_register(bdrv: *mut BlockDriver);

    pub fn error_get_pretty(err: *mut Error) -> *const c_char;

    pub fn error_setg_internal(errp: *mut *mut Error, src: *const c_char,
                               line: c_int, func: *const c_char,
                               fmt: *const c_char, ...);

    pub fn g_strdup(str: *const c_char) -> *mut c_char;

    pub fn qemu_blockalign(bs: *mut BlockDriverState, size: size_t)
        -> *mut c_void;

    pub fn qemu_vfree(ptr: *mut c_void);
}
