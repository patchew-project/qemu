/* TODO: Write a derive(Endianness) macro */

#[repr(C, packed)]
#[derive(Default)]
pub struct QCow2Header {
    pub magic: u32,
    pub version: u32,
    pub backing_file_offset: u64,
    pub backing_file_size: u32,
    pub cluster_bits: u32,
    pub size: u64,
    pub crypt_method: u32,
    pub l1_size: u32,
    pub l1_table_offset: u64,
    pub refcount_table_offset: u64,
    pub refcount_table_clusters: u32,
    pub nb_snapshots: u32,
    pub snapshots_offset: u64,

    pub incompatible_features: u64,
    pub compatible_features: u64,
    pub autoclear_features: u64,

    pub refcount_order: u32,
    pub header_length: u32,
}


impl QCow2Header {
    pub fn from_be(&mut self)
    {
        self.magic                  = u32::from_be(self.magic);
        self.version                = u32::from_be(self.version);

        self.backing_file_offset    = u64::from_be(self.backing_file_offset);
        self.backing_file_size      = u32::from_be(self.backing_file_size);

        self.cluster_bits           = u32::from_be(self.cluster_bits);
        self.size                   = u64::from_be(self.size);
        self.crypt_method           = u32::from_be(self.crypt_method);

        self.l1_size                = u32::from_be(self.l1_size);
        self.l1_table_offset        = u64::from_be(self.l1_table_offset);

        self.refcount_table_offset  = u64::from_be(self.refcount_table_offset);
        self.refcount_table_clusters
            = u32::from_be(self.refcount_table_clusters);

        self.nb_snapshots           = u32::from_be(self.nb_snapshots);
        self.snapshots_offset       = u64::from_be(self.snapshots_offset);

        self.incompatible_features  = u64::from_be(self.incompatible_features);
        self.compatible_features    = u64::from_be(self.compatible_features);
        self.autoclear_features     = u64::from_be(self.autoclear_features);

        self.refcount_order         = u32::from_be(self.refcount_order);
        self.header_length          = u32::from_be(self.header_length);
    }
}
