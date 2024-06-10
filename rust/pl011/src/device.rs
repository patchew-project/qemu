use core::{
    ffi::{c_int, c_uchar, c_uint, c_void, CStr},
    mem::MaybeUninit,
    ptr::{addr_of, addr_of_mut, NonNull},
};

use crate::{
    definitions::PL011_ARM_INFO,
    generated::{self, *},
    memory_ops::PL011_OPS,
    registers::{self, Interrupt},
    RegisterOffset,
};

static PL011_ID_ARM: [c_uchar; 8] = [0x11, 0x10, 0x14, 0x00, 0x0d, 0xf0, 0x05, 0xb1];

const DATA_BREAK: u32 = 1 << 10;

/// QEMU sourced constant.
pub const PL011_FIFO_DEPTH: usize = 16_usize;

#[repr(C)]
#[derive(Debug)]
/// PL011 Device Model in QEMU
pub struct PL011State {
    pub parent_obj: SysBusDevice,
    pub iomem: MemoryRegion,
    pub readbuff: u32,
    #[doc(alias = "fr")]
    pub flags: registers::Flags,
    #[doc(alias = "lcr")]
    pub line_control: registers::LineControl,
    #[doc(alias = "rsr")]
    pub receive_status_error_clear: registers::ReceiveStatusErrorClear,
    #[doc(alias = "cr")]
    pub control: registers::Control,
    pub dmacr: u32,
    pub int_enabled: u32,
    pub int_level: u32,
    pub read_fifo: [u32; PL011_FIFO_DEPTH],
    pub ilpr: u32,
    pub ibrd: u32,
    pub fbrd: u32,
    pub ifl: u32,
    pub read_pos: usize,
    pub read_count: usize,
    pub read_trigger: usize,
    #[doc(alias = "chr")]
    pub char_backend: CharBackend,
    /// QEMU interrupts
    ///
    /// ```text
    ///  * sysbus MMIO region 0: device registers
    ///  * sysbus IRQ 0: `UARTINTR` (combined interrupt line)
    ///  * sysbus IRQ 1: `UARTRXINTR` (receive FIFO interrupt line)
    ///  * sysbus IRQ 2: `UARTTXINTR` (transmit FIFO interrupt line)
    ///  * sysbus IRQ 3: `UARTRTINTR` (receive timeout interrupt line)
    ///  * sysbus IRQ 4: `UARTMSINTR` (momem status interrupt line)
    ///  * sysbus IRQ 5: `UARTEINTR` (error interrupt line)
    /// ```
    #[doc(alias = "irq")]
    pub interrupts: [qemu_irq; 6usize],
    #[doc(alias = "clk")]
    pub clock: NonNull<Clock>,
    #[doc(alias = "migrate_clk")]
    pub migrate_clock: bool,
}

impl PL011State {
    pub fn init(&mut self) {
        unsafe {
            memory_region_init_io(
                addr_of_mut!(self.iomem),
                addr_of_mut!(*self).cast::<Object>(),
                &PL011_OPS,
                addr_of_mut!(*self).cast::<c_void>(),
                PL011_ARM_INFO.name,
                0x1000,
            );
            let sbd = addr_of_mut!(*self).cast::<SysBusDevice>();
            let dev = addr_of_mut!(*self).cast::<DeviceState>();
            sysbus_init_mmio(sbd, addr_of_mut!(self.iomem));
            for irq in self.interrupts.iter_mut() {
                sysbus_init_irq(sbd, irq);
            }
            const CLK_NAME: &CStr = unsafe { CStr::from_bytes_with_nul_unchecked(b"clk\0") };
            self.clock = NonNull::new(qdev_init_clock_in(
                dev,
                CLK_NAME.as_ptr(),
                None, /* pl011_clock_update */
                addr_of_mut!(*self).cast::<c_void>(),
                ClockEvent_ClockUpdate,
            ))
            .unwrap();
        }
    }

    pub fn read(&mut self, offset: hwaddr, _size: core::ffi::c_uint) -> u64 {
        use RegisterOffset::*;

        match RegisterOffset::try_from(offset) {
            Err(v) if (0x3f8..0x400).contains(&v) => {
                u64::from(PL011_ID_ARM[((offset - 0xfe0) >> 2) as usize])
            }
            Err(_) => {
                // qemu_log_mask(LOG_GUEST_ERROR, "pl011_read: Bad offset 0x%x\n", (int)offset);
                0
            }
            Ok(DR) => {
                // s->flags &= ~PL011_FLAG_RXFF;
                self.flags.set_receive_fifo_full(false);
                let c = self.read_fifo[self.read_pos];
                if self.read_count > 0 {
                    self.read_count -= 1;
                    self.read_pos = (self.read_pos + 1) & (self.fifo_depth() - 1);
                }
                if self.read_count == 0 {
                    // self.flags |= PL011_FLAG_RXFE;
                    self.flags.set_receive_fifo_empty(true);
                }
                if self.read_count + 1 == self.read_trigger {
                    //self.int_level &= ~ INT_RX;
                    self.int_level &= !registers::INT_RX;
                }
                // Update error bits.
                self.receive_status_error_clear = c.to_be_bytes()[3].into();
                self.update();
                unsafe { qemu_chr_fe_accept_input(&mut self.char_backend) };
                c.into()
            }
            Ok(RSR) => u8::from(self.receive_status_error_clear).into(),
            Ok(FR) => u16::from(self.flags).into(),
            Ok(FBRD) => self.fbrd.into(),
            Ok(ILPR) => self.ilpr.into(),
            Ok(IBRD) => self.ibrd.into(),
            Ok(LCR_H) => u16::from(self.line_control).into(),
            Ok(CR) => {
                // We exercise our self-control.
                u16::from(self.control).into()
            }
            Ok(FLS) => self.ifl.into(),
            Ok(IMSC) => self.int_enabled.into(),
            Ok(RIS) => self.int_level.into(),
            Ok(MIS) => u64::from(self.int_level & self.int_enabled),
            Ok(ICR) => {
                // "The UARTICR Register is the interrupt clear register and is write-only"
                // Source: ARM DDI 0183G 3.3.13 Interrupt Clear Register, UARTICR
                0
            }
            Ok(DMACR) => self.dmacr.into(),
        }
    }

    pub fn write(&mut self, offset: hwaddr, value: u64) {
        // eprintln!("write offset {offset} value {value}");
        use RegisterOffset::*;
        let value: u32 = value as u32;
        match RegisterOffset::try_from(offset) {
            Err(_bad_offset) => {
                eprintln!("write bad offset {offset} value {value}");
            }
            Ok(DR) => {
                // ??? Check if transmitter is enabled.
                let ch: u8 = value as u8;
                // XXX this blocks entire thread. Rewrite to use
                // qemu_chr_fe_write and background I/O callbacks
                unsafe {
                    qemu_chr_fe_write_all(addr_of_mut!(self.char_backend), &ch, 1);
                }
                self.loopback_tx(value);
                self.int_level |= registers::INT_TX;
                self.update();
            }
            Ok(RSR) => {
                self.receive_status_error_clear = 0.into();
            }
            Ok(FR) => {
                // flag writes are ignored
            }
            Ok(ILPR) => {
                self.ilpr = value;
            }
            Ok(IBRD) => {
                self.ibrd = value;
            }
            Ok(FBRD) => {
                self.fbrd = value;
            }
            Ok(LCR_H) => {
                let value = value as u16;
                let new_val: registers::LineControl = value.into();
                // Reset the FIFO state on FIFO enable or disable
                if bool::from(self.line_control.fifos_enabled())
                    ^ bool::from(new_val.fifos_enabled())
                {
                    self.reset_fifo();
                }
                if self.line_control.send_break() ^ new_val.send_break() {
                    let mut break_enable: c_int = new_val.send_break().into();
                    unsafe {
                        qemu_chr_fe_ioctl(
                            addr_of_mut!(self.char_backend),
                            CHR_IOCTL_SERIAL_SET_BREAK as i32,
                            addr_of_mut!(break_enable).cast::<c_void>(),
                        );
                    }
                    self.loopback_break(break_enable > 0);
                }
                self.line_control = new_val;
                self.set_read_trigger();
            }
            Ok(CR) => {
                // ??? Need to implement the enable bit.
                let value = value as u16;
                self.control = value.into();
                self.loopback_mdmctrl();
            }
            Ok(FLS) => {
                self.ifl = value;
                self.set_read_trigger();
            }
            Ok(IMSC) => {
                self.int_enabled = value;
                self.update();
            }
            Ok(RIS) => {}
            Ok(MIS) => {}
            Ok(ICR) => {
                self.int_level &= !value;
                self.update();
            }
            Ok(DMACR) => {
                self.dmacr = value;
                if value & 3 > 0 {
                    // qemu_log_mask(LOG_UNIMP, "pl011: DMA not implemented\n");
                    eprintln!("pl011: DMA not implemented");
                }
            }
        }
    }

    #[inline]
    fn loopback_tx(&mut self, value: u32) {
        if !self.loopback_enabled() {
            return;
        }

        // Caveat:
        //
        // In real hardware, TX loopback happens at the serial-bit level
        // and then reassembled by the RX logics back into bytes and placed
        // into the RX fifo. That is, loopback happens after TX fifo.
        //
        // Because the real hardware TX fifo is time-drained at the frame
        // rate governed by the configured serial format, some loopback
        // bytes in TX fifo may still be able to get into the RX fifo
        // that could be full at times while being drained at software
        // pace.
        //
        // In such scenario, the RX draining pace is the major factor
        // deciding which loopback bytes get into the RX fifo, unless
        // hardware flow-control is enabled.
        //
        // For simplicity, the above described is not emulated.
        self.put_fifo(value);
    }

    fn loopback_mdmctrl(&mut self) {
        if !self.loopback_enabled() {
            return;
        }

        /*
         * Loopback software-driven modem control outputs to modem status inputs:
         *   FR.RI  <= CR.Out2
         *   FR.DCD <= CR.Out1
         *   FR.CTS <= CR.RTS
         *   FR.DSR <= CR.DTR
         *
         * The loopback happens immediately even if this call is triggered
         * by setting only CR.LBE.
         *
         * CTS/RTS updates due to enabled hardware flow controls are not
         * dealt with here.
         */

        //fr = s->flags & ~(PL011_FLAG_RI | PL011_FLAG_DCD |
        //                  PL011_FLAG_DSR | PL011_FLAG_CTS);
        //fr |= (cr & CR_OUT2) ? PL011_FLAG_RI  : 0;
        //fr |= (cr & CR_OUT1) ? PL011_FLAG_DCD : 0;
        //fr |= (cr & CR_RTS)  ? PL011_FLAG_CTS : 0;
        //fr |= (cr & CR_DTR)  ? PL011_FLAG_DSR : 0;
        //
        self.flags.set_ring_indicator(self.control.out_2());
        self.flags.set_data_carrier_detect(self.control.out_1());
        self.flags.set_clear_to_send(self.control.request_to_send());
        self.flags
            .set_data_set_ready(self.control.data_transmit_ready());

        // Change interrupts based on updated FR
        let mut il = self.int_level;

        il &= !Interrupt::MS;
        //il |= (fr & PL011_FLAG_DSR) ? INT_DSR : 0;
        //il |= (fr & PL011_FLAG_DCD) ? INT_DCD : 0;
        //il |= (fr & PL011_FLAG_CTS) ? INT_CTS : 0;
        //il |= (fr & PL011_FLAG_RI)  ? INT_RI  : 0;

        if self.flags.data_set_ready() {
            il |= Interrupt::DSR as u32;
        }
        if self.flags.data_carrier_detect() {
            il |= Interrupt::DCD as u32;
        }
        if self.flags.clear_to_send() {
            il |= Interrupt::CTS as u32;
        }
        if self.flags.ring_indicator() {
            il |= Interrupt::RI as u32;
        }
        self.int_level = il;
        self.update();
    }

    fn loopback_break(&mut self, enable: bool) {
        if enable {
            self.loopback_tx(DATA_BREAK);
        }
    }

    fn set_read_trigger(&mut self) {
        //#if 0
        //    /* The docs say the RX interrupt is triggered when the FIFO exceeds
        //       the threshold.  However linux only reads the FIFO in response to an
        //       interrupt.  Triggering the interrupt when the FIFO is non-empty seems
        //       to make things work.  */
        //    if (s->lcr & LCR_FEN)
        //        s->read_trigger = (s->ifl >> 1) & 0x1c;
        //    else
        //#endif
        self.read_trigger = 1;
    }

    pub fn realize(&mut self) {
        unsafe {
            qemu_chr_fe_set_handlers(
                addr_of_mut!(self.char_backend),
                Some(pl011_can_receive),
                Some(pl011_receive),
                Some(pl011_event),
                None,
                addr_of_mut!(*self).cast::<c_void>(),
                core::ptr::null_mut(),
                true,
            );
        }
    }

    pub fn reset(&mut self) {
        self.line_control.reset();
        self.receive_status_error_clear.reset();
        self.dmacr = 0;
        self.int_enabled = 0;
        self.int_level = 0;
        self.ilpr = 0;
        self.ibrd = 0;
        self.fbrd = 0;
        self.read_trigger = 1;
        self.ifl = 0x12;
        self.control.reset();
        self.flags = 0.into();
        self.reset_fifo();
    }

    pub fn reset_fifo(&mut self) {
        self.read_count = 0;
        self.read_pos = 0;

        /* Reset FIFO flags */
        self.flags.reset();
    }

    pub fn can_receive(&self) -> bool {
        // trace_pl011_can_receive(s->lcr, s->read_count, r);
        self.read_count < self.fifo_depth()
    }

    pub fn event(&mut self, event: QEMUChrEvent) {
        if event == generated::QEMUChrEvent_CHR_EVENT_BREAK && !self.fifo_enabled() {
            self.put_fifo(DATA_BREAK);
            self.receive_status_error_clear.set_break_error(true);
        }
    }

    #[inline]
    pub fn fifo_enabled(&self) -> bool {
        matches!(self.line_control.fifos_enabled(), registers::Mode::FIFO)
    }

    #[inline]
    pub fn loopback_enabled(&self) -> bool {
        self.control.enable_loopback()
    }

    #[inline]
    pub fn fifo_depth(&self) -> usize {
        // Note: FIFO depth is expected to be power-of-2
        if self.fifo_enabled() {
            return PL011_FIFO_DEPTH;
        }
        1
    }

    pub fn put_fifo(&mut self, value: c_uint) {
        let depth = self.fifo_depth();
        assert!(depth > 0);
        let slot = (self.read_pos + self.read_count) & (depth - 1);
        self.read_fifo[slot] = value;
        self.read_count += 1;
        // s->flags &= ~PL011_FLAG_RXFE;
        self.flags.set_receive_fifo_empty(false);
        if self.read_count == depth {
            //s->flags |= PL011_FLAG_RXFF;
            self.flags.set_receive_fifo_full(true);
        }

        if self.read_count == self.read_trigger {
            self.int_level |= registers::INT_RX;
            self.update();
        }
    }

    pub fn update(&mut self) {
        let flags = self.int_level & self.int_enabled;
        for (irq, i) in self.interrupts.iter().zip(IRQMASK) {
            unsafe { qemu_set_irq(*irq, ((flags & i) != 0) as u32 as c_int) };
        }
    }
}

/// Which bits in the interrupt status matter for each outbound IRQ line ?
pub const IRQMASK: [u32; 6] = [
    /* combined IRQ */
    Interrupt::E
        | Interrupt::MS
        | Interrupt::RT as u32
        | Interrupt::TX as u32
        | Interrupt::RX as u32,
    Interrupt::RX as u32,
    Interrupt::TX as u32,
    Interrupt::RT as u32,
    Interrupt::MS,
    Interrupt::E,
];

pub unsafe extern "C" fn pl011_can_receive(opaque: *mut c_void) -> c_int {
    assert!(!opaque.is_null());
    let state = NonNull::new_unchecked(opaque.cast::<PL011State>());
    state.as_ref().can_receive().into()
}
pub unsafe extern "C" fn pl011_receive(
    opaque: *mut core::ffi::c_void,
    buf: *const u8,
    size: core::ffi::c_int,
) {
    assert!(!opaque.is_null());
    let mut state = NonNull::new_unchecked(opaque.cast::<PL011State>());
    if state.as_ref().loopback_enabled() {
        return;
    }
    if size > 0 {
        assert!(!buf.is_null());
        state.as_mut().put_fifo(*buf.cast::<c_uint>())
    }
}

pub unsafe extern "C" fn pl011_event(opaque: *mut core::ffi::c_void, event: QEMUChrEvent) {
    assert!(!opaque.is_null());
    let mut state = NonNull::new_unchecked(opaque.cast::<PL011State>());
    state.as_mut().event(event)
}

pub const VMSTATE_PL011: VMStateDescription = VMStateDescription {
    name: PL011_ARM_INFO.name,
    unmigratable: true,
    ..unsafe { MaybeUninit::<VMStateDescription>::zeroed().assume_init() }
};
//version_id : 2,
//minimum_version_id : 2,
//post_load : pl011_post_load,
//fields = (const VMStateField[]) {
//    VMSTATE_UINT32(readbuff, PL011State),
//    VMSTATE_UINT32(flags, PL011State),
//    VMSTATE_UINT32(lcr, PL011State),
//    VMSTATE_UINT32(rsr, PL011State),
//    VMSTATE_UINT32(cr, PL011State),
//    VMSTATE_UINT32(dmacr, PL011State),
//    VMSTATE_UINT32(int_enabled, PL011State),
//    VMSTATE_UINT32(int_level, PL011State),
//    VMSTATE_UINT32_ARRAY(read_fifo, PL011State, PL011_FIFO_DEPTH),
//    VMSTATE_UINT32(ilpr, PL011State),
//    VMSTATE_UINT32(ibrd, PL011State),
//    VMSTATE_UINT32(fbrd, PL011State),
//    VMSTATE_UINT32(ifl, PL011State),
//    VMSTATE_INT32(read_pos, PL011State),
//    VMSTATE_INT32(read_count, PL011State),
//    VMSTATE_INT32(read_trigger, PL011State),
//    VMSTATE_END_OF_LIST()
//},
//.subsections = (const VMStateDescription * const []) {
//    &vmstate_pl011_clock,
//    NULL
//}

pub unsafe extern "C" fn pl011_create(
    addr: u64,
    irq: qemu_irq,
    chr: *mut Chardev,
) -> *mut DeviceState {
    let dev: *mut DeviceState = unsafe { qdev_new(PL011_ARM_INFO.name) };
    assert!(!dev.is_null());
    let sysbus: *mut SysBusDevice = dev as *mut SysBusDevice;

    unsafe {
        qdev_prop_set_chr(dev, generated::TYPE_CHARDEV.as_ptr(), chr);
        sysbus_realize_and_unref(sysbus, addr_of!(error_fatal) as *mut *mut Error);
        sysbus_mmio_map(sysbus, 0, addr);
        sysbus_connect_irq(sysbus, 0, irq);
    }
    dev
}
