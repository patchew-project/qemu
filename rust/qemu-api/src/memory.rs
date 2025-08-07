// Copyright 2024 Red Hat, Inc.
// Author(s): Paolo Bonzini <pbonzini@redhat.com>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Bindings for `MemoryRegion`, `MemoryRegionOps`, `MemTxAttrs` and
//! `MemoryRegionSection`.

use std::{
    ffi::{c_uint, c_void, CStr, CString},
    io::ErrorKind,
    marker::PhantomData,
    mem::size_of,
    ops::Deref,
    sync::atomic::Ordering,
};

// FIXME: Convert hwaddr to GuestAddress
pub use bindings::{hwaddr, MemTxAttrs};
pub use vm_memory::GuestAddress;
use vm_memory::{
    bitmap::BS, Address, AtomicAccess, Bytes, GuestMemoryError, GuestMemoryRegion,
    GuestMemoryResult, GuestUsize, MemoryRegionAddress, ReadVolatile, VolatileSlice, WriteVolatile,
};

use crate::{
    bindings::{
        self, device_endian, memory_region_init_io, section_access_allowed,
        section_covers_region_addr, section_fuzz_dma_read, section_get_host_addr,
        section_rust_load, section_rust_read_continue_step, section_rust_store,
        section_rust_write_continue_step, MEMTX_OK,
    },
    callbacks::FnCall,
    cell::Opaque,
    prelude::*,
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
