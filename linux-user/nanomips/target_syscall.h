/* this struct defines the way the registers are stored on the
   stack during a system call. */

struct target_pt_regs {
    /* Pad bytes for argument save space on the stack. */
    abi_ulong pad0[6];

    /* Saved main processor registers. */
    abi_ulong regs[32];

    /* Saved special registers. */
    abi_ulong cp0_status;
    abi_ulong lo;
    abi_ulong hi;
    abi_ulong cp0_badvaddr;
    abi_ulong cp0_cause;
    abi_ulong cp0_epc;
};

/* Nasty hack: define a fake errno value for use by sigreturn.  */
#undef TARGET_QEMU_ESIGRETURN
#define TARGET_QEMU_ESIGRETURN 255

#define UNAME_MACHINE "nanomips"
#define UNAME_MINIMUM_RELEASE "2.6.32"

#define TARGET_CLONE_BACKWARDS
#define TARGET_MINSIGSTKSZ 6144
#define TARGET_MLOCKALL_MCL_CURRENT 1
#define TARGET_MLOCKALL_MCL_FUTURE  2
