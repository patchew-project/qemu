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

use common::{callbacks::FnCall, uninit::MaybeUninitField, zeroable::Zeroable, Opaque};
use qom::prelude::*;
pub use vm_memory::GuestAddress;
use vm_memory::{
    bitmap::BS, Address, AtomicAccess, Bytes, GuestMemoryError, GuestMemoryRegion,
    GuestMemoryRegionBytes, GuestMemoryResult, GuestUsize, MemoryRegionAddress, ReadVolatile,
    WriteVolatile,
};

use crate::bindings::{
    self, device_endian, memory_region_init_io, rust_section_load, rust_section_read_continue_step,
    rust_section_store, rust_section_write_continue_step, section_access_allowed,
    section_covers_region_addr, section_fuzz_dma_read, MemTxResult,
};
// FIXME: Convert hwaddr to GuestAddress
pub use crate::bindings::{hwaddr, MemTxAttrs};

/// Corresponds to C `MEMTX_OK` (#define `MEMTX_OK` 0).
const MEMTX_OK: MemTxResult = 0;

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
#[derive(common::Wrapper)]
pub struct MemoryRegion(Opaque<bindings::MemoryRegion>);

unsafe impl Send for MemoryRegion {}
unsafe impl Sync for MemoryRegion {}

impl MemoryRegion {
    unsafe fn do_init_io(
        slot: *mut bindings::MemoryRegion,
        owner: *mut qom::bindings::Object,
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
/// the `vm-memory` ecosystem.  It directly maps to the concept of
/// [`GuestMemoryRegion`] and implements that trait.
///
/// ### `MemoryRegion` vs. `MemoryRegionSection`
///
/// Although QEMU already has native memory region abstraction, this is
/// [`MemoryRegion`], which supports overlapping.  But `vm-memory` doesn't
/// support overlapped memory, so `MemoryRegionSection` is more proper
/// to implement [`GuestMemoryRegion`] trait.
///
/// One point should pay attention is, [`MemoryRegionAddress`] represents the
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
#[derive(common::Wrapper, Debug)]
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
    /// When `CONFIG_FUZZ` is not set, this hook will do nothing.
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
            );
        }
        self
    }

    /// A helper to check if the memory access is allowed.
    ///
    /// This is needed for memory write/read.
    #[allow(dead_code)]
    fn is_access_allowed(
        &self,
        addr: MemoryRegionAddress,
        len: GuestUsize,
        attrs: MemTxAttrs,
    ) -> bool {
        // SAFETY: Opaque<> ensures the pointer is valid, and here it
        // takes into account the offset conversion between MemoryRegionSection
        // and MemoryRegion.
        let allowed = unsafe {
            section_access_allowed(
                self.as_mut_ptr(),
                attrs,
                addr.checked_add(self.deref().offset_within_region)
                    .unwrap()
                    .raw_value(),
                len,
            )
        };
        allowed
    }
}

/// Satisfies the `Bytes<MemoryRegionAddress>` supertrait required by
/// `GuestMemoryRegion`.
///
/// The blanket impl fails with `HostAddressNotAvailable` because
/// `MemoryRegionSection` methods all go through `as_volatile_slice()` and
/// `get_slice()`, which by default return an error.
///
/// QEMU's real access path is always attrs-aware and reaches this section
/// through the `Bytes<(MemoryRegionAddress,
/// MemTxAttrs)>` impl below.
impl GuestMemoryRegionBytes for MemoryRegionSection {}

/// The attrs-aware `Bytes` implementation that actually does the C-side
/// memory access for a single `MemoryRegionSection`.
///
/// This composite impl is the *only* real access path into a
/// `MemoryRegionSection`: QEMU always reaches it through `FlatView`
/// with the real `attrs`.  The blanket `GuestMemoryRegionBytes` impl above
/// just satisfies the `GuestMemoryRegion` supertrait bound and is never
/// actually  called.
impl Bytes<(MemoryRegionAddress, MemTxAttrs)> for MemoryRegionSection {
    type E = GuestMemoryError;

    /// Write a byte buffer into guest memory at `addr`.
    ///
    /// This shouldn't be called to access memory directly; it is the
    /// per-region worker invoked by `FlatView`'s write path.  The transaction
    /// `attrs` are forwarded to the C side. And the cross-region is handled by
    /// `FlatView`'s write.
    fn write(
        &self,
        buf: &[u8],
        (addr, attrs): (MemoryRegionAddress, MemTxAttrs),
    ) -> GuestMemoryResult<usize> {
        let base = addr
            .checked_add(self.deref().offset_within_region)
            .unwrap()
            .raw_value();
        let total = buf.len() as u64;
        let mut done: u64 = 0;

        while done < total {
            // `step` is the attempt size on input and the bytes actually
            // handled on output.
            let mut step = total - done;

            // SAFETY: the pointers and reference are convertible and the
            // offset conversion is considered.
            let ret = unsafe {
                rust_section_write_continue_step(
                    self.as_mut_ptr(),
                    attrs,
                    buf[done as usize..].as_ptr(),
                    total - done,
                    base + done,
                    &mut step,
                )
            };

            if ret != MEMTX_OK {
                return Err(GuestMemoryError::InvalidBackendAddress);
            }

            // A zero-length step can never make progress.
            if step == 0 {
                break;
            }

            done += step;
        }

        Ok(done as usize)
    }

    /// Read a byte buffer from guest memory at `addr`.
    ///
    /// This shouldn't be called to access memory directly; it is the
    /// per-region worker invoked by `FlatView`'s read path.  The transaction
    /// `attrs` are forwarded to the C side. And the cross-region is handled by
    /// `FlatView`'s read.
    fn read(
        &self,
        buf: &mut [u8],
        (addr, attrs): (MemoryRegionAddress, MemTxAttrs),
    ) -> GuestMemoryResult<usize> {
        let base = addr
            .checked_add(self.deref().offset_within_region)
            .unwrap()
            .raw_value();
        let total = buf.len() as u64;
        let mut done: u64 = 0;

        while done < total {
            // `step` is the attempt size on input and the bytes actually
            // handled on output.
            let mut step = total - done;

            // SAFETY: the pointers and reference are convertible and the
            // offset conversion is considered.
            let ret = unsafe {
                rust_section_read_continue_step(
                    self.as_mut_ptr(),
                    attrs,
                    buf[done as usize..].as_mut_ptr(),
                    total - done,
                    base + done,
                    &mut step,
                )
            };

            if ret != MEMTX_OK {
                return Err(GuestMemoryError::InvalidBackendAddress);
            }

            // A zero-length step can never make progress.
            if step == 0 {
                break;
            }

            done += step;
        }

        Ok(done as usize)
    }

    /// Store a value into guest memory at `addr`.
    ///
    /// This function - as the low-level store implementation - is
    /// called by `FlatView`'s `store()`.  And it shouldn't be called to
    /// access memory directly.
    ///
    /// The transaction `attrs` are forwarded to the C side; the `Ordering` is
    /// ignored because the access is not Rust-atomic.
    fn store<T: AtomicAccess>(
        &self,
        val: T,
        (addr, attrs): (MemoryRegionAddress, MemTxAttrs),
        _order: Ordering,
    ) -> GuestMemoryResult<()> {
        let len = size_of::<T>();

        if len > size_of::<u64>() {
            return Err(GuestMemoryError::IOError(std::io::Error::new(
                ErrorKind::InvalidInput,
                "failed to store the data more then 8 bytes",
            )));
        }

        // Note: rust_section_store() accepts `const uint8_t *buf`.
        //
        // This is a "compromise" solution: vm-memory requires AtomicAccess
        // but QEMU uses uint64_t as the default type. Here we can't convert
        // AtomicAccess to u64, since the compiler will complain "an `as`
        // expression can only be used to convert between primitive types or
        // to coerce to a specific trait object", or other endless errors
        // about conversion to u64.
        //
        // Fortunately, we can use a byte array to bridge the Rust wrapper
        // and the C binding. This approach is not without a trade-off,
        // however: the rust_section_store() function requires an additional
        // conversion from bytes to a uint64_t for the MMIO case. This performance
        // overhead is considered acceptable.
        //
        // SAFETY: the pointers are convertible and the offset conversion is
        // considered.
        let res = unsafe {
            rust_section_store(
                self.as_mut_ptr(),
                addr.checked_add(self.deref().offset_within_region)
                    .unwrap()
                    .raw_value(),
                val.as_slice().as_ptr(),
                attrs,
                len as u64,
            )
        };

        match res {
            MEMTX_OK => Ok(()),
            _ => Err(GuestMemoryError::InvalidBackendAddress),
        }
    }

    /// Load a value from guest memory at `addr`.
    ///
    /// This function - as the low-level load implementation - is
    /// called by `FlatView`'s `load()`.  And it shouldn't be called to
    /// access memory directly.
    ///
    /// The transaction `attrs` are forwarded to the C side; the `Ordering` is
    /// ignored because the access is not Rust-atomic.
    fn load<T: AtomicAccess>(
        &self,
        (addr, attrs): (MemoryRegionAddress, MemTxAttrs),
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

        // Note: rust_section_load() accepts `uint8_t *buf`.
        //
        // This is for a similar reason as store(), with the slight difference
        // that rust_section_load() requires an additional conversion from
        // uint64_t to bytes.
        //
        // SAFETY: the pointers are convertible and the offset conversion is
        // considered.
        let res = unsafe {
            rust_section_load(
                self.as_mut_ptr(),
                addr.checked_add(self.deref().offset_within_region)
                    .unwrap()
                    .raw_value(),
                val.as_mut_slice().as_mut_ptr(),
                attrs,
                size_of::<T>() as u64,
            )
        };

        match res {
            MEMTX_OK => Ok(val),
            _ => Err(GuestMemoryError::InvalidBackendAddress),
        }
    }

    fn write_slice(
        &self,
        _buf: &[u8],
        _addr: (MemoryRegionAddress, MemTxAttrs),
    ) -> GuestMemoryResult<()> {
        unimplemented!()
    }

    fn read_slice(
        &self,
        _buf: &mut [u8],
        _addr: (MemoryRegionAddress, MemTxAttrs),
    ) -> GuestMemoryResult<()> {
        unimplemented!()
    }

    fn read_volatile_from<F>(
        &self,
        _addr: (MemoryRegionAddress, MemTxAttrs),
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
        _addr: (MemoryRegionAddress, MemTxAttrs),
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
        _addr: (MemoryRegionAddress, MemTxAttrs),
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
        _addr: (MemoryRegionAddress, MemTxAttrs),
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

    /// Get the memory size covered by this `MemoryRegionSection`.
    fn len(&self) -> GuestUsize {
        self.deref().size as GuestUsize
    }

    /// Return the minimum (inclusive) Guest physical address managed by
    /// this `MemoryRegionSection`.
    fn start_addr(&self) -> GuestAddress {
        GuestAddress(self.deref().offset_within_address_space)
    }

    fn bitmap(&self) -> BS<'_, Self::B> {}

    /// Check whether the `@addr` is covered by this `MemoryRegionSection`.
    fn check_address(&self, addr: MemoryRegionAddress) -> Option<MemoryRegionAddress> {
        let region_addr = addr
            .checked_add(self.deref().offset_within_region)?
            .raw_value();
        // SAFETY: the pointer is convertible and the offset conversion is
        // considered.
        if unsafe { section_covers_region_addr(self.as_mut_ptr(), region_addr) } {
            Some(addr)
        } else {
            None
        }
    }
}
