// Copyright 2024 Red Hat, Inc.
// Author(s): Paolo Bonzini <pbonzini@redhat.com>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Bindings for `MemoryRegion`, `MemoryRegionOps`, `MemTxAttrs`
//! `MemoryRegionSection`, `FlatView` and `AddressSpace`.

use std::{
    ffi::{c_uint, c_void, CStr, CString},
    io::ErrorKind,
    marker::PhantomData,
    mem::size_of,
    ops::Deref,
    ptr::{addr_of, NonNull},
    sync::atomic::Ordering,
};

// FIXME: Convert hwaddr to GuestAddress
pub use bindings::{hwaddr, MemTxAttrs};
pub use vm_memory::GuestAddress;
use vm_memory::{
    bitmap::BS, Address, AtomicAccess, Bytes, GuestAddressSpace, GuestMemory, GuestMemoryError,
    GuestMemoryRegion, GuestMemoryResult, GuestUsize, MemoryRegionAddress, ReadVolatile,
    VolatileSlice, WriteVolatile,
};

use crate::{
    bindings::{
        self, address_space_lookup_section, address_space_memory, address_space_to_flatview,
        device_endian, flatview_ref, flatview_translate_section, flatview_unref,
        memory_region_init_io, section_access_allowed, section_covers_region_addr,
        section_fuzz_dma_read, section_get_host_addr, section_rust_load,
        section_rust_read_continue_step, section_rust_store, section_rust_write_continue_step,
        target_big_endian, MEMTX_OK,
    },
    callbacks::FnCall,
    cell::Opaque,
    error::{Error, Result},
    prelude::*,
    rcu::{rcu_read_lock, rcu_read_unlock},
    uninit::MaybeUninitField,
    zeroable::Zeroable,
};

pub struct MemoryRegionOps<T>(
    bindings::MemoryRegionOps,
    // Note: quite often you'll see PhantomData<fn(&T)> mentioned when discussing
    // covariance and contravariance; you don't need any of those to understand
    // this usage of PhantomData.  Quite simply, MemoryRegionOps<T> *logically*
    // holds callbacks that take an argument of type &T, except the type is erased
    // before the callback is stored in the bindings::MemoryRegionOps field.
    // The argument of PhantomData is a function pointer in order to represent
    // that relationship; while that will also provide desirable and safe variance
    // for T, variance is not the point but just a consequence.
    PhantomData<fn(&T)>,
);

// SAFETY: When a *const T is passed to the callbacks, the call itself
// is done in a thread-safe manner.  The invocation is okay as long as
// T itself is `Sync`.
unsafe impl<T: Sync> Sync for MemoryRegionOps<T> {}

#[derive(Clone)]
pub struct MemoryRegionOpsBuilder<T>(bindings::MemoryRegionOps, PhantomData<fn(&T)>);

unsafe extern "C" fn memory_region_ops_read_cb<T, F: for<'a> FnCall<(&'a T, hwaddr, u32), u64>>(
    opaque: *mut c_void,
    addr: hwaddr,
    size: c_uint,
) -> u64 {
    F::call((unsafe { &*(opaque.cast::<T>()) }, addr, size))
}

unsafe extern "C" fn memory_region_ops_write_cb<T, F: for<'a> FnCall<(&'a T, hwaddr, u64, u32)>>(
    opaque: *mut c_void,
    addr: hwaddr,
    data: u64,
    size: c_uint,
) {
    F::call((unsafe { &*(opaque.cast::<T>()) }, addr, data, size))
}

impl<T> MemoryRegionOpsBuilder<T> {
    #[must_use]
    pub const fn read<F: for<'a> FnCall<(&'a T, hwaddr, u32), u64>>(mut self, _f: &F) -> Self {
        self.0.read = Some(memory_region_ops_read_cb::<T, F>);
        self
    }

    #[must_use]
    pub const fn write<F: for<'a> FnCall<(&'a T, hwaddr, u64, u32)>>(mut self, _f: &F) -> Self {
        self.0.write = Some(memory_region_ops_write_cb::<T, F>);
        self
    }

    #[must_use]
    pub const fn big_endian(mut self) -> Self {
        self.0.endianness = device_endian::DEVICE_BIG_ENDIAN;
        self
    }

    #[must_use]
    pub const fn little_endian(mut self) -> Self {
        self.0.endianness = device_endian::DEVICE_LITTLE_ENDIAN;
        self
    }

    #[must_use]
    pub const fn native_endian(mut self) -> Self {
        self.0.endianness = device_endian::DEVICE_NATIVE_ENDIAN;
        self
    }

    #[must_use]
    pub const fn valid_sizes(mut self, min: u32, max: u32) -> Self {
        self.0.valid.min_access_size = min;
        self.0.valid.max_access_size = max;
        self
    }

    #[must_use]
    pub const fn valid_unaligned(mut self) -> Self {
        self.0.valid.unaligned = true;
        self
    }

    #[must_use]
    pub const fn impl_sizes(mut self, min: u32, max: u32) -> Self {
        self.0.impl_.min_access_size = min;
        self.0.impl_.max_access_size = max;
        self
    }

    #[must_use]
    pub const fn impl_unaligned(mut self) -> Self {
        self.0.impl_.unaligned = true;
        self
    }

    #[must_use]
    pub const fn build(self) -> MemoryRegionOps<T> {
        MemoryRegionOps::<T>(self.0, PhantomData)
    }

    #[must_use]
    pub const fn new() -> Self {
        Self(bindings::MemoryRegionOps::ZERO, PhantomData)
    }
}

impl<T> Default for MemoryRegionOpsBuilder<T> {
    fn default() -> Self {
        Self::new()
    }
}

/// A safe wrapper around [`bindings::MemoryRegion`].
#[repr(transparent)]
#[derive(qemu_api_macros::Wrapper)]
pub struct MemoryRegion(Opaque<bindings::MemoryRegion>);

unsafe impl Send for MemoryRegion {}
unsafe impl Sync for MemoryRegion {}

impl MemoryRegion {
    // inline to ensure that it is not included in tests, which only
    // link to hwcore and qom.  FIXME: inlining is actually the opposite
    // of what we want, since this is the type-erased version of the
    // init_io function below.  Look into splitting the qemu_api crate.
    #[inline(always)]
    unsafe fn do_init_io(
        slot: *mut bindings::MemoryRegion,
        owner: *mut bindings::Object,
        ops: &'static bindings::MemoryRegionOps,
        name: &'static str,
        size: u64,
    ) {
        unsafe {
            let cstr = CString::new(name).unwrap();
            memory_region_init_io(
                slot,
                owner,
                ops,
                owner.cast::<c_void>(),
                cstr.as_ptr(),
                size,
            );
        }
    }

    pub fn init_io<T: IsA<Object>>(
        this: &mut MaybeUninitField<'_, T, Self>,
        ops: &'static MemoryRegionOps<T>,
        name: &'static str,
        size: u64,
    ) {
        unsafe {
            Self::do_init_io(
                this.as_mut_ptr().cast(),
                MaybeUninitField::parent_mut(this).cast(),
                &ops.0,
                name,
                size,
            );
        }
    }
}

unsafe impl ObjectType for MemoryRegion {
    type Class = bindings::MemoryRegionClass;
    const TYPE_NAME: &'static CStr =
        unsafe { CStr::from_bytes_with_nul_unchecked(bindings::TYPE_MEMORY_REGION) };
}
qom_isa!(MemoryRegion: Object);

/// A special `MemTxAttrs` constant, used to indicate that no memory
/// attributes are specified.
///
/// Bus masters which don't specify any attributes will get this,
/// which has all attribute bits clear except the topmost one
/// (so that we can distinguish "all attributes deliberately clear"
/// from "didn't specify" if necessary).
pub const MEMTXATTRS_UNSPECIFIED: MemTxAttrs = MemTxAttrs {
    unspecified: true,
    ..Zeroable::ZERO
};

/// A safe wrapper around [`bindings::MemoryRegionSection`].
///
/// This struct is fundamental for integrating QEMU's memory model with
/// the [`vm-memory`] ecosystem.  It directly maps to the concept of
/// [`GuestMemoryRegion`](vm_memory::GuestMemoryRegion) and implements
/// that trait.
///
/// ### `MemoryRegion` vs. `MemoryRegionSection`
///
/// Although QEMU already has native memory region abstraction, this is
/// [`MemoryRegion`], which supports overlapping.  But `vm-memory` doesn't
/// support overlapped memory, so `MemoryRegionSection` is more proper
/// to implement [`GuestMemoryRegion`](vm_memory::GuestMemoryRegion)
/// trait.
///
/// One point should pay attention is,
/// [`MemoryRegionAddress`](vm_memory::MemoryRegionAddress) represents the
/// address or offset within the `MemoryRegionSection`.  But traditional C
/// bindings treats memory region address or offset as the offset within
/// `MemoryRegion`.
///
/// Therefore, it's necessary to do conversion when calling C bindings
/// with `MemoryRegionAddress` from the context of `MemoryRegionSection`.
///
/// ### Usage
///
/// Considerring memory access is almost always through `AddressSpace`
/// in QEMU, `MemoryRegionSection` is intended for **internal use only**
///  within the `vm-memory` backend implementation.
///
/// Device and other external users should **not** use or create
/// `MemoryRegionSection`s directly.  Instead, they should work with the
/// higher-level `MemoryRegion` API to create and manage their device's
/// memory.  This separation of concerns mirrors the C API and avoids
/// confusion about different memory abstractions.
#[repr(transparent)]
#[derive(qemu_api_macros::Wrapper)]
pub struct MemoryRegionSection(Opaque<bindings::MemoryRegionSection>);

unsafe impl Send for MemoryRegionSection {}
unsafe impl Sync for MemoryRegionSection {}

impl Deref for MemoryRegionSection {
    type Target = bindings::MemoryRegionSection;

    fn deref(&self) -> &Self::Target {
        // SAFETY: Opaque<> wraps a pointer from C side. The validity
        // of the pointer is confirmed at the creation of Opaque<>.
        unsafe { &*self.0.as_ptr() }
    }
}

impl MemoryRegionSection {
    /// A fuzz testing hook for DMA read.
    ///
    /// When CONFIG_FUZZ is not set, this hook will do nothing.
    #[allow(dead_code)]
    fn fuzz_dma_read(&self, addr: GuestAddress, len: GuestUsize) -> &Self {
        // SAFETY: Opaque<> ensures the pointer is valid, and here it
        // takes into account the offset conversion between MemoryRegionSection
        // and MemoryRegion.
        unsafe {
            section_fuzz_dma_read(
                self.as_mut_ptr(),
                addr.checked_add(self.deref().offset_within_region)
                    .unwrap()
                    .raw_value(),
                len,
            )
        };
        self
    }

    /// A helper to check if the memory access is allowed.
    ///
    /// This is needed for memory write/read.
    #[allow(dead_code)]
    fn is_access_allowed(&self, addr: MemoryRegionAddress, len: GuestUsize) -> bool {
        // SAFETY: Opaque<> ensures the pointer is valid, and here it
        // takes into account the offset conversion between MemoryRegionSection
        // and MemoryRegion.
        let allowed = unsafe {
            section_access_allowed(
                self.as_mut_ptr(),
                MEMTXATTRS_UNSPECIFIED,
                addr.checked_add(self.deref().offset_within_region)
                    .unwrap()
                    .raw_value(),
                len,
            )
        };
        allowed
    }
}

impl Bytes<MemoryRegionAddress> for MemoryRegionSection {
    type E = GuestMemoryError;

    /// The memory wirte interface based on `MemoryRegionSection`.
    ///
    /// This function - as an intermediate step - is called by FlatView's
    /// write(). And it shouldn't be called to access memory directly.
    fn write(&self, buf: &[u8], addr: MemoryRegionAddress) -> GuestMemoryResult<usize> {
        let len = buf.len() as u64;
        let mut remain = len;

        // SAFETY: the pointers and reference are convertible and the
        // offset conversion is considerred.
        let ret = unsafe {
            section_rust_write_continue_step(
                self.as_mut_ptr(),
                MEMTXATTRS_UNSPECIFIED,
                buf.as_ptr(),
                len,
                addr.checked_add(self.deref().offset_within_region)
                    .unwrap()
                    .raw_value(),
                &mut remain,
            )
        };

        if ret == MEMTX_OK {
            return Ok(remain as usize);
        } else {
            return Err(GuestMemoryError::InvalidBackendAddress);
        }
    }

    /// The memory read interface based on `MemoryRegionSection`.
    ///
    /// This function - as an intermediate step - is called by FlatView's
    /// read(). And it shouldn't be called to access memory directly.
    fn read(&self, buf: &mut [u8], addr: MemoryRegionAddress) -> GuestMemoryResult<usize> {
        let len = buf.len() as u64;
        let mut remain = len;

        // SAFETY: the pointers and reference are convertible and the
        // offset conversion is considerred.
        let ret = unsafe {
            section_rust_read_continue_step(
                self.as_mut_ptr(),
                MEMTXATTRS_UNSPECIFIED,
                buf.as_mut_ptr(),
                len,
                addr.checked_add(self.deref().offset_within_region)
                    .unwrap()
                    .raw_value(),
                &mut remain,
            )
        };

        if ret == MEMTX_OK {
            return Ok(remain as usize);
        } else {
            return Err(GuestMemoryError::InvalidBackendAddress);
        }
    }

    /// The memory store interface based on `MemoryRegionSection`.
    ///
    /// This function - as the low-level store implementation - is
    /// called by FlatView's store(). And it shouldn't be called to
    ///  access memory directly.
    fn store<T: AtomicAccess>(
        &self,
        val: T,
        addr: MemoryRegionAddress,
        _order: Ordering,
    ) -> GuestMemoryResult<()> {
        let len = size_of::<T>();

        if len > size_of::<u64>() {
            return Err(GuestMemoryError::IOError(std::io::Error::new(
                ErrorKind::InvalidInput,
                "failed to store the data more then 8 bytes",
            )));
        }

        // Note: setcion_rust_store() accepts `const uint8_t *buf`.
        //
        // This is a "compromise" solution: vm-memory requires AtomicAccess
        // but QEMU uses uint64_t as the default type. Here we can't convert
        // AtomicAccess to u64, since complier will complain "an `as`
        // expression can only be used to convert between primitive types or
        // to coerce to a specific trait object", or other endless errors
        // about convertion to u64.
        //
        // Fortunately, we can use a byte array to bridge the Rust wrapper
        // and the C binding. This approach is not without a trade-off,
        // however: the section_rust_store() function requires an additional
        // conversion from bytes to a uint64_t. This performance overhead is
        // considered acceptable.
        //
        // SAFETY: the pointers are convertible and the offset conversion is
        // considerred.
        let res = unsafe {
            section_rust_store(
                self.as_mut_ptr(),
                addr.checked_add(self.deref().offset_within_region)
                    .unwrap()
                    .raw_value(),
                val.as_slice().as_ptr(),
                MEMTXATTRS_UNSPECIFIED,
                len as u64,
            )
        };

        match res {
            MEMTX_OK => Ok(()),
            _ => Err(GuestMemoryError::InvalidBackendAddress),
        }
    }

    /// The memory load interface based on `MemoryRegionSection`.
    ///
    /// This function - as the low-level load implementation - is
    /// called by FlatView's load(). And it shouldn't be called to
    /// access memory directly.
    fn load<T: AtomicAccess>(
        &self,
        addr: MemoryRegionAddress,
        _order: Ordering,
    ) -> GuestMemoryResult<T> {
        let len = size_of::<T>();

        if len > size_of::<u64>() {
            return Err(GuestMemoryError::IOError(std::io::Error::new(
                ErrorKind::InvalidInput,
                "failed to load the data more then 8 bytes",
            )));
        }

        let mut val: T = T::zeroed();

        // Note: setcion_rust_load() accepts `uint8_t *buf`.
        //
        // It has the similar reason as store() with the slight difference,
        // which is section_rust_load() requires additional conversion of
        // uint64_t to bytes.
        //
        // SAFETY: the pointers are convertible and the offset conversion is
        // considerred.
        let res = unsafe {
            section_rust_load(
                self.as_mut_ptr(),
                addr.checked_add(self.deref().offset_within_region)
                    .unwrap()
                    .raw_value(),
                val.as_mut_slice().as_mut_ptr(),
                MEMTXATTRS_UNSPECIFIED,
                size_of::<T>() as u64,
            )
        };

        match res {
            MEMTX_OK => Ok(val),
            _ => Err(GuestMemoryError::InvalidBackendAddress),
        }
    }

    fn write_slice(&self, _buf: &[u8], _addr: MemoryRegionAddress) -> GuestMemoryResult<()> {
        unimplemented!()
    }

    fn read_slice(&self, _buf: &mut [u8], _addr: MemoryRegionAddress) -> GuestMemoryResult<()> {
        unimplemented!()
    }

    fn read_volatile_from<F>(
        &self,
        _addr: MemoryRegionAddress,
        _src: &mut F,
        _count: usize,
    ) -> GuestMemoryResult<usize>
    where
        F: ReadVolatile,
    {
        unimplemented!()
    }

    fn read_exact_volatile_from<F>(
        &self,
        _addr: MemoryRegionAddress,
        _src: &mut F,
        _count: usize,
    ) -> GuestMemoryResult<()>
    where
        F: ReadVolatile,
    {
        unimplemented!()
    }

    fn write_volatile_to<F>(
        &self,
        _addr: MemoryRegionAddress,
        _dst: &mut F,
        _count: usize,
    ) -> GuestMemoryResult<usize>
    where
        F: WriteVolatile,
    {
        unimplemented!()
    }

    fn write_all_volatile_to<F>(
        &self,
        _addr: MemoryRegionAddress,
        _dst: &mut F,
        _count: usize,
    ) -> GuestMemoryResult<()>
    where
        F: WriteVolatile,
    {
        unimplemented!()
    }
}

impl GuestMemoryRegion for MemoryRegionSection {
    type B = ();

    /// Get the memory size covered by this MemoryRegionSection.
    fn len(&self) -> GuestUsize {
        self.deref().size as GuestUsize
    }

    /// Return the minimum (inclusive) Guest physical address managed by
    /// this MemoryRegionSection.
    fn start_addr(&self) -> GuestAddress {
        GuestAddress(self.deref().offset_within_address_space)
    }

    fn bitmap(&self) -> BS<'_, Self::B> {
        ()
    }

    /// Check whether the @addr is covered by this MemoryRegionSection.
    fn check_address(&self, addr: MemoryRegionAddress) -> Option<MemoryRegionAddress> {
        // SAFETY: the pointer is convertible and the offset conversion is
        // considerred.
        if unsafe {
            section_covers_region_addr(
                self.as_mut_ptr(),
                addr.checked_add(self.deref().offset_within_region)
                    .unwrap()
                    .raw_value(),
            )
        } {
            Some(addr)
        } else {
            None
        }
    }

    /// Get the host virtual address from the offset of this MemoryRegionSection
    /// (@addr).
    fn get_host_address(&self, addr: MemoryRegionAddress) -> GuestMemoryResult<*mut u8> {
        self.check_address(addr)
            .ok_or(GuestMemoryError::InvalidBackendAddress)
            .map(|addr|
                // SAFETY: the pointers are convertible and the offset
                // conversion is considerred.
                unsafe { section_get_host_addr(self.as_mut_ptr(), addr.raw_value()) })
    }

    fn get_slice(
        &self,
        _offset: MemoryRegionAddress,
        _count: usize,
    ) -> GuestMemoryResult<VolatileSlice<BS<Self::B>>> {
        unimplemented!()
    }
}

/// A safe wrapper around [`bindings::FlatView`].
///
/// [`Flaftview`] represents a collection of memory regions, and maps to
/// [`GuestMemoryRegion`](vm_memory::GuestMemoryRegion).
///
/// The memory details are hidden beneath this wrapper. Direct memory access
/// is not allowed.  Instead, memory access, e.g., write/read/store/load
/// should process through [`Bytes<GuestAddress>`].
#[repr(transparent)]
#[derive(qemu_api_macros::Wrapper)]
pub struct FlatView(Opaque<bindings::FlatView>);

unsafe impl Send for FlatView {}
unsafe impl Sync for FlatView {}

impl Deref for FlatView {
    type Target = bindings::FlatView;

    fn deref(&self) -> &Self::Target {
        // SAFETY: Opaque<> wraps a pointer from C side. The validity
        // of the pointer is confirmed at the creation of Opaque<>.
        unsafe { &*self.0.as_ptr() }
    }
}

impl FlatView {
    /// Translate guest address to the offset within a MemoryRegionSection.
    ///
    /// Ideally, this helper should be integrated into
    /// GuestMemory::to_region_addr(), but we haven't reached there yet.
    fn translate(
        &self,
        addr: GuestAddress,
        len: GuestUsize,
        is_write: bool,
    ) -> Option<(&MemoryRegionSection, MemoryRegionAddress, GuestUsize)> {
        let mut remain = len as hwaddr;
        let mut raw_addr: hwaddr = 0;

        // SAFETY: the pointers and reference are convertible and the
        // offset conversion is considerred.
        let ptr = unsafe {
            flatview_translate_section(
                self.as_mut_ptr(),
                addr.raw_value(),
                &mut raw_addr,
                &mut remain,
                is_write,
                MEMTXATTRS_UNSPECIFIED,
            )
        };

        if ptr.is_null() {
            return None;
        }

        // SAFETY: the pointer is valid and not NULL.
        let s = unsafe { <FlatView as GuestMemory>::R::from_raw(ptr) };
        Some((
            s,
            MemoryRegionAddress(raw_addr)
                .checked_sub(s.deref().offset_within_region)
                .unwrap(),
            remain as GuestUsize,
        ))
    }
}

impl Bytes<GuestAddress> for FlatView {
    type E = GuestMemoryError;

    /// The memory wirte interface based on `FlatView`.
    ///
    /// This function is similar to `flatview_write` in C side, but it
    /// only supports MEMTXATTRS_UNSPECIFIED for now.
    ///
    /// Note: This function should be called within RCU critical section.
    /// Furthermore, it is only for internal use and should not be called
    /// directly.
    fn write(&self, buf: &[u8], addr: GuestAddress) -> GuestMemoryResult<usize> {
        self.try_access(
            buf.len(),
            addr,
            true,
            |offset, count, caddr, region| -> GuestMemoryResult<usize> {
                // vm-memory provides an elegent way to advance (See
                // ReadVolatile::read_volatile), but at this moment,
                // this simple way is enough.
                let sub_buf = &buf[offset..offset + count];
                region.write(sub_buf, caddr)
            },
        )
    }

    /// The memory wirte interface based on `FlatView`.
    ///
    /// This function is similar to `flatview_read` in C side, but it
    /// only supports MEMTXATTRS_UNSPECIFIED for now.
    ///
    /// Note: This function should be called within RCU critical section.
    /// Furthermore, it is only for internal use and should not be called
    /// directly.
    fn read(&self, buf: &mut [u8], addr: GuestAddress) -> GuestMemoryResult<usize> {
        if buf.len() == 0 {
            return Ok(0);
        }

        self.try_access(
            buf.len(),
            addr,
            false,
            |offset, count, caddr, region| -> GuestMemoryResult<usize> {
                // vm-memory provides an elegent way to advance (See
                // ReadVolatile::write_volatile), but at this moment,
                // this simple way is enough.
                let sub_buf = &mut buf[offset..offset + count];
                region
                    .fuzz_dma_read(addr, sub_buf.len() as GuestUsize)
                    .read(sub_buf, caddr)
            },
        )
    }

    /// The memory store interface based on `FlatView`.
    ///
    /// This function supports MEMTXATTRS_UNSPECIFIED, and only supports
    /// native endian, which means before calling this function, make sure
    /// the endian of value follows target's endian.
    ///
    /// Note: This function should be called within RCU critical section.
    /// Furthermore, it is only for internal use and should not be called
    /// directly.
    fn store<T: AtomicAccess>(
        &self,
        val: T,
        addr: GuestAddress,
        order: Ordering,
    ) -> GuestMemoryResult<()> {
        self.translate(addr, size_of::<T>() as GuestUsize, true)
            .ok_or(GuestMemoryError::InvalidGuestAddress(addr))
            .and_then(|(region, region_addr, remain)| {
                // Though C side handles this cross region case via MMIO
                // by default, it still looks very suspicious for store/
                // load. It happens Bytes::store() doesn't support more
                // argument to identify this case, so report an error
                // directly!
                if remain < size_of::<T>() as GuestUsize {
                    return Err(GuestMemoryError::InvalidBackendAddress);
                }

                region.store(val, region_addr, order)
            })
    }

    /// The memory load interface based on `FlatView`.
    ///
    /// This function supports MEMTXATTRS_UNSPECIFIED, and only supports
    /// native endian, which means the value returned by this function
    /// follows target's endian.
    ///
    /// Note: This function should be called within RCU critical section.
    /// Furthermore, it is only for internal use and should not be called
    /// directly.
    fn load<T: AtomicAccess>(&self, addr: GuestAddress, order: Ordering) -> GuestMemoryResult<T> {
        self.translate(addr, size_of::<T>() as GuestUsize, false)
            .ok_or(GuestMemoryError::InvalidGuestAddress(addr))
            .and_then(|(region, region_addr, remain)| {
                // Though C side handles this cross region case via MMIO
                // by default, it still looks very suspicious for store/
                // load. It happens Bytes::load() doesn't support more
                // arguments to identify this case, so report an error
                // directly!
                if remain < size_of::<T>() as GuestUsize {
                    return Err(GuestMemoryError::InvalidBackendAddress);
                }

                region
                    .fuzz_dma_read(addr, size_of::<T> as GuestUsize)
                    .load(region_addr, order)
            })
    }

    fn write_slice(&self, _buf: &[u8], _addr: GuestAddress) -> GuestMemoryResult<()> {
        unimplemented!()
    }

    fn read_slice(&self, _buf: &mut [u8], _addr: GuestAddress) -> GuestMemoryResult<()> {
        unimplemented!()
    }

    fn read_volatile_from<F>(
        &self,
        _addr: GuestAddress,
        _src: &mut F,
        _count: usize,
    ) -> GuestMemoryResult<usize>
    where
        F: ReadVolatile,
    {
        unimplemented!()
    }

    fn read_exact_volatile_from<F>(
        &self,
        _addr: GuestAddress,
        _src: &mut F,
        _count: usize,
    ) -> GuestMemoryResult<()>
    where
        F: ReadVolatile,
    {
        unimplemented!()
    }

    fn write_volatile_to<F>(
        &self,
        _addr: GuestAddress,
        _dst: &mut F,
        _count: usize,
    ) -> GuestMemoryResult<usize>
    where
        F: WriteVolatile,
    {
        unimplemented!()
    }

    fn write_all_volatile_to<F>(
        &self,
        _addr: GuestAddress,
        _dst: &mut F,
        _count: usize,
    ) -> GuestMemoryResult<()>
    where
        F: WriteVolatile,
    {
        unimplemented!()
    }
}

impl GuestMemory for FlatView {
    type R = MemoryRegionSection;

    /// Get the number of `MemoryRegionSection`s managed by this `FlatView`.
    fn num_regions(&self) -> usize {
        self.deref().nr.try_into().unwrap()
    }

    /// Find the `MemoryRegionSection` which covers @addr
    fn find_region(&self, addr: GuestAddress) -> Option<&Self::R> {
        // set resolve_subpage as true by default
        //
        // SAFETY: bindings::FlatView has `dispatch` field and the pointer is
        // valid, although accessing the field of C structure is ugly.
        let raw =
            unsafe { address_space_lookup_section(self.deref().dispatch, addr.raw_value(), true) };

        if !raw.is_null() {
            let s = unsafe { Self::R::from_raw(raw) };
            Some(s)
        } else {
            None
        }
    }

    /// Return an empty iterator.
    ///
    /// This function always triggers panic under debug mode.
    fn iter(&self) -> impl Iterator<Item = &Self::R> {
        assert!(false); // Do not use this iter()!

        // QEMU has a linear iteration in C side named `flatview_for_each_range`,
        // but it iterates `FlatRange` instead of `MemoryRegionSection`.
        //
        // It is still possible to have a `Iterator` based on `MemoryRegionSection`,
        // by iterating `FlatView::dispatch::map::sections`.
        //
        // However, it is not worth it. QEMU has implemented the two-level "page"
        // walk in `phys_page_find`, which is more efficient than linear
        // iteration. Therefore, there is no need to reinvent the wheel on the
        // Rust side, at least for now.
        //
        // Just return an empty iterator to satisfy the trait's contract.
        // This makes the code compile, but the iterator won't yield
        // any items.
        std::iter::empty()
    }

    fn to_region_addr(&self, _addr: GuestAddress) -> Option<(&Self::R, MemoryRegionAddress)> {
        // Note: This method should implement FlatView::translate(), but
        // its function signature is unfriendly to QEMU's translation. QEMU
        // needs to distinguish write access or not, and care about the
        // remianing bytes of the region.
        //
        // FIXME: Once GuestMemory::to_region_addr() could meet QEMU's
        // requirements, move FlatView::translate() here.
        unimplemented!()
    }

    /// Try to access a contiguous block of guest memory, executing a callback
    /// for each memory region that backs the requested address range.
    ///
    /// This method is the core of memory access.  It iterates through each
    /// `MemoryRegionSection` that corresponds to the guest address
    /// range [`addr`, `addr` + `count`) and invokes the provided closure `f`
    /// for each section.
    fn try_access<F>(
        &self,
        count: usize,
        addr: GuestAddress,
        is_write: bool,
        mut f: F,
    ) -> GuestMemoryResult<usize>
    where
        F: FnMut(usize, usize, MemoryRegionAddress, &Self::R) -> GuestMemoryResult<usize>,
    {
        // FIXME: it's tricky to add more argument in try_access(), e.g.,
        // attrs. Or maybe it's possible to move try_access() to Bytes trait,
        // then it can accept a generic type which contains the address and
        // other arguments.

        if count == 0 {
            return Ok(count);
        }

        let mut total = 0;
        let mut curr = addr;

        while total < count {
            let len = (count - total) as GuestUsize;
            let (region, start, remain) = self.translate(curr, len, is_write).unwrap();

            if !region.is_access_allowed(start, remain) {
                // FIXME: could we return something like MEMTX_ACCESS_ERROR?
                return Err(GuestMemoryError::InvalidGuestAddress(addr));
            }

            match f(total as usize, remain as usize, start, region) {
                // no more data
                Ok(0) => return Ok(total),
                // made some progress
                Ok(res) => {
                    if res as GuestUsize > remain {
                        return Err(GuestMemoryError::CallbackOutOfRange);
                    }

                    total = match total.checked_add(res) {
                        Some(x) if x < count => x,
                        Some(x) if x == count => return Ok(x),
                        _ => return Err(GuestMemoryError::CallbackOutOfRange),
                    };

                    curr = match curr.overflowing_add(res as GuestUsize) {
                        (x @ GuestAddress(0), _) | (x, false) => x,
                        (_, true) => return Err(GuestMemoryError::GuestAddressOverflow),
                    };
                }
                // error happened
                e => return e,
            }
        }

        if total == 0 {
            Err(GuestMemoryError::InvalidGuestAddress(addr))
        } else {
            Ok(total)
        }
    }
}

/// A RAII guard that provides temporary access to a `FlatView`.
///
/// Upon creation, this guard increments the reference count of the
/// underlying `FlatView`.  When the guard goes out of of scope, it
/// automatically decrements the count.
///
/// As long as the guard lives, the access to `FlatView` is valid.
#[derive(Debug)]
pub struct FlatViewRefGuard(NonNull<FlatView>);

impl Drop for FlatViewRefGuard {
    fn drop(&mut self) {
        // SAFETY: the pointer is convertible.
        unsafe { flatview_unref(self.0.as_ref().as_mut_ptr()) };
    }
}

impl FlatViewRefGuard {
    /// Attempt to create a new RAII guard for the given `FlatView`.
    ///
    /// This may fail if the `FlatView`'s reference count is already zero.
    pub fn new(flat: &FlatView) -> Option<Self> {
        // SAFETY: the pointer is convertible.
        if unsafe { flatview_ref(flat.as_mut_ptr()) } {
            Some(FlatViewRefGuard(NonNull::from(flat)))
        } else {
            None
        }
    }
}

impl Deref for FlatViewRefGuard {
    type Target = FlatView;

    fn deref(&self) -> &Self::Target {
        // SAFETY: the pointer and reference are convertible.
        unsafe { &*self.0.as_ptr() }
    }
}

impl Clone for FlatViewRefGuard {
    /// Clone the guard, which involves incrementing the reference
    /// count again.
    ///
    /// This method will **panic** if the reference count of the underlying
    /// `FlatView` cannot be incremented (e.g., if it is zero, meaning the
    /// object is being destroyed).  This can happen in concurrent scenarios.
    fn clone(&self) -> Self {
        FlatViewRefGuard::new(self.deref()).expect(
            "Failed to clone FlatViewRefGuard: the FlatView may have been destroyed concurrently.",
        )
    }
}

/// A safe wrapper around [`bindings::AddressSpace`].
///
/// [`AddressSpace`] is the address space abstraction in QEMU, which
/// provides memory access for the Guest memory it managed.
#[repr(transparent)]
#[derive(qemu_api_macros::Wrapper)]
pub struct AddressSpace(Opaque<bindings::AddressSpace>);

unsafe impl Send for AddressSpace {}
unsafe impl Sync for AddressSpace {}

impl GuestAddressSpace for AddressSpace {
    type M = FlatView;
    type T = FlatViewRefGuard;

    /// Get the memory of the [`AddressSpace`].
    ///
    /// This function retrieves the [`FlatView`] for the current
    /// [`AddressSpace`].  And it should be called from an RCU
    /// critical section.  The returned [`FlatView`] is used for
    /// short-term memory access.
    ///
    /// Note, this function method may **panic** if [`FlatView`] is
    /// being distroying.  Fo this case, we should consider to providing
    /// the more stable binding with [`bindings::address_space_get_flatview`].
    fn memory(&self) -> Self::T {
        let flatp = unsafe { address_space_to_flatview(self.0.as_mut_ptr()) };
        FlatViewRefGuard::new(unsafe { Self::M::from_raw(flatp) }).expect(
            "Failed to clone FlatViewRefGuard: the FlatView may have been destroyed concurrently.",
        )
    }
}

/// The helper to convert [`vm_memory::GuestMemoryError`] to
/// [`crate::error::Error`].
#[track_caller]
fn guest_mem_err_to_qemu_err(err: GuestMemoryError) -> Error {
    match err {
        GuestMemoryError::InvalidGuestAddress(addr) => {
            Error::from(format!("Invalid guest address: {:#x}", addr.raw_value()))
        }
        GuestMemoryError::InvalidBackendAddress => Error::from("Invalid backend memory address"),
        GuestMemoryError::GuestAddressOverflow => {
            Error::from("Guest address addition resulted in an overflow")
        }
        GuestMemoryError::CallbackOutOfRange => {
            Error::from("Callback accessed memory out of range")
        }
        GuestMemoryError::IOError(io_err) => Error::with_error("Guest memory I/O error", io_err),
        other_err => Error::with_error("An unexpected guest memory error occurred", other_err),
    }
}

impl AddressSpace {
    /// The write interface of `AddressSpace`.
    ///
    /// This function is similar to `address_space_write` in C side.
    ///
    /// But it assumes the memory attributes is MEMTXATTRS_UNSPECIFIED.
    pub fn write(&self, buf: &[u8], addr: GuestAddress) -> Result<usize> {
        rcu_read_lock();
        let r = self.memory().deref().write(buf, addr);
        rcu_read_unlock();
        r.map_err(guest_mem_err_to_qemu_err)
    }

    /// The read interface of `AddressSpace`.
    ///
    /// This function is similar to `address_space_read_full` in C side.
    ///
    /// But it assumes the memory attributes is MEMTXATTRS_UNSPECIFIED.
    ///
    /// It should also be noted that this function does not support the fast
    /// path like `address_space_read` in C side.
    pub fn read(&self, buf: &mut [u8], addr: GuestAddress) -> Result<usize> {
        rcu_read_lock();
        let r = self.memory().deref().read(buf, addr);
        rcu_read_unlock();
        r.map_err(guest_mem_err_to_qemu_err)
    }

    /// The store interface of `AddressSpace`.
    ///
    /// This function is similar to `address_space_st{size}` in C side.
    ///
    /// But it only assumes @val follows target-endian by default. So ensure
    /// the endian of `val` aligned with target, before using this method.  The
    /// taget-endian can be checked with [`target_is_big_endian`].
    ///
    /// And it assumes the memory attributes is MEMTXATTRS_UNSPECIFIED.
    ///
    /// # Examples
    ///
    /// ```
    /// use qemu_api::memory::{ADDRESS_SPACE_MEMORY, target_is_big_endian};
    ///
    /// let addr = GuestAddress(0x123438000);
    /// let val: u32 = 5;
    /// let val_end = if target_is_big_endian() {
    ///     val.to_be()
    /// } else {
    ///     val.to_le()
    /// }
    ///
    /// assert!(ADDRESS_SPACE_MEMORY.store(addr, val_end).is_ok());
    pub fn store<T: AtomicAccess>(&self, addr: GuestAddress, val: T) -> Result<()> {
        rcu_read_lock();
        let r = self.memory().deref().store(val, addr, Ordering::Relaxed);
        rcu_read_unlock();
        r.map_err(guest_mem_err_to_qemu_err)
    }

    /// The load interface of `AddressSpace`.
    ///
    /// This function is similar to `address_space_ld{size}` in C side.
    ///
    /// But it only support target-endian by default.  The returned value is
    /// with target-endian.  The taget-endian can be checked with
    /// [`target_is_big_endian`].
    ///
    /// And it assumes the memory attributes is MEMTXATTRS_UNSPECIFIED.
    pub fn load<T: AtomicAccess>(&self, addr: GuestAddress) -> Result<T> {
        rcu_read_lock();
        let r = self.memory().deref().load(addr, Ordering::Relaxed);
        rcu_read_unlock();
        r.map_err(guest_mem_err_to_qemu_err)
    }
}

/// The safe binding around [`bindings::address_space_memory`].
///
/// `ADDRESS_SPACE_MEMORY` provides the complete address space
/// abstraction for the whole Guest memory.
pub static ADDRESS_SPACE_MEMORY: &AddressSpace = unsafe {
    let ptr: *const bindings::AddressSpace = addr_of!(address_space_memory);

    // SAFETY: AddressSpace is #[repr(transparent)].
    let wrapper_ptr: *const AddressSpace = ptr.cast();

    // SAFETY: `address_space_memory` structure is valid in C side during
    // the whole QEMU life.
    &*wrapper_ptr
};

pub fn target_is_big_endian() -> bool {
    // SAFETY: the return value is boolean, so it is always valid.
    unsafe { target_big_endian() }
}
