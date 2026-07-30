PSEUDO_INST_DEF(InvalidPseudoInst,  ArgVoid, PSEUDO_INST_ARGVEC()),
// Identity mapping
PSEUDO_INST_DEF(IdentityMap,        ArgAny, PSEUDO_INST_ARGVEC(ArgAny)),
// Pointer arithmetic
PSEUDO_INST_DEF(PtrAdd,             ArgPtr, PSEUDO_INST_ARGVEC(ArgPtr, ArgInt)),
// Global accesses
PSEUDO_INST_DEF(AccessGlobalArray,  ArgInt, PSEUDO_INST_ARGVEC(ArgInt, ArgInt, ArgInt)),
PSEUDO_INST_DEF(AccessGlobalValue,  ArgInt, PSEUDO_INST_ARGVEC(ArgInt, ArgInt)),
// Conditional branch
PSEUDO_INST_DEF(Brcond,             ArgVoid, PSEUDO_INST_ARGVEC(ArgInt, ArgInt, ArgInt, ArgInt, ArgLabel, ArgLabel)),
// Conditional move
PSEUDO_INST_DEF(Movcond,            ArgInt, PSEUDO_INST_ARGVEC(ArgInt, ArgInt, ArgInt, ArgInt, ArgInt)),
// Vector creation ops
PSEUDO_INST_DEF(VecSplat,           ArgVec, PSEUDO_INST_ARGVEC(ArgInt)),
PSEUDO_INST_DEF(VecConstant,        ArgVec, PSEUDO_INST_ARGVEC(ArgVec)),
// Vector unary ops
PSEUDO_INST_DEF(VecNot,             ArgVec, PSEUDO_INST_ARGVEC(ArgVec)),
// Vector scalar binary ops
PSEUDO_INST_DEF(VecAddScalar,       ArgVec, PSEUDO_INST_ARGVEC(ArgVec, ArgInt)),
PSEUDO_INST_DEF(VecSubScalar,       ArgVec, PSEUDO_INST_ARGVEC(ArgVec, ArgInt)),
PSEUDO_INST_DEF(VecMulScalar,       ArgVec, PSEUDO_INST_ARGVEC(ArgVec, ArgInt)),
PSEUDO_INST_DEF(VecXorScalar,       ArgVec, PSEUDO_INST_ARGVEC(ArgVec, ArgInt)),
PSEUDO_INST_DEF(VecOrScalar,        ArgVec, PSEUDO_INST_ARGVEC(ArgVec, ArgInt)),
PSEUDO_INST_DEF(VecAndScalar,       ArgVec, PSEUDO_INST_ARGVEC(ArgVec, ArgInt)),
PSEUDO_INST_DEF(VecShlScalar,       ArgVec, PSEUDO_INST_ARGVEC(ArgVec, ArgInt)),
PSEUDO_INST_DEF(VecLShrScalar,      ArgVec, PSEUDO_INST_ARGVEC(ArgVec, ArgInt)),
PSEUDO_INST_DEF(VecAShrScalar,      ArgVec, PSEUDO_INST_ARGVEC(ArgVec, ArgInt)),
// Vector unary ops that stores to pointer
PSEUDO_INST_DEF(VecNotStore,        ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec)),
// Vector binary ops that stores to pointer
PSEUDO_INST_DEF(VecAddStore,        ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgVec)),
PSEUDO_INST_DEF(VecSubStore,        ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgVec)),
PSEUDO_INST_DEF(VecMulStore,        ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgVec)),
PSEUDO_INST_DEF(VecXorStore,        ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgVec)),
PSEUDO_INST_DEF(VecOrStore,         ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgVec)),
PSEUDO_INST_DEF(VecAndStore,        ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgVec)),
PSEUDO_INST_DEF(VecShlStore,        ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgVec)),
PSEUDO_INST_DEF(VecLShrStore,       ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgVec)),
PSEUDO_INST_DEF(VecAShrStore,       ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgVec)),
PSEUDO_INST_DEF(VecAddScalarStore,  ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgInt)),
PSEUDO_INST_DEF(VecSubScalarStore,  ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgInt)),
PSEUDO_INST_DEF(VecMulScalarStore,  ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgInt)),
PSEUDO_INST_DEF(VecXorScalarStore,  ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgInt)),
PSEUDO_INST_DEF(VecOrScalarStore,   ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgInt)),
PSEUDO_INST_DEF(VecAndScalarStore,  ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgInt)),
PSEUDO_INST_DEF(VecShlScalarStore,  ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgInt)),
PSEUDO_INST_DEF(VecLShrScalarStore, ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgInt)),
PSEUDO_INST_DEF(VecAShrScalarStore, ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgInt)),
// Host memory operations
//                                                      vaddr,  value   sign    size    endian
PSEUDO_INST_DEF(GuestLoad,  ArgInt,  PSEUDO_INST_ARGVEC(ArgInt,         ArgInt, ArgInt, ArgInt)),
PSEUDO_INST_DEF(GuestStore, ArgVoid, PSEUDO_INST_ARGVEC(ArgInt, ArgInt,         ArgInt, ArgInt)),
// ...
PSEUDO_INST_DEF(VecTruncStore,        ArgVoid, PSEUDO_INST_ARGVEC(ArgInt, ArgPtr, ArgVec)),
PSEUDO_INST_DEF(VecZExtStore,         ArgVoid, PSEUDO_INST_ARGVEC(ArgInt, ArgPtr, ArgVec)),
PSEUDO_INST_DEF(VecSExtStore,         ArgVoid, PSEUDO_INST_ARGVEC(ArgInt, ArgPtr, ArgVec)),
PSEUDO_INST_DEF(VecSignedSatAddStore, ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgVec)),
PSEUDO_INST_DEF(VecSignedSatSubStore, ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgVec)),
PSEUDO_INST_DEF(VecSelectStore,       ArgVoid, PSEUDO_INST_ARGVEC(ArgInt, ArgPtr, ArgVec, ArgVec, ArgVec)),
PSEUDO_INST_DEF(VecFunnelShrStore,    ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgVec, ArgVec)),
PSEUDO_INST_DEF(VecAbsStore,          ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec)),
PSEUDO_INST_DEF(VecSignedMaxStore,    ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgVec)),
PSEUDO_INST_DEF(VecUnsignedMaxStore,  ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgVec)),
PSEUDO_INST_DEF(VecSignedMinStore,    ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgVec)),
PSEUDO_INST_DEF(VecUnsignedMinStore,  ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgVec)),
PSEUDO_INST_DEF(VecCtlzStore,         ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec)),
PSEUDO_INST_DEF(VecCttzStore,         ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec)),
PSEUDO_INST_DEF(VecCtpopStore,        ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec)),
PSEUDO_INST_DEF(VecWideCondBitsel,    ArgVec, PSEUDO_INST_ARGVEC(ArgVec, ArgVec, ArgVec)),
PSEUDO_INST_DEF(VecWideCondBitselStore,    ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgVec, ArgVec, ArgVec)),
PSEUDO_INST_DEF(VecCompare,           ArgVec, PSEUDO_INST_ARGVEC(ArgInt, ArgVec, ArgVec)),
PSEUDO_INST_DEF(VecSelect,            ArgVec, PSEUDO_INST_ARGVEC(ArgInt, ArgVec, ArgVec)),

PSEUDO_INST_DEF(SignExtract,          ArgInt, PSEUDO_INST_ARGVEC(ArgInt, ArgInt, ArgInt)),
PSEUDO_INST_DEF(Extract,              ArgInt, PSEUDO_INST_ARGVEC(ArgInt, ArgInt, ArgInt)),

PSEUDO_INST_DEF(Exception,            ArgVoid, PSEUDO_INST_ARGVEC(ArgPtr, ArgInt)),
PSEUDO_INST_DEF(GetPC,                ArgInt,  PSEUDO_INST_ARGVEC()),
