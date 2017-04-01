use interface::c_structs::*;


extern {
    pub static child_file: BdrvChildRole;
    pub static child_format: BdrvChildRole;
    pub static child_backing: BdrvChildRole;
}
