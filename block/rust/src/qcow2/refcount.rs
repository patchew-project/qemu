use interface::*;
use qcow2::*;


impl QCow2BDS {
    pub fn get_refcount(cbds: &mut CBDS, offset: u64) -> Result<u64, IOError>
    {
        let refblock_index;
        let refcount_order;
        let cluster_size;
        let file;

        let reftable_entry = {
            let_bds!(this, cbds);

            refcount_order = this.refcount_order;
            cluster_size = this.cluster_size;
            file = this.common.file();

            let cluster_index = offset >> this.cluster_bits;
            let reftable_index = offset >> this.reftable_bits;

            refblock_index = (cluster_index as u32) & (this.refblock_size - 1);

            if reftable_index >= (this.reftable_size as u64) {
                0
            } else {
                this.reftable[reftable_index as usize]
            }
        };

        let refblock_offset = reftable_entry & REFT_OFFSET_MASK;

        if refblock_offset == 0 {
            return Ok(0);
        }

        if (refblock_offset & ((cluster_size - 1) as u64)) != 0 {
            return Err(IOError::InvalidMetadata);
        }

        assert!(refcount_order <= 6);
        if refcount_order == 6 {
            let mut refcount: u64 = 0;
            let byte_offset = (refblock_index * 8) as u64;
            if let Err(_) =
                file.bdrv_pread(refblock_offset + byte_offset,
                                object_as_mut_byte_slice(&mut refcount))
            {
                return Err(IOError::GenericError);
            }
            Ok(u64::from_be(refcount))
        } else if refcount_order == 5 {
            let mut refcount: u32 = 0;
            let byte_offset = (refblock_index * 4) as u64;
            if let Err(_) =
                file.bdrv_pread(refblock_offset + byte_offset,
                                object_as_mut_byte_slice(&mut refcount))
            {
                return Err(IOError::GenericError);
            }
            Ok(u32::from_be(refcount) as u64)
        } else if refcount_order == 4 {
            let mut refcount: u16 = 0;
            let byte_offset = (refblock_index * 2) as u64;
            if let Err(_) =
                file.bdrv_pread(refblock_offset + byte_offset,
                                object_as_mut_byte_slice(&mut refcount))
            {
                return Err(IOError::GenericError);
            }
            Ok(u16::from_be(refcount) as u64)
        } else {
            let mut refcount_byte: u8 = 0;
            let byte_offset = (refblock_index >> (3 - refcount_order)) as u64;
            if let Err(_) =
                file.bdrv_pread(refblock_offset + byte_offset,
                                object_as_mut_byte_slice(&mut refcount_byte))
            {
                return Err(IOError::GenericError);
            }

            let mask = ((1u16 << (1u8 << refcount_order)) - 1) as u8;
            let shift = (refblock_index << refcount_order) & 0x7;

            Ok(((refcount_byte >> shift) & mask) as u64)
        }
    }


    pub fn change_refcount(_: &mut CBDS, _: u64, _: i8)
        -> Result<(), IOError>
    {
        Err(IOError::UnsupportedImageFeature)
    }
}
