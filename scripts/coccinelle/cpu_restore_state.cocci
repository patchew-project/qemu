// Remove unneeded tests before calling cpu_restore_state
//
// spatch --macro-file scripts/cocci-macro-file.h \
//        --sp-file ./scripts/coccinelle/cpu_restore_state.cocci \
//        --keep-comments --in-place --use-gitgrep --dir target
@@
identifier A;
expression C;
@@
-if (A) {
     cpu_restore_state(C, A);
-}
