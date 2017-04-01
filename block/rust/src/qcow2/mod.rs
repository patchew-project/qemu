mod allocation;
mod io;
mod refcount;
mod on_disk_structures;


use interface::*;
use self::on_disk_structures::*;


const MIN_CLUSTER_BITS: u32 =  9;
const MAX_CLUSTER_BITS: u32 = 21;
const MAX_L1_SIZE       : u32 = 0x02000000u32;
const MAX_REFTABLE_SIZE : u32 = 0x00800000u32;
const L1E_OFFSET_MASK       : u64 = 0x00fffffffffffe00u64;
const L2E_OFFSET_MASK       : u64 = 0x00fffffffffffe00u64;
const L2E_COMPRESSED_MASK   : u64 = 0x3fffffffffffffffu64;
const REFT_OFFSET_MASK      : u64 = 0xfffffffffffffe00u64;

const OFLAG_COPIED      : u64 = 1u64 << 63;
const OFLAG_COMPRESSED  : u64 = 1u64 << 62;
const OFLAG_ZERO        : u64 = 1u64 <<  0;


pub struct QCow2BDS {
    common: BDSCommon<QCow2BDS>,

    qcow_version: u8,

    cluster_bits: u8,
    cluster_size: u32,
    cluster_sectors: u32,

    l1_bits: u8,
    l1_size: u32,
    l2_bits: u8,
    l2_size: u32,

    l1_offset: u64,
    l1_table: Vec<u64>,

    refcount_order: u8,
    reftable_bits: u8,
    refblock_size: u32,

    reftable_offset: u64,
    reftable_size: u32,
    reftable: Vec<u64>,

    first_free_cluster_offset: u64,
}


impl QCow2BDS {
    fn do_open(cbds: &mut CBDS, _: QDict, _: u32)
        -> Result<(), String>
    {
        let file = {
            let_mut_bds!(this, cbds);
            this.common.file()
        };

        let mut header = QCow2Header::default();
        try_prepend!(file.bdrv_pread(0, object_as_mut_byte_slice(&mut header)),
                     "Could not read qcow2 header");

        header.from_be();

        let reftable_size;

        {
            let_mut_bds!(this, cbds);

            if header.magic != 0x514649fb {
                return Err(String::from("Image is not in qcow2 format"));
            }
            if header.version < 2 || header.version > 3 {
                return Err(format!("Unsupported qcow2 version {}",
                                   header.version));
            }

            this.qcow_version = header.version as u8;

            if header.cluster_bits < MIN_CLUSTER_BITS ||
                header.cluster_bits > MAX_CLUSTER_BITS
            {
                return Err(format!("Unsupported cluster size: 2^{}",
                                   header.cluster_bits));
            }

            this.cluster_bits = header.cluster_bits as u8;
            this.cluster_size = 1u32 << this.cluster_bits;
            this.cluster_sectors = this.cluster_size >> BDRV_SECTOR_SHIFT;

            if this.qcow_version > 2 {
                if header.header_length < 104 {
                    return Err(String::from("qcow2 header too short"));
                }
                if header.header_length > this.cluster_size {
                    return Err(String::from("qcow2 header exceeds cluster \
                                             size"));
                }
            }

            if header.backing_file_offset > (this.cluster_size as u64) {
                return Err(String::from("Invalid backing file offset"));
            }

            if this.qcow_version > 2 {
                if header.incompatible_features != 0 {
                    return Err(format!("Unsupported incompatible features: \
                                        {:x}", header.incompatible_features));
                }

                if header.refcount_order > 6 {
                    return Err(String::from("Refcount width may not exceed 64 \
                                             bits"));
                }
                this.refcount_order = header.refcount_order as u8;
            }

            /* No need to do anything about snapshots, compression, encryption,
             * or other funky extensions: We do not support them */

            if header.crypt_method != 0 {
                return Err(format!("Unsupported encryption method: {}",
                                   header.crypt_method));
            }

            if header.backing_file_size > 1023 ||
                (header.backing_file_size as u64) >
                    (this.cluster_size as u64) - header.backing_file_offset
            {
                return Err(String::from("Backing file name too long"));
            }


            this.l2_bits = this.cluster_bits - 3 /* ld(sizeof(u64)) */;
            this.l2_size = 1u32 << this.l2_bits;

            cbds.total_sectors = (header.size / BDRV_SECTOR_SIZE) as i64;

            this.l1_offset = header.l1_table_offset;
            this.l1_bits = this.cluster_bits + this.l2_bits;

            if header.l1_size > MAX_L1_SIZE / 8 {
                return Err(String::from("Active L1 table too large"));
            }

            let min_l1_size = (header.size + (1u64 << this.l1_bits) - 1) >>
                                  this.l1_bits;
            if (header.l1_size as u64) < min_l1_size || header.l1_size == 0 {
                return Err(String::from("Active L1 table too small"));
            }

            this.l1_size = header.l1_size;

            this.reftable_offset = header.refcount_table_offset;

            reftable_size = (header.refcount_table_clusters as u64) <<
                                (this.cluster_bits - 3);
            if reftable_size > (MAX_REFTABLE_SIZE as u64) {
                return Err(String::from("Refcount table too large"));
            }

            this.reftable_size = reftable_size as u32;

            let refblock_bits = this.cluster_bits + this.refcount_order - 3;
            this.reftable_bits = this.cluster_bits + refblock_bits;
            this.refblock_size = 1u32 << refblock_bits;
        }

        /* Read L1 table */
        let mut l1_table = Vec::<u64>::new();
        l1_table.resize(header.l1_size as usize, 0);

        try_prepend!(file.bdrv_pread(header.l1_table_offset,
                                     vec_as_mut_byte_slice(&mut l1_table)),
                     "Could not read L1 table");

        for i in 0..header.l1_size {
            l1_table[i as usize] = u64::from_be(l1_table[i as usize]);
        }

        /* Read reftable */
        let mut reftable = Vec::<u64>::new();
        reftable.resize(reftable_size as usize, 0);

        try_prepend!(file.bdrv_pread(header.refcount_table_offset,
                                     vec_as_mut_byte_slice(&mut reftable)),
                     "Could not read refcount table");

        for i in 0..reftable_size {
            reftable[i as usize] = u64::from_be(reftable[i as usize]);
        }

        /* Read backing file name */
        try_prepend!(
            file.bdrv_pread(header.backing_file_offset,
                            slice_as_mut_byte_slice(&mut cbds.backing_file)),
            "Could not read backing file name");
        cbds.backing_file[header.backing_file_size as usize] = 0;

        {
            let_mut_bds!(this, cbds);
            this.l1_table = l1_table;
            this.reftable = reftable;
        }

        Ok(())
    }
}


impl BlockDriverState for QCow2BDS {
    fn new() -> Self
    {
        QCow2BDS {
            common: BDSCommon::<Self>::new(),

            qcow_version: 0,

            cluster_bits: 0,
            cluster_size: 0,
            cluster_sectors: 0,

            l1_bits: 0,
            l1_size: 0,
            l2_bits: 0,
            l2_size: 0,

            l1_offset: 0,
            l1_table: Vec::new(),

            refcount_order: 0,
            reftable_bits: 0,
            refblock_size: 0,

            reftable_offset: 0,
            reftable_size: 0,
            reftable: Vec::new(),

            first_free_cluster_offset: 0,
        }
    }

    /* Required for the generic BlockDriverState implementation */
    fn common(&mut self) -> &mut BDSCommon<Self>
    {
        &mut self.common
    }
}


impl BlockDriverOpen for QCow2BDS {
    fn bdrv_open(cbds: &mut CBDS, options: QDict, flags: u32)
        -> Result<(), String>
    {
        let role = bdrv_get_standard_child_role(StandardChildRole::File);
        let file = try!(bdrv_open_child(None, Some(options),
                                        String::from("file"), cbds, role,
                                        false));

        {
            let_mut_bds!(this, cbds);
            this.common.set_file(Some(file));
        }

        QCow2BDS::do_open(cbds, options, flags)
    }
}


impl BlockDriverClose for QCow2BDS {
    fn bdrv_close(_: &mut CBDS)
    {
    }
}


impl BlockDriverRead for QCow2BDS {
    fn bdrv_co_preadv(cbds: &mut CBDS, offset: u64, bytes: u64,
                      iov: Vec<&mut [u8]>, flags: u32)
        -> Result<(), IOError>
    {
        /* TODO: Do not split */
        Self::split_io_to_clusters(cbds, offset, bytes, io::MNMIOV::Mut(iov),
                                   flags, &Self::read_cluster)
    }
}


impl BlockDriverWrite for QCow2BDS {
    fn bdrv_co_pwritev(cbds: &mut CBDS, offset: u64, bytes: u64,
                       iov: Vec<&[u8]>, flags: u32)
        -> Result<(), IOError>
    {
        /* TODO: Do not split */
        Self::split_io_to_clusters(cbds, offset, bytes, io::MNMIOV::Const(iov),
                                   flags, &Self::write_cluster)
    }
}


impl BlockDriverChildPerm for QCow2BDS {
    fn bdrv_child_perm(cbds: &mut CBDS, c: Option<&mut BdrvChild>,
                       role: &c_structs::BdrvChildRole, perm: u64, shared: u64)
        -> (u64, u64)
    {
        bdrv_format_default_perms(c, role, perm, shared,
                                  bdrv_is_read_only(cbds))
    }
}


impl BlockDriverInfo for QCow2BDS {
    fn bdrv_get_info(cbds: &mut CBDS, bdi: &mut c_structs::BlockDriverInfo)
        -> Result<(), String>
    {
        let_bds!(this, cbds);

        bdi.unallocated_blocks_are_zero = true;
        bdi.can_write_zeroes_with_unmap = false; /* no discard support */
        bdi.cluster_size = this.cluster_size as i32;
        /* no VM state support */

        Ok(())
    }
}


#[no_mangle]
pub extern fn bdrv_qcow2_rust_init()
{
    let mut bdrv = BlockDriver::<QCow2BDS>::new(String::from("qcow2-rust"));

    bdrv.provides_open();
    bdrv.provides_close();
    bdrv.provides_read();
    bdrv.provides_write();
    bdrv.provides_child_perm();
    bdrv.provides_info();

    bdrv.supports_backing();

    bdrv_register(bdrv);
}
