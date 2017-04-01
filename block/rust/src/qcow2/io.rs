use interface::*;
use qcow2::*;


pub enum MNMIOV<'a> {
    Mut(Vec<&'a mut [u8]>),
    Const(Vec<&'a [u8]>),
}

pub enum MNMIOVSlice<'a> {
    Mut(&'a mut [u8]),
    Const(&'a [u8]),
}


pub struct HostOffsetInfo {
    pub guest_offset: u64,

    pub cluster_size: u32,
    pub compressed_shift: u8,

    pub file: BdrvChild,

    pub l1_index: u32,
    pub l2_index: u32,
    pub offset_in_cluster: u32,

    pub l1_entry: L1Entry,
    pub l2_entry: Option<L2Entry>,
}

pub enum L1Entry {
    Unallocated,

    /* L2 offset, COPIED */
    Allocated(u64, bool),
}

pub enum L2Entry {
    Unallocated,

    /* Offset, COPIED */
    Normal(u64, bool),

    /* Offset (if allocated), COPIED */
    Zero(Option<u64>, bool),

    /* Offset, compressed length */
    Compressed(u64, usize),
}

impl L1Entry {
    pub fn from_bits(l1_entry: u64, cluster_size: u32) -> Result<L1Entry, IOError>
    {
        let l2_offset = l1_entry & L1E_OFFSET_MASK;

        if l2_offset == 0 {
            Ok(L1Entry::Unallocated)
        } else if (l2_offset & ((cluster_size - 1) as u64)) != 0 {
            Err(IOError::InvalidMetadata)
        } else {
            Ok(L1Entry::Allocated(l2_offset, (l1_entry & OFLAG_COPIED) != 0))
        }
    }


    pub fn to_bits(&self) -> u64
    {
        match *self {
            L1Entry::Unallocated                => 0u64,
            L1Entry::Allocated(offset, false)   => offset,
            L1Entry::Allocated(offset, true)    => offset | OFLAG_COPIED,
        }
    }
}

impl L2Entry {
    pub fn from_bits(l2_entry: u64, cluster_size: u32, compressed_shift: u8)
        -> Result<L2Entry, IOError>
    {
        if (l2_entry & OFLAG_COMPRESSED) != 0 {
            let offset = l2_entry & ((1u64 << compressed_shift) - 1);
            let sectors = (l2_entry & L2E_COMPRESSED_MASK) >> compressed_shift;
            let length = sectors * BDRV_SECTOR_SIZE;

            Ok(L2Entry::Compressed(offset, length as usize))
        } else {
            let offset = l2_entry & L2E_OFFSET_MASK;
            let copied = (l2_entry & OFLAG_COPIED) != 0;

            if (offset & ((cluster_size - 1) as u64)) != 0 {
                Err(IOError::InvalidMetadata)
            } else {
                if (l2_entry & OFLAG_ZERO) != 0 {
                    if offset == 0 {
                        Ok(L2Entry::Zero(None, false))
                    } else {
                        Ok(L2Entry::Zero(Some(offset), copied))
                    }
                } else {
                    if offset == 0 {
                        Ok(L2Entry::Unallocated)
                    } else {
                        Ok(L2Entry::Normal(offset, copied))
                    }
                }
            }
        }
    }


    pub fn to_bits(&self, compressed_shift: u8) -> u64
    {
        match *self {
            L2Entry::Unallocated                => 0u64,
            L2Entry::Normal(offset, false)      => offset,
            L2Entry::Normal(offset, true)       => offset | OFLAG_COPIED,
            L2Entry::Zero(None, _)              => OFLAG_ZERO,
            L2Entry::Zero(Some(offset), false)  => offset | OFLAG_ZERO,
            L2Entry::Zero(Some(offset), true)   => offset
                                                   | OFLAG_COPIED | OFLAG_ZERO,

            L2Entry::Compressed(offset, length) => {
                let secs = ((length as u64) + BDRV_SECTOR_SIZE - 1)
                               / BDRV_SECTOR_SIZE;

                assert!((offset & !((1u64 << compressed_shift) - 1)) == 0);
                assert!((secs << compressed_shift) >> compressed_shift == secs);

                offset | (secs << compressed_shift) | OFLAG_COMPRESSED
            }
        }
    }
}


impl QCow2BDS {
    pub fn split_io_to_clusters(cbds: &mut CBDS, mut offset: u64, bytes: u64,
                                mut iov_mnm: MNMIOV, flags: u32,
                                func: &Fn(&mut CBDS, u64, u32, &mut MNMIOVSlice,
                                          u32)
                                     -> Result<(), IOError>)
        -> Result<(), IOError>
    {
        let cluster_size = {
            let_bds!(this, cbds);
            this.cluster_size
        };

        let mut current_slice_outer: Option<MNMIOVSlice> = None;

        let end_offset = offset + bytes;
        while offset < end_offset {
            /* Using a single current_slice variable does not work, and my
             * knowledge of Rust does not suffice to explain why. */
            current_slice_outer = {
                let mut current_slice_opt = current_slice_outer;

                let mut cs_len;

                while (cs_len = match current_slice_opt {
                        None                                => 0,
                        Some(MNMIOVSlice::Mut(ref slice))   => slice.len(),
                        Some(MNMIOVSlice::Const(ref slice)) => slice.len(),
                    }, cs_len) == ((), 0)
                {
                    current_slice_opt = match iov_mnm {
                        MNMIOV::Mut(ref mut iov) =>
                            Some(MNMIOVSlice::Mut(iov.pop().unwrap())),

                        MNMIOV::Const(ref mut iov) =>
                            Some(MNMIOVSlice::Const(iov.pop().unwrap())),
                    }
                }

                let mut current_slice = current_slice_opt.unwrap();

                let mut this_bytes: u32 = cluster_size;
                if cs_len < (this_bytes as usize) {
                    this_bytes = cs_len as u32;
                }
                if end_offset - offset < (this_bytes as u64) {
                    this_bytes = (end_offset - offset) as u32;
                }

                try!(func(cbds, offset, this_bytes, &mut current_slice, flags));

                offset += this_bytes as u64;

                Some(match current_slice {
                    MNMIOVSlice::Mut(iov) =>
                        MNMIOVSlice::Mut(iov.split_at_mut(this_bytes as usize).1),

                    MNMIOVSlice::Const(iov) =>
                        MNMIOVSlice::Const(iov.split_at(this_bytes as usize).1),
                })
            };
        }

        Ok(())
    }


    fn do_backing_read(cbds: &mut CBDS, offset: u64, dest: &mut [u8])
        -> Result<(), IOError>
    {
        let backing = {
            let_mut_bds!(this, cbds);

            if !this.common.has_backing() {
                zero_byte_slice(dest);
                return Ok(());
            }

            this.common.backing()
        };

        match backing.bdrv_pread(offset, dest) {
            Ok(_) => Ok(()),
            Err(_) => Err(IOError::GenericError),
        }
    }


    fn find_host_offset(cbds: &mut CBDS, offset: u64)
        -> Result<HostOffsetInfo, IOError>
    {
        let mut res = {
            let_mut_bds!(this, cbds);

            let cluster_offset_mask = (this.cluster_size - 1) as u64;
            let l2_mask = (this.l2_size - 1) as u32;

            let l1_index = (offset >> this.l1_bits) as usize;

            HostOffsetInfo {
                guest_offset: offset,

                cluster_size: this.cluster_size,
                compressed_shift: 63 - (this.cluster_bits - 8),

                file: this.common.file(),

                l1_index: l1_index as u32,
                l2_index: ((offset >> this.cluster_bits) as u32) & l2_mask,
                offset_in_cluster: (offset & cluster_offset_mask) as u32,

                l1_entry: try!(L1Entry::from_bits(this.l1_table[l1_index],
                                                  this.cluster_size)),
                l2_entry: None,
            }
        };

        let mut l2_entry_offset;

        match res.l1_entry {
            L1Entry::Unallocated    => return Ok(res),
            L1Entry::Allocated(l2_offset, _) => l2_entry_offset = l2_offset,
        }

        l2_entry_offset += (res.l2_index as u64) * 8;

        let mut l2_entry = 0u64;
        if let Err(_) =
            res.file.bdrv_pread(l2_entry_offset,
                                object_as_mut_byte_slice(&mut l2_entry))
        {
            return Err(IOError::GenericError);
        }

        let l2_entry = try!(L2Entry::from_bits(u64::from_be(l2_entry),
                                               res.cluster_size,
                                               res.compressed_shift));
        res.l2_entry = Some(l2_entry);
        return Ok(res);
    }


    fn do_read_cluster(cbds: &mut CBDS, hoi: &HostOffsetInfo, dest: &mut [u8],
                       _: u32)
        -> Result<(), IOError>
    {
        match hoi.l2_entry {
            None | Some(L2Entry::Unallocated) =>
                Self::do_backing_read(cbds, hoi.guest_offset, dest),

            Some(L2Entry::Zero(_, _)) => {
                zero_byte_slice(dest);
                Ok(())
            },

            Some(L2Entry::Compressed(_, _)) =>
                Err(IOError::UnsupportedImageFeature),

            Some(L2Entry::Normal(offset, _)) => {
                let full_offset = offset + (hoi.offset_in_cluster as u64);
                if let Err(_) = hoi.file.bdrv_pread(full_offset, dest) {
                    Err(IOError::GenericError)
                } else {
                    Ok(())
                }
            }
        }
    }


    pub fn read_cluster(cbds: &mut CBDS, offset: u64, bytes: u32,
                        full_dest_mnm: &mut MNMIOVSlice, flags: u32)
        -> Result<(), IOError>
    {
        let mut dest = match *full_dest_mnm {
            MNMIOVSlice::Mut(ref mut full_dest) =>
                full_dest.split_at_mut(bytes as usize).0,

            MNMIOVSlice::Const(_) =>
                panic!("read_cluster() requires a mutable I/O vector"),
        };

        let hoi = try!(Self::find_host_offset(cbds, offset));
        Self::do_read_cluster(cbds, &hoi, dest, flags)
    }
}
