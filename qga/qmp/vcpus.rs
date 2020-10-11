#[cfg(unix)]
use std::fs::OpenOptions;
#[cfg(unix)]
use std::io::ErrorKind;
#[cfg(unix)]
use std::os::unix::fs::FileExt;

#[cfg(windows)]
use winapi::um::{sysinfoapi, winnt};

use crate::*;

#[cfg(target_os = "linux")]
fn get_sysfs_cpu_path(id: i64) -> String {
    format!("/sys/devices/system/cpu/cpu{}", id)
}

#[cfg(target_os = "linux")]
fn set_vcpu(vcpu: &qapi::GuestLogicalProcessor) -> Result<()> {
    let path = get_sysfs_cpu_path(vcpu.logical_id);
    std::fs::metadata(&path)?;

    let path = format!("{}/online", path);
    match OpenOptions::new().read(true).write(true).open(&path) {
        Ok(file) => {
            let mut buf = [0u8; 1];
            file.read_exact_at(&mut buf, 0)?;
            let online = buf[0] != 0;
            if vcpu.online != online {
                buf[0] = if vcpu.online { b'1' } else { b'0' };
                file.write_all_at(&buf, 0)?;
            }
        }
        Err(e) => {
            if e.kind() != ErrorKind::NotFound {
                return Err(e.into());
            } else if !vcpu.online {
                return err!(format!(
                    "logical processor #{} can't be offlined",
                    vcpu.logical_id
                ));
            }
        }
    }

    Ok(())
}

#[cfg(not(target_os = "linux"))]
fn set_vcpu(_vcpu: &qapi::GuestLogicalProcessor) -> Result<()> {
    err!("unimplemented")
}

pub(crate) fn set(vcpus: Vec<qapi::GuestLogicalProcessor>) -> Result<i64> {
    let mut processed = 0;

    for vcpu in &vcpus {
        if let Err(e) = set_vcpu(vcpu) {
            if processed != 0 {
                break;
            }
            return Err(e);
        }

        processed += 1;
    }

    Ok(processed)
}

#[cfg(target_os = "linux")]
pub(crate) fn get() -> Result<Vec<qapi::GuestLogicalProcessor>> {
    use nix::unistd::sysconf;

    let mut vcpus = vec![];
    let nproc_conf = match sysconf(unsafe { std::mem::transmute(libc::_SC_NPROCESSORS_CONF) })? {
        Some(nproc) => nproc,
        None => {
            return err!("Indefinite number of processors.");
        }
    };

    for logical_id in 0..nproc_conf {
        let path = get_sysfs_cpu_path(logical_id);
        if std::fs::metadata(&path).is_err() {
            continue;
        }

        let path = format!("{}/online", path);
        let (online, can_offline) = match OpenOptions::new().read(true).open(&path) {
            Ok(file) => {
                let mut buf = [0u8; 1];
                file.read_exact_at(&mut buf, 0)?;
                (buf[0] != 0, Some(true))
            }
            Err(e) => {
                if e.kind() != ErrorKind::NotFound {
                    return Err(e.into());
                }
                (true, Some(false))
            }
        };

        vcpus.push(qapi::GuestLogicalProcessor {
            logical_id,
            online,
            can_offline,
        });
    }

    Ok(vcpus)
}

#[cfg(target_os = "windows")]
fn get_logical_processor_info() -> Result<Vec<winnt::SYSTEM_LOGICAL_PROCESSOR_INFORMATION>> {
    unsafe {
        let mut needed_size = 0;
        sysinfoapi::GetLogicalProcessorInformation(std::ptr::null_mut(), &mut needed_size);
        let struct_size = std::mem::size_of::<winnt::SYSTEM_LOGICAL_PROCESSOR_INFORMATION>() as u32;
        if needed_size == 0 || needed_size < struct_size || needed_size % struct_size != 0 {
            return err!("Failed to get processor information");
        }

        let nstruct = needed_size / struct_size;
        let mut buf = Vec::with_capacity(nstruct as usize);
        let result = sysinfoapi::GetLogicalProcessorInformation(buf.as_mut_ptr(), &mut needed_size);
        if result == 0 {
            return err!("Failed to get processor information");
        }

        let nstruct = needed_size / struct_size;
        buf.set_len(nstruct as usize);
        Ok(buf)
    }
}

#[cfg(target_os = "windows")]
pub(crate) fn get() -> Result<Vec<qapi::GuestLogicalProcessor>> {
    let mut vcpus = vec![];

    get_logical_processor_info()?.iter().map(|info| {
        for _ in 0..info.ProcessorMask.count_ones() {
            vcpus.push(qapi::GuestLogicalProcessor {
                logical_id: vcpus.len() as i64,
                online: true,
                can_offline: Some(false),
            });
        }
    });

    if vcpus.is_empty() {
        return err!("Guest reported zero VCPUs");
    }

    Ok(vcpus)
}

#[cfg(not(any(target_os = "linux", target_os = "windows")))]
pub(crate) fn get() -> Result<Vec<qapi::GuestLogicalProcessor>> {
    err!("unimplemented")
}
