/*
 * RAW s390x CPU feature definitions:
 *
 * DEF_FEAT(_FEAT, _NAME, _TYPE, _BIT, _DESC):
 * - _FEAT: Feature (enum) name used internally (S390_FEAT_##_FEAT)
 * - _NAME: Feature name exposed to the user.
 * - _TYPE: Feature type (S390_FEAT_TYPE_##_TYPE).
 * - _BIT: Feature bit number within feature type block (unused for MISC).
 * - _DESC: Feature description, exposed to the user.
 *
 * Copyright IBM Corp. 2016, 2018
 * Copyright Red Hat, Inc. 2019
 *
 * Author(s): Michael Mueller <mimu@linux.vnet.ibm.com>
 *            David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

/* Features exposed via the STFL(E) instruction. */
DEF_FEAT(ESAN3, "esan3", STFL, 0, "Instructions marked as n3")
DEF_FEAT(ZARCH, "zarch", STFL, 1, "z/Architecture architectural mode")
DEF_FEAT(DAT_ENH, "dateh", STFL, 3, "DAT-enhancement facility")
DEF_FEAT(IDTE_SEGMENT, "idtes", STFL, 4, "IDTE selective TLB segment-table clearing")
DEF_FEAT(IDTE_REGION, "idter", STFL, 5, "IDTE selective TLB region-table clearing")
DEF_FEAT(ASN_LX_REUSE, "asnlxr", STFL, 6, "ASN-and-LX reuse facility")
DEF_FEAT(STFLE, "stfle", STFL, 7, "Store-facility-list-extended facility")
DEF_FEAT(EDAT, "edat", STFL, 8, "Enhanced-DAT facility")
DEF_FEAT(SENSE_RUNNING_STATUS, "srs", STFL, 9, "Sense-running-status facility")
DEF_FEAT(CONDITIONAL_SSKE, "csske", STFL, 10, "Conditional-SSKE facility")
DEF_FEAT(CONFIGURATION_TOPOLOGY, "ctop", STFL, 11, "Configuration-topology facility")
DEF_FEAT(AP_QUERY_CONFIG_INFO, "apqci", STFL, 12, "Query AP Configuration Information facility")
DEF_FEAT(IPTE_RANGE, "ipter", STFL, 13, "IPTE-range facility")
DEF_FEAT(NONQ_KEY_SETTING, "nonqks", STFL, 14, "Nonquiescing key-setting facility")
DEF_FEAT(AP_FACILITIES_TEST, "apft", STFL, 15, "AP Facilities Test facility")
DEF_FEAT(EXTENDED_TRANSLATION_2, "etf2", STFL, 16, "Extended-translation facility 2")
DEF_FEAT(MSA, "msa-base", STFL, 17, "Message-security-assist facility (excluding subfunctions)")
DEF_FEAT(LONG_DISPLACEMENT, "ldisp", STFL, 18, "Long-displacement facility")
DEF_FEAT(LONG_DISPLACEMENT_FAST, "ldisphp", STFL, 19, "Long-displacement facility has high performance")
DEF_FEAT(HFP_MADDSUB, "hfpm", STFL, 20, "HFP-multiply-add/subtract facility")
DEF_FEAT(EXTENDED_IMMEDIATE, "eimm", STFL, 21, "Extended-immediate facility")
DEF_FEAT(EXTENDED_TRANSLATION_3, "etf3", STFL, 22, "Extended-translation facility 3")
DEF_FEAT(HFP_UNNORMALIZED_EXT, "hfpue", STFL, 23, "HFP-unnormalized-extension facility")
DEF_FEAT(ETF2_ENH, "etf2eh", STFL, 24, "ETF2-enhancement facility")
DEF_FEAT(STORE_CLOCK_FAST, "stckf", STFL, 25, "Store-clock-fast facility")
DEF_FEAT(PARSING_ENH, "parseh", STFL, 26, "Parsing-enhancement facility")
DEF_FEAT(MOVE_WITH_OPTIONAL_SPEC, "mvcos", STFL, 27, "Move-with-optional-specification facility")
DEF_FEAT(TOD_CLOCK_STEERING, "tods-base", STFL, 28, "TOD-clock-steering facility (excluding subfunctions)")
DEF_FEAT(ETF3_ENH, "etf3eh", STFL, 30, "ETF3-enhancement facility")
DEF_FEAT(EXTRACT_CPU_TIME, "ectg", STFL, 31, "Extract-CPU-time facility")
DEF_FEAT(COMPARE_AND_SWAP_AND_STORE, "csst", STFL, 32, "Compare-and-swap-and-store facility")
DEF_FEAT(COMPARE_AND_SWAP_AND_STORE_2, "csst2", STFL, 33, "Compare-and-swap-and-store facility 2")
DEF_FEAT(GENERAL_INSTRUCTIONS_EXT, "ginste", STFL, 34, "General-instructions-extension facility")
DEF_FEAT(EXECUTE_EXT, "exrl", STFL, 35, "Execute-extensions facility")
DEF_FEAT(ENHANCED_MONITOR, "emon", STFL, 36, "Enhanced-monitor facility")
DEF_FEAT(FLOATING_POINT_EXT, "fpe", STFL, 37, "Floating-point extension facility")
DEF_FEAT(ORDER_PRESERVING_COMPRESSION, "opc", STFL, 38, "Order Preserving Compression facility")
DEF_FEAT(SET_PROGRAM_PARAMETERS, "sprogp", STFL, 40, "Set-program-parameters facility")
DEF_FEAT(FLOATING_POINT_SUPPORT_ENH, "fpseh", STFL, 41, "Floating-point-support-enhancement facilities")
DEF_FEAT(DFP, "dfp", STFL, 42, "DFP (decimal-floating-point) facility")
DEF_FEAT(DFP_FAST, "dfphp", STFL, 43, "DFP (decimal-floating-point) facility has high performance")
DEF_FEAT(PFPO, "pfpo", STFL, 44, "PFPO instruction")
DEF_FEAT(STFLE_45, "stfle45", STFL, 45, "Various facilities introduced with z196")
DEF_FEAT(CMPSC_ENH, "cmpsceh", STFL, 47, "CMPSC-enhancement facility")
DEF_FEAT(DFP_ZONED_CONVERSION, "dfpzc", STFL, 48, "Decimal-floating-point zoned-conversion facility")
DEF_FEAT(STFLE_49, "stfle49", STFL, 49, "Various facilities introduced with zEC12")
DEF_FEAT(CONSTRAINT_TRANSACTIONAL_EXE, "cte", STFL, 50, "Constrained transactional-execution facility")
DEF_FEAT(LOCAL_TLB_CLEARING, "ltlbc", STFL, 51, "Local-TLB-clearing facility")
DEF_FEAT(INTERLOCKED_ACCESS_2, "iacc2", STFL, 52, "Interlocked-access facility 2")
DEF_FEAT(STFLE_53, "stfle53", STFL, 53, "Various facilities introduced with z13")
DEF_FEAT(ENTROPY_ENC_COMP, "eec", STFL, 54, "Entropy encoding compression facility")
DEF_FEAT(MSA_EXT_5, "msa5-base", STFL, 57, "Message-security-assist-extension-5 facility (excluding subfunctions)")
DEF_FEAT(MISC_INSTRUCTION_EXT2, "minste2", STFL, 58, "Miscellaneous-instruction-extensions facility 2")
DEF_FEAT(SEMAPHORE_ASSIST, "sema", STFL, 59, "Semaphore-assist facility")
DEF_FEAT(TIME_SLICE_INSTRUMENTATION, "tsi", STFL, 60, "Time-slice Instrumentation facility")
DEF_FEAT(MISC_INSTRUCTION_EXT3, "minste3", STFL, 61, "Miscellaneous-Instruction-Extensions Facility 3")
DEF_FEAT(RUNTIME_INSTRUMENTATION, "ri", STFL, 64, "CPU runtime-instrumentation facility")
DEF_FEAT(AP_QUEUE_INTERRUPT_CONTROL, "apqi", STFL, 65, "AP-Queue interruption facility")
DEF_FEAT(ZPCI, "zpci", STFL, 69, "z/PCI facility")
DEF_FEAT(ADAPTER_EVENT_NOTIFICATION, "aen", STFL, 71, "General-purpose-adapter-event-notification facility")
DEF_FEAT(ADAPTER_INT_SUPPRESSION, "ais", STFL, 72, "General-purpose-adapter-interruption-suppression facility")
DEF_FEAT(TRANSACTIONAL_EXE, "te", STFL, 73, "Transactional-execution facility")
DEF_FEAT(STORE_HYPERVISOR_INFO, "sthyi", STFL, 74, "Store-hypervisor-information facility")
DEF_FEAT(ACCESS_EXCEPTION_FS_INDICATION, "aefsi", STFL, 75, "Access-exception-fetch/store-indication facility")
DEF_FEAT(MSA_EXT_3, "msa3-base", STFL, 76, "Message-security-assist-extension-3 facility (excluding subfunctions)")
DEF_FEAT(MSA_EXT_4, "msa4-base", STFL, 77, "Message-security-assist-extension-4 facility (excluding subfunctions)")
DEF_FEAT(EDAT_2, "edat2", STFL, 78, "Enhanced-DAT facility 2")
DEF_FEAT(DFP_PACKED_CONVERSION, "dfppc", STFL, 80, "Decimal-floating-point packed-conversion facility")
DEF_FEAT(PPA15, "ppa15", STFL, 81, "PPA15 is installed")
DEF_FEAT(BPB, "bpb", STFL, 82, "Branch prediction blocking")
DEF_FEAT(MISC_INSTRUCTION_EXT4, "minste4", STFL, 84, "Miscellaneous-Instruction-Extensions Facility 4")
DEF_FEAT(SIF, "sif", STFL, 85, "Sequential-instruction-fetching facility")
DEF_FEAT(MSA_EXT_12, "msa12-base", STFL, 86, "Message-security-assist-extension-12 facility (excluding subfunctions)")
DEF_FEAT(PLO_EXT, "plo-ext", STFL, 87, "PLO-extension facility")
DEF_FEAT(VECTOR, "vx", STFL, 129, "Vector facility")
DEF_FEAT(INSTRUCTION_EXEC_PROT, "iep", STFL, 130, "Instruction-execution-protection facility")
DEF_FEAT(SIDE_EFFECT_ACCESS_ESOP2, "sea_esop2", STFL, 131, "Side-effect-access facility and Enhanced-suppression-on-protection facility 2")
DEF_FEAT(GUARDED_STORAGE, "gs", STFL, 133, "Guarded-storage facility")
DEF_FEAT(VECTOR_PACKED_DECIMAL, "vxpd", STFL, 134, "Vector packed decimal facility")
DEF_FEAT(VECTOR_ENH, "vxeh", STFL, 135, "Vector enhancements facility")
DEF_FEAT(MULTIPLE_EPOCH, "mepoch", STFL, 139, "Multiple-epoch facility")
DEF_FEAT(EXTENDED_LENGTH_SCCB, "els", STFL, 140, "Extended-length SCCB facility")
DEF_FEAT(TEST_PENDING_EXT_INTERRUPTION, "tpei", STFL, 144, "Test-pending-external-interruption facility")
DEF_FEAT(INSERT_REFERENCE_BITS_MULT, "irbm", STFL, 145, "Insert-reference-bits-multiple facility")
DEF_FEAT(MSA_EXT_8, "msa8-base", STFL, 146, "Message-security-assist-extension-8 facility (excluding subfunctions)")
DEF_FEAT(CMM_NT, "cmmnt", STFL, 147, "CMM: ESSA-enhancement (no translate) facility")
DEF_FEAT(VECTOR_ENH2, "vxeh2", STFL, 148, "Vector Enhancements facility 2")
DEF_FEAT(ESORT_BASE, "esort-base", STFL, 150, "Enhanced-sort facility (excluding subfunctions)")
DEF_FEAT(DEFLATE_BASE, "deflate-base", STFL, 151, "Deflate-conversion facility (excluding subfunctions)")
DEF_FEAT(VECTOR_PACKED_DECIMAL_ENH, "vxpdeh", STFL, 152, "Vector-Packed-Decimal-Enhancement Facility")
DEF_FEAT(MSA_EXT_9, "msa9-base", STFL, 155, "Message-security-assist-extension-9 facility (excluding subfunctions)")
DEF_FEAT(ETOKEN, "etoken", STFL, 156, "Etoken facility")
DEF_FEAT(UNPACK, "unpack", STFL, 161, "Unpack facility")
DEF_FEAT(NNPA, "nnpa", STFL, 165, "NNPA facility")
DEF_FEAT(INEFF_NC_TX, "ineff_nc_tx", STFL, 170, "Ineffective-nonconstrained-transaction facility")
DEF_FEAT(VECTOR_PACKED_DECIMAL_ENH2, "vxpdeh2", STFL, 192, "Vector-Packed-Decimal-Enhancement facility 2")
DEF_FEAT(BEAR_ENH, "beareh", STFL, 193, "BEAR-enhancement facility")
DEF_FEAT(RDP, "rdp", STFL, 194, "Reset-DAT-protection facility")
DEF_FEAT(PAI, "pai", STFL, 196, "Processor-Activity-Instrumentation facility")
DEF_FEAT(PAIE, "paie", STFL, 197, "Processor-Activity-Instrumentation extension-1")
DEF_FEAT(VECTOR_ENH3, "vxeh3", STFL, 198, "Vector Enhancements facility 3")
DEF_FEAT(VECTOR_PACKED_DECIMAL_ENH3, "vxpdeh3", STFL, 199, "Vector-Packed-Decimal-Enhancement facility 3")
DEF_FEAT(CCF_BASE, "ccf-base", STFL, 201, "Concurrent-Functions facility")

/* Features exposed via SCLP SCCB Byte 80 - 98  (bit numbers relative to byte-80) */
DEF_FEAT(SIE_GSLS, "gsls", SCLP_CONF_CHAR, 40, "SIE: Guest-storage-limit-suppression facility")
DEF_FEAT(ESOP, "esop", SCLP_CONF_CHAR, 46, "Enhanced-suppression-on-protection facility")
DEF_FEAT(HPMA2, "hpma2", SCLP_CONF_CHAR, 90, "Host page management assist 2 Facility") /* 91-2 */
DEF_FEAT(SIE_KSS, "kss", SCLP_CONF_CHAR, 151, "SIE: Keyless-subset facility")  /* 98-7 */

/* Features exposed via SCLP SCCB Byte 116 - 119 (bit numbers relative to byte-116) */
DEF_FEAT(SIE_64BSCAO, "64bscao", SCLP_CONF_CHAR_EXT, 0, "SIE: 64-bit-SCAO facility")
DEF_FEAT(SIE_CMMA, "cmma", SCLP_CONF_CHAR_EXT, 1, "SIE: Collaborative-memory-management assist")
DEF_FEAT(SIE_PFMFI, "pfmfi", SCLP_CONF_CHAR_EXT, 9, "SIE: PFMF interpretation facility")
DEF_FEAT(SIE_IBS, "ibs", SCLP_CONF_CHAR_EXT, 10, "SIE: Interlock-and-broadcast-suppression facility")

/* Features exposed via SCLP SCCB Facilities byte 134 (bit numbers relative to byte-134) */
DEF_FEAT(DIAG_318, "diag318", SCLP_FAC134, 0, "Control program name and version codes")

/* Features exposed via SCLP CPU info. */
DEF_FEAT(SIE_F2, "sief2", SCLP_CPU, 4, "SIE: interception format 2 (Virtual SIE)")
DEF_FEAT(SIE_SKEY, "skey", SCLP_CPU, 5, "SIE: Storage-key facility")
DEF_FEAT(SIE_GPERE, "gpereh", SCLP_CPU, 10, "SIE: Guest-PER enhancement facility")
DEF_FEAT(SIE_SIIF, "siif", SCLP_CPU, 11, "SIE: Shared IPTE-interlock facility")
DEF_FEAT(SIE_SIGPIF, "sigpif", SCLP_CPU, 12, "SIE: SIGP interpretation facility")
DEF_FEAT(SIE_IB, "ib", SCLP_CPU, 42, "SIE: Intervention bypass facility")
DEF_FEAT(SIE_CEI, "cei", SCLP_CPU, 43, "SIE: Conditional-external-interception facility")

/*
 * Features exposed via no feature bit (but e.g., instruction sensing)
 * -> the feature bit number is irrelevant
 */
DEF_FEAT(DAT_ENH_2, "dateh2", MISC, 0, "DAT-enhancement facility 2")
DEF_FEAT(CMM, "cmm", MISC, 0, "Collaborative-memory-management facility")
DEF_FEAT(AP, "ap", MISC, 0, "AP instructions installed")

/* Features exposed via the PLO instruction. */
DEF_FEAT(PLO_CL, "plo-cl", PLO, 0, "PLO Compare and load (32 bit in general registers)")
DEF_FEAT(PLO_CLG, "plo-clg", PLO, 1, "PLO Compare and load (64 bit in parameter list)")
DEF_FEAT(PLO_CLGR, "plo-clgr", PLO, 2, "PLO Compare and load (64 bit in general registers)")
DEF_FEAT(PLO_CLX, "plo-clx", PLO, 3, "PLO Compare and load (128 bit in parameter list)")
DEF_FEAT(PLO_CS, "plo-cs", PLO, 4, "PLO Compare and swap (32 bit in general registers)")
DEF_FEAT(PLO_CSG, "plo-csg", PLO, 5, "PLO Compare and swap (64 bit in parameter list)")
DEF_FEAT(PLO_CSGR, "plo-csgr", PLO, 6, "PLO Compare and swap (64 bit in general registers)")
DEF_FEAT(PLO_CSX, "plo-csx", PLO, 7, "PLO Compare and swap (128 bit in parameter list)")
DEF_FEAT(PLO_DCS, "plo-dcs", PLO, 8, "PLO Double compare and swap (32 bit in general registers)")
DEF_FEAT(PLO_DCSG, "plo-dcsg", PLO, 9, "PLO Double compare and swap (64 bit in parameter list)")
DEF_FEAT(PLO_DCSGR, "plo-dcsgr", PLO, 10, "PLO Double compare and swap (64 bit in general registers)")
DEF_FEAT(PLO_DCSX, "plo-dcsx", PLO, 11, "PLO Double compare and swap (128 bit in parameter list)")
DEF_FEAT(PLO_CSST, "plo-csst", PLO, 12, "PLO Compare and swap and store (32 bit in general registers)")
DEF_FEAT(PLO_CSSTG, "plo-csstg", PLO, 13, "PLO Compare and swap and store (64 bit in parameter list)")
DEF_FEAT(PLO_CSSTGR, "plo-csstgr", PLO, 14, "PLO Compare and swap and store (64 bit in general registers)")
DEF_FEAT(PLO_CSSTX, "plo-csstx", PLO, 15, "PLO Compare and swap and store (128 bit in parameter list)")
DEF_FEAT(PLO_CSDST, "plo-csdst", PLO, 16, "PLO Compare and swap and double store (32 bit in general registers)")
DEF_FEAT(PLO_CSDSTG, "plo-csdstg", PLO, 17, "PLO Compare and swap and double store (64 bit in parameter list)")
DEF_FEAT(PLO_CSDSTGR, "plo-csdstgr", PLO, 18, "PLO Compare and swap and double store (64 bit in general registers)")
DEF_FEAT(PLO_CSDSTX, "plo-csdstx", PLO, 19, "PLO Compare and swap and double store (128 bit in parameter list)")
DEF_FEAT(PLO_CSTST, "plo-cstst", PLO, 20, "PLO Compare and swap and triple store (32 bit in general registers)")
DEF_FEAT(PLO_CSTSTG, "plo-cststg", PLO, 21, "PLO Compare and swap and triple store (64 bit in parameter list)")
DEF_FEAT(PLO_CSTSTGR, "plo-cststgr", PLO, 22, "PLO Compare and swap and triple store (64 bit in general registers)")
DEF_FEAT(PLO_CSTSTX, "plo-cststx", PLO, 23, "PLO Compare and swap and triple store (128 bit in parameter list)")
DEF_FEAT(PLO_CLO, "plo-clo", PLO, 24, "PLO Compare and load (256 bit in parameter list)")
DEF_FEAT(PLO_CSO, "plo-cso", PLO, 25, "PLO Compare and swap (256 bit in parameter list)")
DEF_FEAT(PLO_DCSO, "plo-dcso", PLO, 26, "PLO Double compare and swap (256 bit in parameter list)")
DEF_FEAT(PLO_CSSTO, "plo-cssto", PLO, 27, "PLO Compare and swap and store (256 bit in parameter list)")
DEF_FEAT(PLO_CSDSTO, "plo-csdsto", PLO, 28, "PLO Compare and swap and double store (256 bit in parameter list)")
DEF_FEAT(PLO_CSTSTO, "plo-cststo", PLO, 29, "PLO Compare and swap and triple store (256 bit in parameter list)")
DEF_FEAT(PLO_TCS, "plo-tcs", PLO, 30, "Triple compare and swap (32 bit in parameter list)")
DEF_FEAT(PLO_TCSG, "plo-tcsg", PLO, 31, "Triple compare and swap (64 bit in parameter list)")
DEF_FEAT(PLO_TCSX, "plo-tcsx", PLO, 32, "Triple compare and swap (128 bit in parameter list)")
DEF_FEAT(PLO_TCSO, "plo-tcso", PLO, 33, "Triple compare and swap (256 bit in parameter list)")
DEF_FEAT(PLO_QCS, "plo-qcs", PLO, 34, "Quadruple compare and swap (32 bit in parameter list)")
DEF_FEAT(PLO_QCSG, "plo-qcsg", PLO, 35, "Quadruple compare and swap (64 bit in parameter list)")
DEF_FEAT(PLO_QCSX, "plo-qcsx", PLO, 36, "Quadruple compare and swap (128 bit in parameter list)")
DEF_FEAT(PLO_QCSO, "plo-qcso", PLO, 37, "Quadruple compare and swap (256 bit in parameter list)")
DEF_FEAT(PLO_LO, "plo-lo", PLO, 38, "Load (256 bit in parameter list)")
DEF_FEAT(PLO_DLX, "plo-dlx", PLO, 39, "Double load (128 bit in parameter list)")
DEF_FEAT(PLO_DLO, "plo-dlo", PLO, 40, "Double load (256 bit in parameter list)")
DEF_FEAT(PLO_TL, "plo-tl", PLO, 41, "Triple load (32 bit in parameter list)")
DEF_FEAT(PLO_TLG, "plo-tlg", PLO, 42, "Triple load (64 bit in parameter list)")
DEF_FEAT(PLO_TLX, "plo-tlx", PLO, 43, "Triple load (128 bit in parameter list)")
DEF_FEAT(PLO_TLO, "plo-tlo", PLO, 44, "Triple load (256 bit in parameter list)")
DEF_FEAT(PLO_QL, "plo-ql", PLO, 45, "Quadruple load (32 bit in parameter list)")
DEF_FEAT(PLO_QLG, "plo-qlg", PLO, 46, "Quadruple load (64 bit in parameter list)")
DEF_FEAT(PLO_QLX, "plo-qlx", PLO, 47, "Quadruple load (128 bit in parameter list)")
DEF_FEAT(PLO_QLO, "plo-qlo", PLO, 48, "Quadruple load (256 bit in parameter list)")
DEF_FEAT(PLO_STO, "plo-sto", PLO, 49, "Store (256 bit in parameter list)")
DEF_FEAT(PLO_DST, "plo-dst", PLO, 50, "Double store (32 bit in parameter list)")
DEF_FEAT(PLO_DSTG, "plo-dstg", PLO, 51, "Double store (64 bit in parameter list)")
DEF_FEAT(PLO_DSTX, "plo-dstx", PLO, 52, "Double store (128 bit in parameter list)")
DEF_FEAT(PLO_DSTO, "plo-dsto", PLO, 53, "Double store (256 bit in parameter list)")
DEF_FEAT(PLO_TST, "plo-tst", PLO, 54, "Triple store (32 bit in parameter list)")
DEF_FEAT(PLO_TSTG, "plo-tstg", PLO, 55, "Triple store (64 bit in parameter list)")
DEF_FEAT(PLO_TSTX, "plo-tstx", PLO, 56, "Triple store (128 bit in parameter list)")
DEF_FEAT(PLO_TSTO, "plo-tsto", PLO, 57, "Triple store (256 bit in parameter list)")
DEF_FEAT(PLO_QST, "plo-qst", PLO, 58, "Quadruple store (32 bit in parameter list)")
DEF_FEAT(PLO_QSTG, "plo-qstg", PLO, 59, "Quadruple store (64 bit in parameter list)")
DEF_FEAT(PLO_QSTX, "plo-qstx", PLO, 60, "Quadruple store (128 bit in parameter list)")
DEF_FEAT(PLO_QSTO, "plo-qsto", PLO, 61, "Quadruple store (256 bit in parameter list)")

/* Features exposed via the PTFF instruction. */
DEF_FEAT(PTFF_QTO, "ptff-qto", PTFF, 1, "PTFF Query TOD Offset")
DEF_FEAT(PTFF_QSI, "ptff-qsi", PTFF, 2, "PTFF Query Steering Information")
DEF_FEAT(PTFF_QPT, "ptff-qpc", PTFF, 3, "PTFF Query Physical Clock")
DEF_FEAT(PTFF_QUI, "ptff-qui", PTFF, 4, "PTFF Query UTC Information")
DEF_FEAT(PTFF_QTOU, "ptff-qtou", PTFF, 5, "PTFF Query TOD Offset User")
DEF_FEAT(PTFF_QTSE, "ptff-qtse", PTFF, 6, "PTFF Query Time-Stamp Event")
DEF_FEAT(PTFF_QSIE, "ptff-qsie", PTFF, 10, "PTFF Query Steering Information Extended")
DEF_FEAT(PTFF_QTOUE, "ptff-qtoue", PTFF, 13, "PTFF Query TOD Offset User Extended")
DEF_FEAT(PTFF_STO, "ptff-sto", PTFF, 65, "PTFF Set TOD Offset")
DEF_FEAT(PTFF_STOU, "ptff-stou", PTFF, 69, "PTFF Set TOD Offset User")
DEF_FEAT(PTFF_STOE, "ptff-stoe", PTFF, 73, "PTFF Set TOD Offset Extended")
DEF_FEAT(PTFF_STOUE, "ptff-stoue", PTFF, 77, "PTFF Set TOD Offset User Extended")

/* Features exposed via the KMAC instruction. */
DEF_FEAT(KMAC_DEA, "kmac-dea", KMAC, 1, "KMAC DEA")
DEF_FEAT(KMAC_TDEA_128, "kmac-tdea-128", KMAC, 2, "KMAC TDEA-128")
DEF_FEAT(KMAC_TDEA_192, "kmac-tdea-192", KMAC, 3, "KMAC TDEA-192")
DEF_FEAT(KMAC_EDEA, "kmac-edea", KMAC, 9, "KMAC Encrypted-DEA")
DEF_FEAT(KMAC_ETDEA_128, "kmac-etdea-128", KMAC, 10, "KMAC Encrypted-TDEA-128")
DEF_FEAT(KMAC_ETDEA_192, "kmac-etdea-192", KMAC, 11, "KMAC Encrypted-TDEA-192")
DEF_FEAT(KMAC_AES_128, "kmac-aes-128", KMAC, 18, "KMAC AES-128")
DEF_FEAT(KMAC_AES_192, "kmac-aes-192", KMAC, 19, "KMAC AES-192")
DEF_FEAT(KMAC_AES_256, "kmac-aes-256", KMAC, 20, "KMAC AES-256")
DEF_FEAT(KMAC_EAES_128, "kmac-eaes-128", KMAC, 26, "KMAC Encrypted-AES-128")
DEF_FEAT(KMAC_EAES_192, "kmac-eaes-192", KMAC, 27, "KMAC Encrypted-AES-192")
DEF_FEAT(KMAC_EAES_256, "kmac-eaes-256", KMAC, 28, "KMAC Encrypted-AES-256")
DEF_FEAT(KMAC_HMAC_SHA_224, "kmac-hmac-sha-224", KMAC, 112, "KMAC HMAC-SHA-224")
DEF_FEAT(KMAC_HMAC_SHA_256, "kmac-hmac-sha-246", KMAC, 113, "KMAC HMAC-SHA-256")
DEF_FEAT(KMAC_HMAC_SHA_384, "kmac-hmac-sha-384", KMAC, 114, "KMAC HMAC-SHA-384")
DEF_FEAT(KMAC_HMAC_SHA_512, "kmac-hmac-sha-512", KMAC, 115, "KMAC HMAC-SHA-512")
DEF_FEAT(KMAC_HMAC_ESHA_224, "kmac-hmac-esha-224", KMAC, 120, "KMAC HMAC-Encrypted-SHA-224")
DEF_FEAT(KMAC_HMAC_ESHA_256, "kmac-hmac-esha-246", KMAC, 121, "KMAC HMAC-Encrypted-SHA-256")
DEF_FEAT(KMAC_HMAC_ESHA_384, "kmac-hmac-esha-384", KMAC, 122, "KMAC HMAC-Encrypted-SHA-384")
DEF_FEAT(KMAC_HMAC_ESHA_512, "kmac-hmac-esha-512", KMAC, 123, "KMAC HMAC-Encrypted-SHA-512")
DEF_FEAT(KMAC_QAI, "kmac-qai", KMAC, 127, "KMAC Query-Authentication-Information")

/* Features exposed via the KMC instruction. */
DEF_FEAT(KMC_DEA, "kmc-dea", KMC, 1, "KMC DEA")
DEF_FEAT(KMC_TDEA_128, "kmc-tdea-128", KMC, 2, "KMC TDEA-128")
DEF_FEAT(KMC_TDEA_192, "kmc-tdea-192", KMC, 3, "KMC TDEA-192")
DEF_FEAT(KMC_EDEA, "kmc-edea", KMC, 9, "KMC Encrypted-DEA")
DEF_FEAT(KMC_ETDEA_128, "kmc-etdea-128", KMC, 10, "KMC Encrypted-TDEA-128")
DEF_FEAT(KMC_ETDEA_192, "kmc-etdea-192", KMC, 11, "KMC Encrypted-TDEA-192")
DEF_FEAT(KMC_AES_128, "kmc-aes-128", KMC, 18, "KMC AES-128")
DEF_FEAT(KMC_AES_192, "kmc-aes-192", KMC, 19, "KMC AES-192")
DEF_FEAT(KMC_AES_256, "kmc-aes-256", KMC, 20, "KMC AES-256")
DEF_FEAT(KMC_EAES_128, "kmc-eaes-128", KMC, 26, "KMC Encrypted-AES-128")
DEF_FEAT(KMC_EAES_192, "kmc-eaes-192", KMC, 27, "KMC Encrypted-AES-192")
DEF_FEAT(KMC_EAES_256, "kmc-eaes-256", KMC, 28, "KMC Encrypted-AES-256")
DEF_FEAT(KMC_PRNG, "kmc-prng", KMC, 67, "KMC PRNG")

/* Features exposed via the KM instruction. */
DEF_FEAT(KM_DEA, "km-dea", KM, 1, "KM DEA")
DEF_FEAT(KM_TDEA_128, "km-tdea-128", KM, 2, "KM TDEA-128")
DEF_FEAT(KM_TDEA_192, "km-tdea-192", KM, 3, "KM TDEA-192")
DEF_FEAT(KM_EDEA, "km-edea", KM, 9, "KM Encrypted-DEA")
DEF_FEAT(KM_ETDEA_128, "km-etdea-128", KM, 10, "KM Encrypted-TDEA-128")
DEF_FEAT(KM_ETDEA_192, "km-etdea-192", KM, 11, "KM Encrypted-TDEA-192")
DEF_FEAT(KM_AES_128, "km-aes-128", KM, 18, "KM AES-128")
DEF_FEAT(KM_AES_192, "km-aes-192", KM, 19, "KM AES-192")
DEF_FEAT(KM_AES_256, "km-aes-256", KM, 20, "KM AES-256")
DEF_FEAT(KM_EAES_128, "km-eaes-128", KM, 26, "KM Encrypted-AES-128")
DEF_FEAT(KM_EAES_192, "km-eaes-192", KM, 27, "KM Encrypted-AES-192")
DEF_FEAT(KM_EAES_256, "km-eaes-256", KM, 28, "KM Encrypted-AES-256")
DEF_FEAT(KM_XTS_AES_128, "km-xts-aes-128", KM, 50, "KM XTS-AES-128")
DEF_FEAT(KM_XTS_AES_256, "km-xts-aes-256", KM, 52, "KM XTS-AES-256")
DEF_FEAT(KM_XTS_EAES_128, "km-xts-eaes-128", KM, 58, "KM XTS-Encrypted-AES-128")
DEF_FEAT(KM_XTS_EAES_256, "km-xts-eaes-256", KM, 60, "KM XTS-Encrypted-AES-256")
DEF_FEAT(KM_FULL_XTS_AES_128, "km-full-xts-aes-128", KM, 82, "KM Full-XTS-AES-128")
DEF_FEAT(KM_FULL_XTS_AES_256, "km-full-xts-aes-256", KM, 84, "KM Full-XTS-AES-256")
DEF_FEAT(KM_FULL_XTS_EAES_128, "km-full-xts-eaes-128", KM, 90, "KM Full-XTS-Encrypted-AES-128")
DEF_FEAT(KM_FULL_XTS_EAES_256, "km-full-xts-eaes-256", KM, 92, "KM Full-XTS-Encrypted-AES-256")
DEF_FEAT(KM_QAI, "km-qai", KM, 127, "KM Query-Authentication-Information")

/* Features exposed via the KIMD instruction. */
DEF_FEAT(KIMD_SHA_1, "kimd-sha-1", KIMD, 1, "KIMD SHA-1")
DEF_FEAT(KIMD_SHA_256, "kimd-sha-256", KIMD, 2, "KIMD SHA-256")
DEF_FEAT(KIMD_SHA_512, "kimd-sha-512", KIMD, 3, "KIMD SHA-512")
DEF_FEAT(KIMD_SHA3_224, "kimd-sha3-224", KIMD, 32, "KIMD SHA3-224")
DEF_FEAT(KIMD_SHA3_256, "kimd-sha3-256", KIMD, 33, "KIMD SHA3-256")
DEF_FEAT(KIMD_SHA3_384, "kimd-sha3-384", KIMD, 34, "KIMD SHA3-384")
DEF_FEAT(KIMD_SHA3_512, "kimd-sha3-512", KIMD, 35, "KIMD SHA3-512")
DEF_FEAT(KIMD_SHAKE_128, "kimd-shake-128", KIMD, 36, "KIMD SHAKE-128")
DEF_FEAT(KIMD_SHAKE_256, "kimd-shake-256", KIMD, 37, "KIMD SHAKE-256")
DEF_FEAT(KIMD_GHASH, "kimd-ghash", KIMD, 65, "KIMD GHASH")
DEF_FEAT(KIMD_QAI, "kimd-qai", KIMD, 127, "KIMD Query-Authentication-Information")

/* Features exposed via the KLMD instruction. */
DEF_FEAT(KLMD_SHA_1, "klmd-sha-1", KLMD, 1, "KLMD SHA-1")
DEF_FEAT(KLMD_SHA_256, "klmd-sha-256", KLMD, 2, "KLMD SHA-256")
DEF_FEAT(KLMD_SHA_512, "klmd-sha-512", KLMD, 3, "KLMD SHA-512")
DEF_FEAT(KLMD_SHA3_224, "klmd-sha3-224", KLMD, 32, "KLMD SHA3-224")
DEF_FEAT(KLMD_SHA3_256, "klmd-sha3-256", KLMD, 33, "KLMD SHA3-256")
DEF_FEAT(KLMD_SHA3_384, "klmd-sha3-384", KLMD, 34, "KLMD SHA3-384")
DEF_FEAT(KLMD_SHA3_512, "klmd-sha3-512", KLMD, 35, "KLMD SHA3-512")
DEF_FEAT(KLMD_SHAKE_128, "klmd-shake-128", KLMD, 36, "KLMD SHAKE-128")
DEF_FEAT(KLMD_SHAKE_256, "klmd-shake-256", KLMD, 37, "KLMD SHAKE-256")
DEF_FEAT(KLMD_QAI, "klmd-qai", KLMD, 127, "KLMD Query-Authentication-Information")

/* Features exposed via the PCKMO instruction. */
DEF_FEAT(PCKMO_EDEA, "pckmo-edea", PCKMO, 1, "PCKMO Encrypted-DEA-Key")
DEF_FEAT(PCKMO_ETDEA_128, "pckmo-etdea-128", PCKMO, 2, "PCKMO Encrypted-TDEA-128-Key")
DEF_FEAT(PCKMO_ETDEA_256, "pckmo-etdea-192", PCKMO, 3, "PCKMO Encrypted-TDEA-192-Key")
DEF_FEAT(PCKMO_AES_128, "pckmo-aes-128", PCKMO, 18, "PCKMO Encrypted-AES-128-Key")
DEF_FEAT(PCKMO_AES_192, "pckmo-aes-192", PCKMO, 19, "PCKMO Encrypted-AES-192-Key")
DEF_FEAT(PCKMO_AES_256, "pckmo-aes-256", PCKMO, 20, "PCKMO Encrypted-AES-256-Key")
DEF_FEAT(PCKMO_AES_XTS_128_DK, "pckmo-aes-xts-128-dk", PCKMO, 21, "PCKMO Encrypt-AES-XTS-128-Double-Key")
DEF_FEAT(PCKMO_AES_XTS_256_DK, "pckmo-aes-xts-256-dk", PCKMO, 22, "PCKMO Encrypt-AES-XTS-256-Double-Key")
DEF_FEAT(PCKMO_ECC_P256, "pckmo-ecc-p256", PCKMO, 32, "PCKMO Encrypt-ECC-P256-Key")
DEF_FEAT(PCKMO_ECC_P384, "pckmo-ecc-p384", PCKMO, 33, "PCKMO Encrypt-ECC-P384-Key")
DEF_FEAT(PCKMO_ECC_P521, "pckmo-ecc-p521", PCKMO, 34, "PCKMO Encrypt-ECC-P521-Key")
DEF_FEAT(PCKMO_ECC_ED25519, "pckmo-ecc-ed25519", PCKMO, 40 , "PCKMO Encrypt-ECC-Ed25519-Key")
DEF_FEAT(PCKMO_ECC_ED448, "pckmo-ecc-ed448", PCKMO, 41 , "PCKMO Encrypt-ECC-Ed448-Key")
DEF_FEAT(PCKMO_HMAC_512, "pckmo-hmac-512", PCKMO, 118, "PCKMO Encrypt-HMAC-512-Key")
DEF_FEAT(PCKMO_HMAC_1024, "pckmo-hmac-1024", PCKMO, 122, "PCKMO Encrypt-HMAC-1024-Key")
DEF_FEAT(PCKMO_QAI, "pckmo-qai", PCKMO, 127, "PCKMO Query-Authentication-Information")

/* Features exposed via the KMCTR instruction. */
DEF_FEAT(KMCTR_DEA, "kmctr-dea", KMCTR, 1, "KMCTR DEA")
DEF_FEAT(KMCTR_TDEA_128, "kmctr-tdea-128", KMCTR, 2, "KMCTR TDEA-128")
DEF_FEAT(KMCTR_TDEA_192, "kmctr-tdea-192", KMCTR, 3, "KMCTR TDEA-192")
DEF_FEAT(KMCTR_EDEA, "kmctr-edea", KMCTR, 9, "KMCTR Encrypted-DEA")
DEF_FEAT(KMCTR_ETDEA_128, "kmctr-etdea-128", KMCTR, 10, "KMCTR Encrypted-TDEA-128")
DEF_FEAT(KMCTR_ETDEA_192, "kmctr-etdea-192", KMCTR, 11, "KMCTR Encrypted-TDEA-192")
DEF_FEAT(KMCTR_AES_128, "kmctr-aes-128", KMCTR, 18, "KMCTR AES-128")
DEF_FEAT(KMCTR_AES_192, "kmctr-aes-192", KMCTR, 19, "KMCTR AES-192")
DEF_FEAT(KMCTR_AES_256, "kmctr-aes-256", KMCTR, 20, "KMCTR AES-256")
DEF_FEAT(KMCTR_EAES_128, "kmctr-eaes-128", KMCTR, 26, "KMCTR Encrypted-AES-128")
DEF_FEAT(KMCTR_EAES_192, "kmctr-eaes-192", KMCTR, 27, "KMCTR Encrypted-AES-192")
DEF_FEAT(KMCTR_EAES_256, "kmctr-eaes-256", KMCTR, 28, "KMCTR Encrypted-AES-256")
DEF_FEAT(KMCTR_QAI, "kmctr-qai", KMCTR, 127, "KMCTR Query-Authentication-Information")

/* Features exposed via the KMF instruction. */
DEF_FEAT(KMF_DEA, "kmf-dea", KMF, 1, "KMF DEA")
DEF_FEAT(KMF_TDEA_128, "kmf-tdea-128", KMF, 2, "KMF TDEA-128")
DEF_FEAT(KMF_TDEA_192, "kmf-tdea-192", KMF, 3, "KMF TDEA-192")
DEF_FEAT(KMF_EDEA, "kmf-edea", KMF, 9, "KMF Encrypted-DEA")
DEF_FEAT(KMF_ETDEA_128, "kmf-etdea-128", KMF, 10, "KMF Encrypted-TDEA-128")
DEF_FEAT(KMF_ETDEA_192, "kmf-etdea-192", KMF, 11, "KMF Encrypted-TDEA-192")
DEF_FEAT(KMF_AES_128, "kmf-aes-128", KMF, 18, "KMF AES-128")
DEF_FEAT(KMF_AES_192, "kmf-aes-192", KMF, 19, "KMF AES-192")
DEF_FEAT(KMF_AES_256, "kmf-aes-256", KMF, 20, "KMF AES-256")
DEF_FEAT(KMF_EAES_128, "kmf-eaes-128", KMF, 26, "KMF Encrypted-AES-128")
DEF_FEAT(KMF_EAES_192, "kmf-eaes-192", KMF, 27, "KMF Encrypted-AES-192")
DEF_FEAT(KMF_EAES_256, "kmf-eaes-256", KMF, 28, "KMF Encrypted-AES-256")
DEF_FEAT(KMF_QAI, "kmf-qai", KMF, 127, "KMF Query-Authentication-Information")

/* Features exposed via the KMO instruction. */
DEF_FEAT(KMO_DEA, "kmo-dea", KMO, 1, "KMO DEA")
DEF_FEAT(KMO_TDEA_128, "kmo-tdea-128", KMO, 2, "KMO TDEA-128")
DEF_FEAT(KMO_TDEA_192, "kmo-tdea-192", KMO, 3, "KMO TDEA-192")
DEF_FEAT(KMO_EDEA, "kmo-edea", KMO, 9, "KMO Encrypted-DEA")
DEF_FEAT(KMO_ETDEA_128, "kmo-etdea-128", KMO, 10, "KMO Encrypted-TDEA-128")
DEF_FEAT(KMO_ETDEA_192, "kmo-etdea-192", KMO, 11, "KMO Encrypted-TDEA-192")
DEF_FEAT(KMO_AES_128, "kmo-aes-128", KMO, 18, "KMO AES-128")
DEF_FEAT(KMO_AES_192, "kmo-aes-192", KMO, 19, "KMO AES-192")
DEF_FEAT(KMO_AES_256, "kmo-aes-256", KMO, 20, "KMO AES-256")
DEF_FEAT(KMO_EAES_128, "kmo-eaes-128", KMO, 26, "KMO Encrypted-AES-128")
DEF_FEAT(KMO_EAES_192, "kmo-eaes-192", KMO, 27, "KMO Encrypted-AES-192")
DEF_FEAT(KMO_EAES_256, "kmo-eaes-256", KMO, 28, "KMO Encrypted-AES-256")
DEF_FEAT(KMO_QAI, "kmo-qai", KMO, 127, "KMO Query-Authentication-Information")

/* Features exposed via the PCC instruction. */
DEF_FEAT(PCC_CMAC_DEA, "pcc-cmac-dea", PCC, 1, "PCC Compute-Last-Block-CMAC-Using-DEA")
DEF_FEAT(PCC_CMAC_TDEA_128, "pcc-cmac-tdea-128", PCC, 2, "PCC Compute-Last-Block-CMAC-Using-TDEA-128")
DEF_FEAT(PCC_CMAC_TDEA_192, "pcc-cmac-tdea-192", PCC, 3, "PCC Compute-Last-Block-CMAC-Using-TDEA-192")
DEF_FEAT(PCC_CMAC_ETDEA_128, "pcc-cmac-edea", PCC, 9, "PCC Compute-Last-Block-CMAC-Using-Encrypted-DEA")
DEF_FEAT(PCC_CMAC_ETDEA_192, "pcc-cmac-etdea-128", PCC, 10, "PCC Compute-Last-Block-CMAC-Using-Encrypted-TDEA-128")
DEF_FEAT(PCC_CMAC_TDEA, "pcc-cmac-etdea-192", PCC, 11, "PCC Compute-Last-Block-CMAC-Using-EncryptedTDEA-192")
DEF_FEAT(PCC_CMAC_AES_128, "pcc-cmac-aes-128", PCC, 18, "PCC Compute-Last-Block-CMAC-Using-AES-128")
DEF_FEAT(PCC_CMAC_AES_192, "pcc-cmac-aes-192", PCC, 19, "PCC Compute-Last-Block-CMAC-Using-AES-192")
DEF_FEAT(PCC_CMAC_AES_256, "pcc-cmac-aes-256", PCC, 20, "PCC Compute-Last-Block-CMAC-Using-AES-256")
DEF_FEAT(PCC_CMAC_EAES_128, "pcc-cmac-eaes-128", PCC, 26, "PCC Compute-Last-Block-CMAC-Using-Encrypted-AES-128")
DEF_FEAT(PCC_CMAC_EAES_192, "pcc-cmac-eaes-192", PCC, 27, "PCC Compute-Last-Block-CMAC-Using-Encrypted-AES-192")
DEF_FEAT(PCC_CMAC_EAES_256, "pcc-cmac-eaes-256", PCC, 28, "PCC Compute-Last-Block-CMAC-Using-Encrypted-AES-256")
DEF_FEAT(PCC_XTS_AES_128, "pcc-xts-aes-128", PCC, 50, "PCC Compute-XTS-Parameter-Using-AES-128")
DEF_FEAT(PCC_XTS_AES_256, "pcc-xts-aes-256", PCC, 52, "PCC Compute-XTS-Parameter-Using-AES-256")
DEF_FEAT(PCC_XTS_EAES_128, "pcc-xts-eaes-128", PCC, 58, "PCC Compute-XTS-Parameter-Using-Encrypted-AES-128")
DEF_FEAT(PCC_XTS_EAES_256, "pcc-xts-eaes-256", PCC, 60, "PCC Compute-XTS-Parameter-Using-Encrypted-AES-256")
DEF_FEAT(PCC_SCALAR_MULT_P256, "pcc-scalar-mult-p256", PCC, 64, "PCC Scalar-Multiply-P256")
DEF_FEAT(PCC_SCALAR_MULT_P384, "pcc-scalar-mult-p384", PCC, 65, "PCC Scalar-Multiply-P384")
DEF_FEAT(PCC_SCALAR_MULT_P512, "pcc-scalar-mult-p521", PCC, 66, "PCC Scalar-Multiply-P521")
DEF_FEAT(PCC_SCALAR_MULT_ED25519, "pcc-scalar-mult-ed25519", PCC, 72, "PCC Scalar-Multiply-Ed25519")
DEF_FEAT(PCC_SCALAR_MULT_ED448, "pcc-scalar-mult-ed448", PCC, 73, "PCC Scalar-Multiply-Ed448")
DEF_FEAT(PCC_SCALAR_MULT_X25519, "pcc-scalar-mult-x25519", PCC, 80, "PCC Scalar-Multiply-X25519")
DEF_FEAT(PCC_SCALAR_MULT_X448, "pcc-scalar-mult-x448", PCC, 81, "PCC Scalar-Multiply-X448")
DEF_FEAT(PCC_QAI, "pcc-qai", PCC, 127, "PCC Query-Authentication-Information")

/* Features exposed via the PPNO/PRNO instruction. */
DEF_FEAT(PPNO_SHA_512_DRNG, "ppno-sha-512-drng", PPNO, 3, "PPNO SHA-512-DRNG")
DEF_FEAT(PRNO_TRNG_QRTCR, "prno-trng-qrtcr", PPNO, 112, "PRNO TRNG-Query-Raw-to-Conditioned-Ratio")
DEF_FEAT(PRNO_TRNG, "prno-trng", PPNO, 114, "PRNO TRNG")
DEF_FEAT(PRNO_QAI, "prno-qai", PPNO, 127, "PRNO Query-Authentication-Information")

/* Features exposed via the KMA instruction. */
DEF_FEAT(KMA_GCM_AES_128, "kma-gcm-aes-128", KMA, 18, "KMA GCM-AES-128")
DEF_FEAT(KMA_GCM_AES_192, "kma-gcm-aes-192", KMA, 19, "KMA GCM-AES-192")
DEF_FEAT(KMA_GCM_AES_256, "kma-gcm-aes-256", KMA, 20, "KMA GCM-AES-256")
DEF_FEAT(KMA_GCM_EAES_128, "kma-gcm-eaes-128", KMA, 26, "KMA GCM-Encrypted-AES-128")
DEF_FEAT(KMA_GCM_EAES_192, "kma-gcm-eaes-192", KMA, 27, "KMA GCM-Encrypted-AES-192")
DEF_FEAT(KMA_GCM_EAES_256, "kma-gcm-eaes-256", KMA, 28, "KMA GCM-Encrypted-AES-256")
DEF_FEAT(KMA_QAI, "kma-qai", KMA, 127, "KMA Query-Authentication-Information")

/* Features exposed via the KDSA instruction. */
DEF_FEAT(KDSA_ECDSA_VERIFY_P256, "kdsa-ecdsa-verify-p256", KDSA, 1, "KDSA ECDSA-Verify-P256")
DEF_FEAT(KDSA_ECDSA_VERIFY_P384, "kdsa-ecdsa-verify-p384", KDSA, 2, "KDSA ECDSA-Verify-P384")
DEF_FEAT(KDSA_ECDSA_VERIFY_P512, "kdsa-ecdsa-verify-p521", KDSA, 3, "KDSA ECDSA-Verify-P521")
DEF_FEAT(KDSA_ECDSA_SIGN_P256, "kdsa-ecdsa-sign-p256", KDSA, 9, "KDSA ECDSA-Sign-P256")
DEF_FEAT(KDSA_ECDSA_SIGN_P384, "kdsa-ecdsa-sign-p384", KDSA, 10, "KDSA ECDSA-Sign-P384")
DEF_FEAT(KDSA_ECDSA_SIGN_P512, "kdsa-ecdsa-sign-p521", KDSA, 11, "KDSA ECDSA-Sign-P521")
DEF_FEAT(KDSA_EECDSA_SIGN_P256, "kdsa-eecdsa-sign-p256", KDSA, 17, "KDSA Encrypted-ECDSA-Sign-P256")
DEF_FEAT(KDSA_EECDSA_SIGN_P384, "kdsa-eecdsa-sign-p384", KDSA, 18, "KDSA Encrypted-ECDSA-Sign-P384")
DEF_FEAT(KDSA_EECDSA_SIGN_P512, "kdsa-eecdsa-sign-p521", KDSA, 19, "KDSA Encrypted-ECDSA-Sign-P521")
DEF_FEAT(KDSA_EDDSA_VERIFY_ED25519, "kdsa-eddsa-verify-ed25519", KDSA, 32, "KDSA EdDSA-Verify-Ed25519")
DEF_FEAT(KDSA_EDDSA_VERIFY_ED448, "kdsa-eddsa-verify-ed448", KDSA, 36, "KDSA EdDSA-Verify-Ed448")
DEF_FEAT(KDSA_EDDSA_SIGN_ED25519, "kdsa-eddsa-sign-ed25519", KDSA, 40, "KDSA EdDSA-Sign-Ed25519")
DEF_FEAT(KDSA_EDDSA_SIGN_ED448, "kdsa-eddsa-sign-ed448", KDSA, 44, "KDSA EdDSA-Sign-Ed448")
DEF_FEAT(KDSA_EEDDSA_SIGN_ED25519, "kdsa-eeddsa-sign-ed25519", KDSA, 48, "KDSA Encrypted-EdDSA-Sign-Ed25519")
DEF_FEAT(KDSA_EEDDSA_SIGN_ED448, "kdsa-eeddsa-sign-ed448", KDSA, 52, "KDSA Encrypted-EdDSA-Sign-Ed448")
DEF_FEAT(KDSA_QAI, "kdsa-qai", KDSA, 127, "KDSA Query-Authentication-Information")

/* Features exposed via the SORTL instruction. */
DEF_FEAT(SORTL_SFLR, "sortl-sflr", SORTL, 1, "SORTL SFLR")
DEF_FEAT(SORTL_SVLR, "sortl-svlr", SORTL, 2, "SORTL SVLR")
DEF_FEAT(SORTL_32, "sortl-32", SORTL, 130, "SORTL 32 input lists")
DEF_FEAT(SORTL_128, "sortl-128", SORTL, 132, "SORTL 128 input lists")
DEF_FEAT(SORTL_F0, "sortl-f0", SORTL, 192, "SORTL format 0 parameter-block")

/* Features exposed via the DEFLATE instruction. */
DEF_FEAT(DEFLATE_GHDT, "dfltcc-gdht", DFLTCC, 1, "DFLTCC GDHT")
DEF_FEAT(DEFLATE_CMPR, "dfltcc-cmpr", DFLTCC, 2, "DFLTCC CMPR")
DEF_FEAT(DEFLATE_XPND, "dfltcc-xpnd", DFLTCC, 4, "DFLTCC XPND")
DEF_FEAT(DEFLATE_F0, "dfltcc-f0", DFLTCC, 192, "DFLTCC format 0 parameter-block")

/* Features exposed via the UV-CALL instruction */
DEF_FEAT(UV_FEAT_AP, "appv", UV_FEAT_GUEST, 4, "AP instructions installed for secure guests")
DEF_FEAT(UV_FEAT_AP_INTR, "appvi", UV_FEAT_GUEST, 5, "AP instructions interruption support for secure guests")

/* Features exposed via the PFCR instruction (concurrent-functions facility). */
DEF_FEAT(PFCR_QAF, "pfcr-qaf", PFCR, 0, "PFCR Query-Available-Functions")
DEF_FEAT(PFCR_CSDST, "pfcr-csdst", PFCR, 1, "PFCR Compare-and-Swap-and-Double-Store (32)")
DEF_FEAT(PFCR_CSDSTG, "pfcr-csdstg", PFCR, 2, "PFCR Compare-and-Swap-and-Double-Store (64)")
DEF_FEAT(PFCR_CSTST, "pfcr-cstst", PFCR, 3, "PFCR Compare-and-Swap-and-Triple-Store (32)")
DEF_FEAT(PFCR_CSTSTG, "pfcr-cststg", PFCR, 4, "PFCR Compare-and-Swap-and-Triple-Store (64)")
