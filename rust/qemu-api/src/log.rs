// Copyright 2025 Bernhard Beschow <shentey@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

pub use crate::bindings::{LOG_GUEST_ERROR, LOG_INVALID_MEM, LOG_UNIMP};

/// A macro to log messages conditionally based on a provided mask.
///
/// The `qemu_log_mask` macro checks whether the given mask matches the
/// current log level and, if so, formats and logs the message.
///
/// # Parameters
///
/// - `$mask`: The log mask to check. This should be an integer value
///   indicating the log level mask.
/// - `$fmt`: A format string following the syntax and rules of the `format!`
///   macro. It specifies the structure of the log message.
/// - `$args`: Optional arguments to be interpolated into the format string.
///
/// # Example
///
/// ```
/// use qemu_api::log::LOG_GUEST_ERROR;
///
/// qemu_log_mask!(
///     LOG_GUEST_ERROR,
///     "Address 0x{error_address:x} out of range\n"
/// );
/// ```
///
/// It is also possible to use printf style:
///
/// ```
/// use qemu_api::log::LOG_GUEST_ERROR;
///
/// qemu_log_mask!(
///     LOG_GUEST_ERROR,
///     "Address 0x{:x} out of range\n",
///     error_address
/// );
/// ```
#[macro_export]
macro_rules! qemu_log_mask {
    ($mask:expr, $fmt:expr $(, $args:expr)*) => {{
        if unsafe {
            (::qemu_api::bindings::qemu_loglevel & ($mask as std::os::raw::c_int)) != 0
        } {
            let formatted_string = format!($fmt, $($args),*);
            let c_string = std::ffi::CString::new(formatted_string).unwrap();

            unsafe {
                ::qemu_api::bindings::qemu_log(c_string.as_ptr());
            }
        }
    }};
}
