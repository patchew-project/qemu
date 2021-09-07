use crate::*;

pub(crate) fn get() -> Result<qapi::GuestHostName> {
    let host_name = hostname::get()?
        .into_string()
        .or_else(|_| err!("Invalid hostname"))?;

    Ok(qapi::GuestHostName { host_name })
}
