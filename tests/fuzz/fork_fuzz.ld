/* We adjust linker script modification to place all of the stuff that needs to
 * persist across fuzzing runs into a contiguous seciton of memory. Then, it is
 * easy to copy it to and from shared memory.
 *
 * Total Size : A5A00
 * Sancov counters: B26F
 * Coverage counters: 56D60
 * TracePC Object: 43C00
*/

SECTIONS
{
  .data.fuzz_start : ALIGN(4K)
  {
        __FUZZ_COUNTERS_START = .;
  }
  .data.fuzz_ordered :
  {
      /* Internal Libfuzzer TracePC object which contains the ValueProfileMap.
       * Not optimal that we have to copy the rest of the TracePC object.
       * */
      __start___sancov_cntrs = .;
      *(__sancov_cntrs*)
      __stop___sancov_cntrs = .;
  }
  .data.fuzz_unordered :
  {
      /* Coverage counters. They're not necessary for fuzzing, but are useful
       * for analyzing the fuzzing performance
       * */
      __start___llvm_prf_cnts = .;
      *(*llvm_prf_cnts);
      __stop___llvm_prf_cnts = .;

      /* Lowest stack counter */
      *(__sancov_lowest_stack);
      /* Internal Libfuzzer TracePC object which contains the ValueProfileMap.
       * Not optimal that we have to copy the rest of the TracePC object.
       * */
      *FuzzerTracePC*(.bss._ZN6fuzzer*)
      __FUZZ_COUNTERS_END = .;
  }
}
/* Dont overwrite the SECTIONS in the default linker script. Instead insert the
 * above into the default script */
INSERT AFTER .data;
