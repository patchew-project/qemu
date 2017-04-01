pub mod c_constants;
pub mod c_functions;
pub mod c_structs;


use core::intrinsics::transmute;
use core::marker::Sized;
pub use core::ops::{Deref,DerefMut};
use libc;
use libc::{c_char,c_int,c_void,size_t,EINVAL,EIO,ENOSPC,ENOTSUP,EPERM};
use self::c_functions::error_setg_internal;
use std::{mem,ptr,slice};
use std::cell::RefCell;
use std::ffi::CString;
use std::io;
use std::io::Write;
use std::marker::PhantomData;
use std::rc::{Rc,Weak};


pub type QDict = *mut c_structs::QDict;
pub type CBDS = c_structs::BlockDriverState;


pub const BDRV_SECTOR_SIZE: u64 = 512u64;
pub const BDRV_SECTOR_SHIFT: i32 = 9;


pub enum IOError {
    GenericError,
    NoSpaceLeft,
    InvalidMetadata,
    UnsupportedImageFeature,
}

impl IOError {
    pub fn to_errno(&self) -> c_int
    {
        match *self {
            IOError::GenericError => -EIO,
            IOError::NoSpaceLeft => -ENOSPC,
            IOError::InvalidMetadata => -EIO,
            IOError::UnsupportedImageFeature => -ENOTSUP,
        }
    }
}


pub struct BDSOpaqueLink<T> {
    pub opaque: Option<Rc<Box<BDSOpaque<T>>>>,
}

pub struct BDSOpaque<T> {
    c_obj: *mut CBDS,
    pub driver_obj: RefCell<T>,
}


/*
 * Macros for extracting the block driver's opaque BDS from a &mut CBDS
 *
 * Note that we have to pass a CBDS to the block driver functions instead of
 * the opaque BDS itself because a block driver is allowed to do recursive calls
 * (and even if it does not do so actively, the qemu block layer may just do it
 * anyway). The borrow checker will prevent us from borrowing the opaque BDS in
 * recursive function invocations, so the functions will have to borrow it only
 * when needed.
 */
#[macro_export]
macro_rules! let_bds {
    ($result:ident, $cbds:expr) => (
        let _bds_opaque = get_bds_opaque_link::<Self>($cbds).unwrap();
        let mut _driver_obj_ref = _bds_opaque.driver_obj.borrow();
        let $result = _driver_obj_ref.deref();
    );
}

#[macro_export]
macro_rules! let_mut_bds {
    ($result:ident, $cbds:expr) => (
        let _bds_opaque = get_bds_opaque_link::<Self>($cbds).unwrap();
        let mut _driver_obj_ref = _bds_opaque.driver_obj.borrow_mut();
        let $result = _driver_obj_ref.deref_mut();
    );
}


/* Allows you to prepend an error string to an error string on error */
#[macro_export]
macro_rules! try_prepend {
    ($e:expr, $m:expr) => (match $e {
        Ok(val) => val,
        Err(err) => return Err(String::from($m) + ": " + err.as_str()),
    });
}


/* try! that executes a qemu_vfree() on error */
#[macro_export]
macro_rules! try_vfree {
    ($e:expr, $buffer:ident) => (match $e {
            Ok(ok) => ok,
            Err(err) => {
                qemu_vfree($buffer);
                return Err(err);
            },
        });
}



pub struct BDSBacklink<T> {
    pub link: Weak<Box<BDSOpaque<T>>>,
}

pub struct BDSCommon<T> {
    backlink: Option<BDSBacklink<T>>,
}

impl<T> BDSCommon<T> {
    pub fn new() -> Self
    {
        Self {
            backlink: None,
        }
    }

    pub fn set_backlink(&mut self, backlink: BDSBacklink<T>)
    {
        self.backlink = Some(backlink);
    }

    pub fn get_cbds(&self) -> &mut CBDS
    {
        let backlink = self.backlink.as_ref().unwrap();
        let bds_opaque_rc = backlink.link.upgrade();
        let bds_opaque = Rc::as_ref(bds_opaque_rc.as_ref().unwrap());

        let res = unsafe { &mut *bds_opaque.c_obj };
        return res;
    }

    pub fn has_file(&self) -> bool
    {
        let cbds = self.get_cbds();
        !cbds.file.is_null()
    }

    pub fn file(&self) -> BdrvChild
    {
        let cbds = self.get_cbds();
        BdrvChild { c_ptr: cbds.file }
    }

    pub fn set_file(&mut self, file: Option<BdrvChild>)
    {
        let cbds = self.get_cbds();

        match file {
            None => cbds.file = ptr::null_mut(),
            Some(ref child) => cbds.file = child.c_ptr,
        };
    }

    pub fn has_backing(&self) -> bool
    {
        let cbds = self.get_cbds();
        !cbds.backing.is_null()
    }

    pub fn backing(&self) -> BdrvChild
    {
        let cbds = self.get_cbds();
        BdrvChild { c_ptr: cbds.backing }
    }

    pub fn set_backing(&mut self, backing: Option<BdrvChild>)
    {
        let cbds = self.get_cbds();

        match backing {
            None => cbds.file = ptr::null_mut(),
            Some(ref child) => cbds.file = child.c_ptr,
        };
    }
}


pub trait BlockDriverState where Self: Sized {
    fn new() -> Self;
    fn common(&mut self) -> &mut BDSCommon<Self>;

    fn set_backlink(&mut self, backlink: BDSBacklink<Self>)
    {
        self.common().set_backlink(backlink);
    }
}

pub trait BlockDriverOpen: BlockDriverState {
    fn bdrv_open(cbds: &mut CBDS, options: QDict, flags: u32)
        -> Result<(), String>;
}

pub trait BlockDriverClose: BlockDriverState {
    fn bdrv_close(cbds: &mut CBDS);
}

pub trait BlockDriverRead: BlockDriverState {
    /* iov is ordered backwards so you can pop in order */
    fn bdrv_co_preadv(cbds: &mut CBDS, offset: u64, bytes: u64,
                      iov: Vec<&mut [u8]>, flags: u32)
        -> Result<(), IOError>;
}

pub trait BlockDriverWrite: BlockDriverState {
    /* iov is ordered backwards so you can pop in order */
    fn bdrv_co_pwritev(cbds: &mut CBDS, offset: u64, bytes: u64,
                       iov: Vec<&[u8]>, flags: u32)
        -> Result<(), IOError>;
}

pub trait BlockDriverPerm: BlockDriverState {
    fn bdrv_check_perm(cbds: &mut CBDS, perm: u64, shared: u64)
        -> Result<(), String>;
    fn bdrv_set_perm(cbds: &mut CBDS, perm: u64, shared: u64);
    fn bdrv_abort_perm_update(cbds: &mut CBDS);
}

pub trait BlockDriverChildPerm: BlockDriverState {
    fn bdrv_child_perm(cbds: &mut CBDS, c: Option<&mut BdrvChild>,
                       role: &c_structs::BdrvChildRole,
                       parent_perm: u64, parent_shared: u64)
        -> (u64, u64); /* nperm, nshared */
}

pub trait BlockDriverInfo: BlockDriverState {
    fn bdrv_get_info(cbds: &mut CBDS, bdi: &mut c_structs::BlockDriverInfo)
        -> Result<(), String>;
}


pub struct BlockDriver<T> {
    c_obj: c_structs::BlockDriver,

    _phantom: PhantomData<T>,
}


pub fn get_bds_opaque_link<'a, T>(bds: *mut CBDS)
    -> &'a mut BDSOpaqueLink<T>
{
    unsafe {
        let r = transmute::<*mut c_void, *mut BDSOpaqueLink<T>>((*bds).opaque);
        return &mut *r;
    }
}

impl<T> BDSOpaqueLink<T> {
    pub fn unwrap(&self) -> &BDSOpaque<T>
    {
        Rc::as_ref(self.opaque.as_ref().unwrap()).as_ref()
    }
}


pub struct BdrvChild {
    c_ptr: *mut c_structs::BdrvChild,
}


impl BdrvChild {
    pub fn perm(&self) -> u64
    {
        unsafe {
            (*self.c_ptr).perm
        }
    }

    pub fn shared(&self) -> u64
    {
        unsafe {
            (*self.c_ptr).shared_perm
        }
    }

    pub fn borrow(&self) -> Self
    {
        BdrvChild {
            c_ptr: self.c_ptr
        }
    }

    pub fn bdrv_pread(&self, offset: u64, buf: &mut [u8])
        -> Result<(), String>
    {
        let ret = unsafe {
             c_functions::bdrv_pread(self.c_ptr, offset as i64,
                                     buf.as_mut_ptr() as *mut c_void,
                                     buf.len() as c_int)
        };

        if ret < 0 {
            Err(strerror(-ret))
        } else {
            Ok(())
        }
    }

    pub fn bdrv_pwrite(&self, offset: u64, buf: &[u8])
        -> Result<(), String>
    {
        let ret = unsafe {
            c_functions::bdrv_pwrite(self.c_ptr, offset as i64,
                                     buf.as_ptr() as *const c_void,
                                     buf.len() as c_int)
        };

        if ret < 0 {
            Err(strerror(-ret))
        } else {
            Ok(())
        }
    }
}


impl<T: BlockDriverOpen> BlockDriver<T> {
    extern fn invoke_open(bds: *mut CBDS, options: *mut c_structs::QDict,
                          flags: c_int, errp: *mut *mut c_structs::Error)
        -> c_int
    {
        let bds_opaque_link = get_bds_opaque_link::<T>(bds);

        assert!(bds_opaque_link.opaque.is_none());

        let weak_bds_opaque = {
            let rc_bds_opaque = Rc::new(Box::new(BDSOpaque::<T> {
                    c_obj: bds,
                    driver_obj: RefCell::new(T::new()),
                }));

            let weak_link = Rc::downgrade(&rc_bds_opaque);
            bds_opaque_link.opaque = Some(rc_bds_opaque);

            weak_link
        };

        {
            let bds_opaque = bds_opaque_link.unwrap();
            let mut driver_obj_ref = bds_opaque.driver_obj.borrow_mut();
            let backlink = BDSBacklink::<T> {
                link: weak_bds_opaque
            };

            driver_obj_ref.deref_mut().set_backlink(backlink);
        }

        let cbds = unsafe { &mut *bds };
        let res = T::bdrv_open(cbds, options, flags as u32);

        match res {
            Ok(_) => 0,
            Err(msg) => {
                bds_opaque_link.opaque = None;
                error_set_message(errp, msg);

                -EINVAL
            }
        }
    }
}


impl<T: BlockDriverClose> BlockDriver<T> {
    extern fn invoke_close(bds: *mut CBDS)
    {
        let cbds = unsafe { &mut *bds };
        T::bdrv_close(cbds);

        get_bds_opaque_link::<T>(bds).opaque = None;
    }
}


impl<T: BlockDriverRead> BlockDriver<T> {
    extern fn invoke_co_preadv(bds: *mut CBDS, offset: u64, bytes: u64,
                               qiov: *mut c_structs::QEMUIOVector, flags: c_int)
        -> c_int
    {
        let cbds = unsafe { &mut *bds };
        let qiov_deref = unsafe { &*qiov };

        let qiov_vecs = unsafe {
                slice::from_raw_parts(qiov_deref.iov, qiov_deref.niov as usize)
            };

        let mut iov = Vec::new();
        /* Push backwards so the driver can pop in the right order */
        for vec in qiov_vecs.iter().rev() {
            iov.push(iov_as_mut_byte_slice(vec));
        }

        let res = T::bdrv_co_preadv(cbds, offset, bytes, iov, flags as u32);
        match res {
            Ok(_) => 0,
            Err(err) => err.to_errno(),
        }
    }
}


impl<T: BlockDriverWrite> BlockDriver<T> {
    extern fn invoke_co_pwritev(bds: *mut CBDS, offset: u64, bytes: u64,
                                qiov: *mut c_structs::QEMUIOVector, flags: c_int)
        -> c_int
    {
        let cbds = unsafe { &mut *bds };
        let qiov_deref = unsafe { &*qiov };

        let qiov_vecs = unsafe {
                slice::from_raw_parts(qiov_deref.iov, qiov_deref.niov as usize)
            };

        let mut iov = Vec::new();
        /* Push backwards so the driver can pop in the right order */
        for vec in qiov_vecs.iter().rev() {
            iov.push(iov_as_byte_slice(vec));
        }

        let res = T::bdrv_co_pwritev(cbds, offset, bytes, iov, flags as u32);
        match res {
            Ok(_) => 0,
            Err(err) => err.to_errno(),
        }
    }
}


impl<T: BlockDriverPerm> BlockDriver<T> {
    extern fn invoke_check_perm(bds: *mut CBDS, perm: u64, shared: u64,
                                errp: *mut *mut c_structs::Error)
        -> c_int
    {
        let cbds = unsafe { &mut *bds };
        let res = T::bdrv_check_perm(cbds, perm, shared);
        match res {
            Ok(_) => 0,
            Err(msg) => {
                error_set_message(errp, msg);
                -EPERM
            }
        }
    }

    extern fn invoke_set_perm(bds: *mut CBDS, perm: u64, shared: u64)
    {
        let cbds = unsafe { &mut *bds };
        T::bdrv_set_perm(cbds, perm, shared);
    }

    extern fn invoke_abort_perm_update(bds: *mut CBDS)
    {
        let cbds = unsafe { &mut *bds };
        T::bdrv_abort_perm_update(cbds);
    }
}


impl<T: BlockDriverChildPerm> BlockDriver<T> {
    extern fn invoke_child_perm(bds: *mut CBDS, c: *mut c_structs::BdrvChild,
                                role: *const c_structs::BdrvChildRole,
                                parent_perm: u64, parent_shared: u64,
                                nperm: *mut u64, nshared: *mut u64)
    {
        let cbds = unsafe { &mut *bds };
        let role_deref = unsafe { &*role };

        let res = if c.is_null() {
                T::bdrv_child_perm(cbds, None, role_deref,
                                   parent_perm, parent_shared)
            } else {
                let mut child = BdrvChild { c_ptr: c };
                T::bdrv_child_perm(cbds, Some(&mut child), role_deref,
                                   parent_perm, parent_shared)
            };

        unsafe {
            *nperm = res.0;
            *nshared = res.1;
        }
    }
}


impl<T: BlockDriverInfo> BlockDriver<T> {
    extern fn invoke_get_info(bds: *mut CBDS,
                              bdi: *mut c_structs::BlockDriverInfo)
        -> c_int
    {
        let cbds = unsafe { &mut *bds };
        let bdi_deref = unsafe { &mut *bdi };

        let res = T::bdrv_get_info(cbds, bdi_deref);
        match res {
            Ok(_) => 0,
            Err(msg) => {
                writeln!(&mut io::stderr(), "{}", msg).unwrap();
                -EIO
            }
        }
    }
}


impl<T: BlockDriverState> BlockDriver<T> {
    pub fn new(name: String) -> BlockDriver<T>
    {
        let instance_size = mem::size_of::<BDSOpaque<T>>();
        let /*mut*/ bdrv = BlockDriver::<T> {
            c_obj: c_structs::BlockDriver {
                format_name: CString::new(name).unwrap().into_raw(),
                instance_size: instance_size as c_int,

                is_filter: false,

                bdrv_recurse_is_first_non_filter: None,

                bdrv_probe: None,
                bdrv_probe_device: None,

                bdrv_parse_filename: None,
                bdrv_needs_filename: false,

                supports_backing: false,

                bdrv_reopen_prepare: None,
                bdrv_reopen_commit: None,
                bdrv_reopen_abort: None,
                bdrv_join_options: None,

                bdrv_open: None,
                bdrv_file_open: None,
                bdrv_close: None,
                bdrv_create: None,
                bdrv_set_key: None,
                bdrv_make_empty: None,

                bdrv_refresh_filename: None,

                bdrv_aio_readv: None,
                bdrv_aio_writev: None,
                bdrv_aio_flush: None,
                bdrv_aio_pdiscard: None,

                bdrv_co_readv: None,
                bdrv_co_preadv: None,
                bdrv_co_writev: None,
                bdrv_co_writev_flags: None,
                bdrv_co_pwritev: None,

                bdrv_co_pwrite_zeroes: None,
                bdrv_co_pdiscard: None,
                bdrv_co_get_block_status: None,

                bdrv_invalidate_cache: None,
                bdrv_inactivate: None,

                bdrv_co_flush: None,

                bdrv_co_flush_to_disk: None,

                bdrv_co_flush_to_os: None,

                protocol_name: ptr::null(),
                bdrv_truncate: None,

                bdrv_getlength: None,
                has_variable_length: false,
                bdrv_get_allocated_file_size: None,

                bdrv_co_pwritev_compressed: None,

                bdrv_snapshot_create: None,
                bdrv_snapshot_goto: None,
                bdrv_snapshot_delete: None,
                bdrv_snapshot_list: None,
                bdrv_snapshot_load_tmp: None,

                bdrv_get_info: None,
                bdrv_get_specific_info: None,

                bdrv_save_vmstate: None,
                bdrv_load_vmstate: None,

                bdrv_change_backing_file: None,

                bdrv_is_inserted: None,
                bdrv_media_changed: None,
                bdrv_eject: None,
                bdrv_lock_medium: None,

                bdrv_aio_ioctl: None,
                bdrv_co_ioctl: None,

                create_opts: ptr::null_mut(),

                bdrv_check: None,

                bdrv_amend_options: None,

                bdrv_debug_event: None,
                bdrv_debug_breakpoint: None,
                bdrv_debug_remove_breakpoint: None,
                bdrv_debug_resume: None,
                bdrv_debug_is_suspended: None,

                bdrv_refresh_limits: None,

                bdrv_has_zero_init: None,

                bdrv_detach_aio_context: None,

                bdrv_attach_aio_context: None,

                bdrv_io_plug: None,
                bdrv_io_unplug: None,

                bdrv_probe_blocksizes: None,
                bdrv_probe_geometry: None,

                bdrv_drain: None,

                bdrv_add_child: None,
                bdrv_del_child: None,

                bdrv_check_perm: None,
                bdrv_set_perm: None,
                bdrv_abort_perm_update: None,

                bdrv_child_perm: None,

                list: c_structs::QListEntry::<c_structs::BlockDriver> {
                    le_next: ptr::null_mut(),
                    le_prev: ptr::null_mut(),
                },
            },

            _phantom: PhantomData,
        };

        /* TODO: Call provides_* automatically
         * (We cannot do this currently because there is no way to either
         *  (1) Check whether T implements a trait in an if clause
         *  (2) Implement provides_* if T does not implement a trait
         *  (The latter of which is something that Rust is expected to implement
         *   at some point in the future.)) */

        return bdrv;
    }

    pub fn supports_backing(&mut self)
    {
        self.c_obj.supports_backing = true;
    }

    pub fn has_variable_length(&mut self)
    {
        self.c_obj.has_variable_length = true;
    }
}


impl<T: BlockDriverOpen> BlockDriver<T> {
    pub fn provides_open(&mut self)
    {
        self.c_obj.bdrv_open = Some(BlockDriver::<T>::invoke_open);
    }
}


impl<T: BlockDriverClose> BlockDriver<T> {
    pub fn provides_close(&mut self)
    {
        self.c_obj.bdrv_close = Some(BlockDriver::<T>::invoke_close);
    }
}


impl<T: BlockDriverRead> BlockDriver<T> {
    pub fn provides_read(&mut self)
    {
        self.c_obj.bdrv_co_preadv = Some(BlockDriver::<T>::invoke_co_preadv);
    }
}


impl<T: BlockDriverWrite> BlockDriver<T> {
    pub fn provides_write(&mut self)
    {
        self.c_obj.bdrv_co_pwritev = Some(BlockDriver::<T>::invoke_co_pwritev);
    }
}


impl<T: BlockDriverPerm> BlockDriver<T> {
    pub fn provides_perm(&mut self)
    {
        self.c_obj.bdrv_check_perm = Some(BlockDriver::<T>::invoke_check_perm);
        self.c_obj.bdrv_set_perm = Some(BlockDriver::<T>::invoke_set_perm);
        self.c_obj.bdrv_abort_perm_update =
            Some(BlockDriver::<T>::invoke_abort_perm_update);
    }
}


impl<T: BlockDriverChildPerm> BlockDriver<T> {
    pub fn provides_child_perm(&mut self)
    {
        self.c_obj.bdrv_child_perm = Some(BlockDriver::<T>::invoke_child_perm);
    }
}


impl<T: BlockDriverInfo> BlockDriver<T> {
    pub fn provides_info(&mut self)
    {
        self.c_obj.bdrv_get_info = Some(BlockDriver::<T>::invoke_get_info);
    }
}


pub fn bdrv_register<T>(bdrv: BlockDriver<T>)
{
    /* Box so it doesn't go away */
    let bdrv_box = Box::new(bdrv);

    unsafe {
        c_functions::bdrv_register(&mut (*Box::into_raw(bdrv_box)).c_obj);
    }
}


pub fn bdrv_open_child(filename: Option<String>, options: Option<QDict>,
                       bdref_key: String, parent: &mut CBDS,
                       child_role: &c_structs::BdrvChildRole, allow_none: bool)
    -> Result<BdrvChild, String>
{
    let c_filename = match filename {
        None => ptr::null(),
        Some(x) => CString::new(x).unwrap().into_raw(),
    };

    let c_options = match options {
        None => ptr::null_mut(),
        Some(x) => x,
    };

    let c_bdref_key = CString::new(bdref_key).unwrap().into_raw();
    let c_parent = parent as *mut CBDS;
    let c_child_role = child_role as *const c_structs::BdrvChildRole;

    let c_allow_none = allow_none;

    unsafe {
        let mut local_err: *mut c_structs::Error = ptr::null_mut();
        let c_errp = &mut local_err as *mut *mut c_structs::Error;

        let child = c_functions::bdrv_open_child(c_filename, c_options,
                                                 c_bdref_key, c_parent,
                                                 c_child_role, c_allow_none,
                                                 c_errp);

        if child.is_null() {
            Err(error_get_string(local_err))
        } else {
            Ok(BdrvChild { c_ptr: child })
        }
    }
}


pub enum StandardChildRole {
    File,
    Format,
    Backing,
}

pub fn bdrv_get_standard_child_role(role: StandardChildRole)
    -> &'static c_structs::BdrvChildRole
{
    unsafe {
        match role {
            StandardChildRole::File => &c_constants::child_file,
            StandardChildRole::Format => &c_constants::child_format,
            StandardChildRole::Backing => &c_constants::child_backing,
        }
    }
}


const BLK_PERM_CONSISTENT_READ  : u64 = 0x01u64;
const BLK_PERM_WRITE            : u64 = 0x02u64;
const BLK_PERM_WRITE_UNCHANGED  : u64 = 0x04u64;
const BLK_PERM_RESIZE           : u64 = 0x08u64;
const BLK_PERM_GRAPH_MOD        : u64 = 0x10u64;

const BLK_PERM_ALL              : u64 = 0x1fu64;


pub fn bdrv_filter_default_perms(c: Option<&mut BdrvChild>,
                                 _: &c_structs::BdrvChildRole,
                                 perm: u64, shared: u64)
    -> (u64, u64)
{
    let default_perm_passthrough = BLK_PERM_CONSISTENT_READ
                                   | BLK_PERM_WRITE
                                   | BLK_PERM_WRITE_UNCHANGED
                                   | BLK_PERM_RESIZE;
    let default_perm_unchanged = BLK_PERM_ALL & !default_perm_passthrough;

    if c.is_none() {
        (perm & default_perm_passthrough,
         (shared & default_perm_passthrough) | default_perm_unchanged)
    } else {
        let child = c.unwrap();

        ((perm & default_perm_passthrough) |
         (child.perm() & default_perm_unchanged),

         (shared & default_perm_passthrough) |
         (child.shared() & default_perm_unchanged))
    }
}


pub fn bdrv_format_default_perms(c: Option<&mut BdrvChild>,
                                 role: &c_structs::BdrvChildRole,
                                 perm: u64, shared: u64, is_read_only: bool)
    -> (u64, u64)
{
    let backing_role =
        bdrv_get_standard_child_role(StandardChildRole::Backing);

    let mut p = perm;
    let mut s = shared;

    if role as *const c_structs::BdrvChildRole
        == backing_role as *const c_structs::BdrvChildRole
    {
        p &= BLK_PERM_CONSISTENT_READ;

        if (s & BLK_PERM_WRITE) != 0 {
            s = BLK_PERM_WRITE | BLK_PERM_RESIZE;
        } else {
            s = 0;
        }

        s |= BLK_PERM_CONSISTENT_READ | BLK_PERM_GRAPH_MOD |
             BLK_PERM_WRITE_UNCHANGED;
    } else {
        let filter = bdrv_filter_default_perms(c, role, p, s);
        p = filter.0;
        s = filter.1;

        if !is_read_only {
            p |= BLK_PERM_WRITE | BLK_PERM_RESIZE;
        }

        p |= BLK_PERM_CONSISTENT_READ;
        s &= !(BLK_PERM_WRITE | BLK_PERM_RESIZE);
    }

    (p, s)
}


pub fn bdrv_is_read_only(bds: *mut CBDS) -> bool
{
    unsafe {
        c_functions::bdrv_is_read_only(bds)
    }
}


fn error_get_string(err: *mut c_structs::Error) -> String
{
    unsafe {
        let msg = c_functions::error_get_pretty(err);
        let dmsg = c_functions::g_strdup(msg);

        CString::from_raw(dmsg).into_string().unwrap()
    }
}


fn error_set_message(errp: *mut *mut c_structs::Error, msg: String)
{
    let file = CString::new("<FILE>").unwrap();
    let func = CString::new("<FUNC>").unwrap();
    let format = CString::new("%.*s").unwrap();

    unsafe {
        error_setg_internal(errp, file.as_ptr(), -1, func.as_ptr(),
                            format.as_ptr(),
                            msg.len() as c_int,
                            msg.as_ptr() as *const c_char);
    }
}


fn strerror(errno: c_int) -> String
{
    unsafe {
        let msg = libc::strerror(errno);
        let dmsg = c_functions::g_strdup(msg);

        CString::from_raw(dmsg).into_string().unwrap()
    }
}


/* Attention: The content of the slice is undefined!
 * (Also: Remember that the slice will not be automatically freed; you have to
 *  manually call qemu_vfree() for that.) */
pub fn qemu_blockalign(bds: *mut CBDS, size: usize) -> &'static mut [u8]
{
    unsafe {
        let p = c_functions::qemu_blockalign(bds, size as size_t);
        slice::from_raw_parts_mut(p as *mut u8, size)
    }
}


pub fn qemu_vfree(mem: &mut [u8])
{
    unsafe {
        c_functions::qemu_vfree(mem.as_mut_ptr() as *mut c_void);
    }
}


pub fn object_as_mut_byte_slice<T>(obj: &mut T) -> &mut [u8]
{
    unsafe {
        let p = obj as *mut T;
        slice::from_raw_parts_mut(p as *mut u8, mem::size_of::<T>())
    }
}


pub fn object_as_byte_slice<T>(obj: &T) -> &[u8]
{
    unsafe {
        let p = obj as *const T;
        slice::from_raw_parts(p as *const u8, mem::size_of::<T>())
    }
}


pub fn vec_as_mut_byte_slice<T>(obj: &mut Vec<T>) -> &mut [u8]
{
    unsafe {
        let p = &mut obj[0] as *mut T;
        slice::from_raw_parts_mut(p as *mut u8, obj.len() * mem::size_of::<T>())
    }
}


pub fn slice_as_mut_byte_slice<T>(obj: &mut [T]) -> &mut [u8]
{
    unsafe {
        slice::from_raw_parts_mut(obj.as_mut_ptr() as *mut u8,
                                  obj.len() * mem::size_of::<T>())
    }
}


pub fn iov_as_mut_byte_slice(obj: &c_structs::iovec) -> &mut [u8]
{
    unsafe {
        slice::from_raw_parts_mut(obj.iov_base as *mut u8, obj.iov_len)
    }
}


pub fn iov_as_byte_slice(obj: &c_structs::iovec) -> &[u8]
{
    unsafe {
        slice::from_raw_parts(obj.iov_base as *const u8, obj.iov_len)
    }
}


pub fn zero_byte_slice(slice: &mut [u8])
{
    unsafe {
        libc::memset(slice.as_mut_ptr() as *mut c_void, 0, slice.len());
    }
}


pub fn copy_into_byte_slice(dest: &mut [u8], offset: usize, src: &[u8])
{
    assert!(dest.len() >= offset + src.len());
    unsafe {
        let ptr = dest.as_mut_ptr();
        ptr.offset(offset as isize);

        libc::memcpy(ptr as *mut c_void, src.as_ptr() as *const c_void,
                     src.len());
    }
}
