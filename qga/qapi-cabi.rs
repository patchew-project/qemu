pub use common::{err, Error, Result};
mod qapi_ffi;

fn main() {
    qapi_ffi::cabi()
}
