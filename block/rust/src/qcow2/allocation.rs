use interface::*;
use qcow2::*;
use qcow2::io::*;


impl QCow2BDS {
    fn allocate_cluster(cbds: &mut CBDS) -> Result<u64, IOError>
    {
        let (mut offset, cluster_size) = {
            let_bds!(this, cbds);
            (this.first_free_cluster_offset, this.cluster_size)
        };

        /* TODO: Optimize by scanning whole refblocks */
        while try!(Self::get_refcount(cbds, offset)) > 0 {
            offset += cluster_size as u64;
        }

        try!(Self::change_refcount(cbds, offset, 1));

        {
            let_mut_bds!(this, cbds);
            this.first_free_cluster_offset = offset + (cluster_size as u64);
        }

        Ok(offset)
    }


    pub fn allocate_l2(cbds: &mut CBDS, mut hoi: HostOffsetInfo)
        -> Result<HostOffsetInfo, IOError>
    {
        let offset = try!(Self::allocate_cluster(cbds));

        /* Zero the new table */
        {
            let zero_data = qemu_blockalign(cbds, hoi.cluster_size as usize);
            zero_byte_slice(zero_data);

            let res = hoi.file.bdrv_pwrite(offset, zero_data);
            qemu_vfree(zero_data);

            if let Err(_) = res {
                return Err(IOError::GenericError);
            }
        }

        hoi.l1_entry = L1Entry::Allocated(offset, true);
        try!(Self::update_l1_entry(cbds, &hoi));

        hoi.l2_entry = Some(L2Entry::Unallocated);

        Ok(hoi)
    }


    pub fn allocate_data_cluster(cbds: &mut CBDS, mut hoi: HostOffsetInfo)
        -> Result<HostOffsetInfo, IOError>
    {
        let offset = try!(Self::allocate_cluster(cbds));

        hoi.l2_entry = Some(L2Entry::Normal(offset, true));
        hoi = try!(Self::update_l2_entry(cbds, hoi));

        Ok(hoi)
    }


    pub fn free_cluster(cbds: &mut CBDS, l2e: L2Entry) -> Result<(), IOError>
    {
        match l2e {
            L2Entry::Unallocated    => Ok(()),
            L2Entry::Zero(None, _)  => Ok(()),

            L2Entry::Normal(offset, _)
            | L2Entry::Zero(Some(offset), _)
            | L2Entry::Compressed(offset, _) => {
                {
                    let_mut_bds!(this, cbds);
                    if offset < this.first_free_cluster_offset {
                        this.first_free_cluster_offset = offset;
                    }
                }

                Self::change_refcount(cbds, offset, -1)
            },
        }
    }


    pub fn update_l1_entry(cbds: &mut CBDS, hoi: &HostOffsetInfo)
        -> Result<(), IOError>
    {
        let (l1_offset, l1_size) = {
            let_bds!(this, cbds);
            (this.l1_offset, this.l1_table.len())
        };

        assert!((hoi.l1_index as usize) < l1_size);
        let entry_offset = l1_offset + (hoi.l1_index * 8) as u64;
        let l1_entry_cpu = hoi.l1_entry.to_bits();
        let l1_entry = u64::to_be(l1_entry_cpu);

        if let Err(_) = hoi.file.bdrv_pwrite(entry_offset,
                                             object_as_byte_slice(&l1_entry))
        {
            return Err(IOError::GenericError);
        }

        let_mut_bds!(this, cbds);
        this.l1_table[hoi.l1_index as usize] = l1_entry_cpu;

        Ok(())
    }


    /* hoi.l2_entry must be Some(_) */
    pub fn update_l2_entry(cbds: &mut CBDS, mut hoi: HostOffsetInfo)
        -> Result<HostOffsetInfo, IOError>
    {
        let (l2_offset, copied) = match hoi.l1_entry {
            L1Entry::Unallocated => panic!("L2 table must be allocated"),
            L1Entry::Allocated(o, c) => (o, c),
        };

        let l2_entry = u64::to_be(hoi.l2_entry.as_ref().unwrap()
                                              .to_bits(hoi.compressed_shift));

        if copied {
            let entry_offset = l2_offset + (hoi.l2_index * 8) as u64;
            if let Err(_) = hoi.file.bdrv_pwrite(entry_offset,
                                                 object_as_byte_slice(&l2_entry))
            {
                return Err(IOError::GenericError);
            }
        } else {
            let table_data = qemu_blockalign(cbds, hoi.cluster_size as usize);

            if let Err(_) = hoi.file.bdrv_pread(l2_offset, table_data) {
                qemu_vfree(table_data);
                return Err(IOError::GenericError);
            }

            copy_into_byte_slice(table_data, (hoi.l2_index * 8) as usize,
                                 object_as_byte_slice(&l2_entry));

            let new_offset = try_vfree!(Self::allocate_cluster(cbds),
                                        table_data);
            let res = hoi.file.bdrv_pwrite(new_offset, table_data);
            qemu_vfree(table_data);

            if let Err(_) = res {
                return Err(IOError::GenericError);
            }

            hoi.l1_entry = L1Entry::Allocated(new_offset, true);
            try!(Self::update_l1_entry(cbds, &hoi));
        }

        Ok(hoi)
    }
}
