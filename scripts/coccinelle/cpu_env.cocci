/*
 * Convert &ARCH_CPU(..)->env to use cpu_env(..).
 *
 * Rationale: ARCH_CPU() might be slow, being a QOM cast macro.
 *            cpu_env() is its fast equivalent.
 *            CPU() macro is a no-op.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * SPDX-FileCopyrightText: Linaro Ltd 2024
 * SPDX-FileContributor: Philippe Mathieu-Daudé
 */

@@
type ArchCPU =~ "CPU$";
identifier cpu;
type CPUArchState =~ "^CPU";
identifier env;
@@
     ArchCPU *cpu;
     ...
     CPUArchState *env = &cpu->env;
     <...
-    &cpu->env
+    env
     ...>


/*
 * Due to commit 8ce5c64499 ("semihosting: Return failure from
 * softmmu-uaccess.h functions"), skip functions using softmmu-uaccess.h
 * macros (they don't pass 'env' as argument).
 */
@ uaccess_api_used exists @
identifier semihosting_func =~ "^(put|get)_user_[us](al|8|16|32)$";
@@
      semihosting_func(...)


/*
 * Argument is CPUState*
 */
@ cpustate_arg depends on !uaccess_api_used @
identifier cpu;
type ArchCPU =~ "CPU$";
type CPUArchState;
identifier ARCH_CPU =~ "CPU$";
identifier env;
CPUState *cs;
@@
-    ArchCPU *cpu = ARCH_CPU(cs);
     ...
-    CPUArchState *env = &cpu->env;
+    CPUArchState *env = cpu_env(cs);
     ... when != cpu


/*
 * Argument is not CPUState* but a related QOM object.
 * CPU() is not a QOM macro but a cast (See commit 0d6d1ab499).
 */
@ depends on !uaccess_api_used  && !cpustate_arg @
identifier cpu;
type ArchCPU =~ "CPU$";
type CPUArchState;
identifier ARCH_CPU =~ "CPU$";
identifier env;
expression cs;
@@
-    ArchCPU *cpu = ARCH_CPU(cs);
     ...
-    CPUArchState *env = &cpu->env;
+    CPUArchState *env = cpu_env(CPU(cs));
     ... when != cpu


/* When single use of 'env', call cpu_env() in place */
@ depends on !uaccess_api_used @
type CPUArchState;
identifier env;
expression cs;
@@
-    CPUArchState *env = cpu_env(cs);
     ... when != env
-     env
+     cpu_env(cs)
     ... when != env


/* Both first_cpu/current_cpu are extern CPUState* */
@@
symbol first_cpu;
symbol current_cpu;
@@
(
-    CPU(first_cpu)
+    first_cpu
|
-    CPU(current_cpu)
+    current_cpu
)
