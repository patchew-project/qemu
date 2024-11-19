# Toward a Heterogeneous QEMU

## Introduction

This document outlines a comprehensive roadmap for enhancing the
modularity, scalability, and multi-architecture support of QEMU.
The goal is to address several long-standing architectural issues
that hinder QEMU’s ability to emulate multiple architectures
concurrently, paving the way toward dynamic machine creation and
improved user experiences.

QEMU’s original design focuses on the efficient emulation of a
single target architecture per binary, with a single TCG context
handling architecture-specific code generation and execution.
However, as the demand for more flexible and scalable emulation
grows, several limitations of this design have become evident.
These include reliance on global variables, target-specific static
definitions, tightly coupled APIs across different subsystems, and
the need for manual intervention when building or configuring
machine models.

The proposed roadmap introduces a series of incremental changes
aimed at addressing these limitations and achieving the following
long-term goals:

1. **Improved Modularity and Code Reusability**: Refactor QEMU's
   subsystems to eliminate global variables and reduce the coupling
   between hardware models and target-specific implementations.
   This will involve creating clearer API boundaries and separating
   hardware and target-specific code into distinct, reusable
   modules.

2. **Support for Multi-Architecture Emulation**: Enable QEMU to
   emulate multiple target architectures concurrently within a
   single binary by modularizing the TCG frontends and decoupling
   architecture-specific code from the core emulation logic.

3. **Simplification of QEMU's Core Components**: Streamline the
   startup sequence, refactor the command-line interface (CLI), and
   modularize the device models and subsystems to improve
   maintainability and extensibility. This effort will also focus
   on reducing redundancy and boilerplate code in the machine
   creation process.

4. **Human vs. Machine Configuration**: Recognize the divergent
   needs between human users and management applications (machines)
   in configuring QEMU. Propose a separation of concerns where a
   new binary could focus on machine-oriented expressiveness while
   providing a simpler, more intuitive frontend for human users.

5. **Dynamic Machine Creation and Configuration**: Transition from
   statically built machine models to a dynamic, data-driven
   configuration system, where new machine types can be composed at
   runtime using declarative templates. This will reduce the
   complexity of adding new machines and allow for greater
   flexibility in system configuration.

The roadmap is divided into multiple phases, each addressing a
specific set of problems and dependencies in QEMU’s current
architecture.

This document serves as a reference for contributors. It aims to
provide a clear path forward, along with the necessary steps and
dependencies required to implement the proposed changes.

## Background and Motivation

### Issues with Loadable Modules

QEMU's support for loadable modules has deficiencies:

- **Error Handling**: Modules that fail to load due to missing
  dependencies can cause QEMU to exit unexpectedly, especially
  during hot-plug operations. The module API needs to be
  strengthened to propagate errors and must not exit the main
  process. Proper error handling mechanisms are essential to
  prevent crashes and improve stability.

- **Platform Parity**: Currently, QEMU's support for loadable
  modules is not consistent across all platforms. In particular,
  DSO module loading needs to be implemented on Windows to achieve
  feature parity with other operating systems.

- **Introspection Challenges**: Functions that list or load modules
  may not handle errors gracefully, leading to incomplete
  information and unpredictable behavior. Users may not be aware of
  whether a module is compiled-in or loadable, leading to
  confusion.

### Problems with Singletons and Global Symbols

Global variables and singletons hinder the ability to:

- **Support Multi-Architecture Binaries**: The use of global
  variables (or singletons) causes conflicts when multiple
  architectures are emulated concurrently. A frontend expects the
  global variable to be accessed only by itself, but when using
  multiple frontends concurrently, multiple frontends could access
  the same variable, causing unexpected side effects.

- **Modularize Code**: Singletons prevent instantiating multiple
  instances of certain components, limiting flexibility. Symbol
  conflicts and shared globals cause linking issues and
  unpredictable behavior in heterogeneous machines.

### Inconsistent QOM Object Life Cycle

The lack of a unified life cycle model for QOM objects complicates
configuration and management:

- **Different Life Cycles**: Devices, user-creatable objects, and
  other components have varying life cycles and state transitions.
  Without a consistent model, it's difficult to determine when and
  how objects should be configured and realized, especially in a
  dynamic environment.

- **Configuration Challenges**: Managing object states and
  transitions is difficult without a clear life cycle model. This
  inconsistency leads to complex and error-prone configuration
  processes.

### Challenges with Initial Configuration

QEMU's current configuration mechanisms present challenges for
both management applications and human users:

- **Management Applications**: Require a stable and simple CLI to
  bootstrap QMP (QEMU Machine Protocol) for monitoring and initial
  configuration. However, they are forced to perform non-trivial
  initial configurations via the CLI, which is not ideal.

- **Human Users**: Prefer simple, consistent CLI and configuration
  files. They struggle with the complexity and inconsistency of the
  modern low-level configurations, leading to frustration and
  errors.

### Limitations of Static Machine Definitions

Machines in QEMU are defined statically in C code, requiring
recompilation for any changes. This approach lacks flexibility and
makes it difficult to:

- **Enable Dynamic Machine Creation**: Allow users and management
  applications to define and configure machines at runtime without
  modifying source code.

### Need for Early Availability of QMP

QMP becomes available late in the startup sequence, limiting its
usefulness for initial configuration. An integrated solution is
needed to make QMP available earlier, enabling:

- **Dynamic Configuration via QMP**: Allow management applications
  to perform arbitrary configurations before any machine components
  are initialized.

- **Simplified Bootstrapping**: Provide a minimal CLI that is
  stable and sufficient to start QMP for further configuration.

### Deficiencies in the Configuration Interface

QOM's object configuration interface has limitations:

- **Dynamic Properties**: Properties can be added or removed
  dynamically, leading to a configuration interface that changes at
  runtime.

- **Introspection Limitations**: Weak and often undocumented type
  information makes it challenging to design a robust and
  user-friendly configuration interface.

### Cross-Directory API Calls and Target-Specific Device Models

- **Cross-Directory API Calls**: Problematic calls between `hw/`
  and `target/` directories blur separation and complicate
  modularization. Unfortunately, we have API calls from one
  directory to another. For example, `hw/xtensa/pic_cpu.c` calls
  target `xtensa_runstall()`, and `target/xtensa/exc_helper.c`
  calls `check_interrupts()` in `hw/xtensa/pic_cpu.c`.

- **Target-Specific Device Models in `hw/`**: Placing
  target-specific models in `hw/` leads to dependencies that hinder
  modularization. When a device model is target-specific (like the
  ARM NVIC), it is pointless to expose it in `hw/`. Moving it to
  `target/arm/hw/` could simplify the `hw/` <-> `target/` access
  problem.

### Device Models and Buses

- **Singleton Buses**: The expectation of a single system bus
  (sysbus) doesn't scale for heterogeneous machines requiring
  multiple buses. Currently, a machine expects a single sysbus and
  at most a single ISA bus. This doesn't scale since heterogeneous
  machines might use multiple distinct buses.

- **Bus Ownership Model**: The current ownership model doesn't
  support buses shared by multiple controllers. Currently, a bus
  model is "QOM owned" by a single device controller model. This
  doesn't scale when a bus is shared by two controllers since we
  can only have a single QOM owner.

### Page Size and Target-Specific Definitions

- **Static Definitions**: Target-specific static definitions need
  to be converted to runtime variables to support multiple
  architectures. For example, definitions such as
  `TARGET_LONG_BITS`, `TARGET_PAGE_MASK`, `TARGET_PAGE_BITS`,
  `TCG_GUEST_DEFAULT_MO`, `NB_MMU_MODES`,
  `TARGET_PHYS_ADDR_SPACE_BITS`, and
  `TARGET_VIRT_ADDR_SPACE_BITS` need to be changed to runtime ones.

- **Variable Page Sizes**: The current use of a single
  `TargetPageBits` structure is insufficient for variable page
  sizes across architectures. We cannot use a single
  `TargetPageBits` structure anymore for variable page sizes; we
  need to make this structure per vCPU.

### Global Headers and `NEED_CPU_H` Definition

- **Header Contamination**: Too many global headers are
  'contaminated' with the `NEED_CPU_H` definition, which forces the
  header to become target specific. This complicates modularization
  and code reuse.

## Identified Problems

### Problem 1: Loadable Modules and Error Handling

**Current Challenges:**

The current implementation of loadable modules in QEMU has several
deficiencies, particularly in error handling. When a module fails
to load due to missing dependencies, QEMU may exit unexpectedly,
which can be especially problematic during hot-plug operations.
Additionally, the introspection mechanisms may not list all
available types if modules fail to load silently.

**Proposed Solutions:**

1. **Enhance Error Handling for Loadable Modules:**

   - Improve the module loading API to handle failures gracefully
     without crashing the main process. This includes strengthening
     the module API to propagate errors properly and avoid
     unexpected exits.
   - Ensure that introspection commands like `qom-list-types`
     provide accurate information, even in the presence of loadable
     modules.
   - Modify the module API to propagate errors properly and avoid
     unexpected exits.

2. **Implement DSO Module Loading on Windows:**

   - Add feature parity for DSO module support on Windows OS to
     ensure consistent behavior across platforms.

3. **Testing and Stability:**

   - Implement thorough testing to ensure that the changes do not
     introduce new issues or regressions, particularly in the
     context of module loading and hot-plugging.

### Problem 2: Singletons and Global Symbols

**Current Challenges:**

Global variables and singletons in QEMU's codebase pose challenges
for multi-architecture support. These elements can lead to
conflicts and unpredictable behavior, especially when trying to
link multiple targets into a single binary or manage heterogeneous
machines.

**Proposed Solutions:**

1. **Enumerate and Isolate Global Variables and Singletons:**

   - Identify all global variables and singletons that need to be
     isolated. Refactor the codebase to eliminate singletons and
     reduce reliance on global symbols, particularly in areas where
     multiple architectures are involved.
   - This change is essential for enabling concurrent
     multi-architecture emulation and supporting dynamic machine
     creation.

2. **Modify Function Prototypes and Structures:**

   - Change methods and structures that are currently
     target-specific to be target-agnostic. For example, functions
     like `tcg_gen_code()` and structures like `TranslationBlock`
     and `TCGContext` should become target-agnostic.

### Problem 3: Target-Specific Static Definitions

**Current Challenges:**

Target-specific static definitions prevent the code from being
flexible enough to handle multiple architectures simultaneously.
Definitions like `TARGET_LONG_BITS`, `TARGET_PAGE_MASK`, and others
are statically defined and cannot vary at runtime.

**Proposed Solutions:**

1. **Convert Static Definitions to Runtime Variables:**

   - Change target-specific static definitions to runtime variables
     or structure fields. For instance:

     - Convert `TCG_GUEST_DEFAULT_MO` definition to an
       `ArchAccelClass` field.
     - Convert `TARGET_PHYS_ADDR_SPACE_BITS` and
       `TARGET_VIRT_ADDR_SPACE_BITS` definitions to `CPUClass`
       fields.
     - Convert `TARGET_PAGE_BITS_MIN` definition to a `CPUClass`
       field.
     - Convert `TARGET_LONG_BITS` definition to a `CPUClass`
       helper function.
     - Convert `TARGET_PAGE_*` definitions to runtime variables.

2. **Support Variable Page Sizes:**

   - Convert all MTTCG-enabled targets to use
     `TARGET_PAGE_BITS_VARY`. Since multiple vCPUs can share the
     same accelerator and page sizes, better would be to have a set
     of compatible vCPUs share the same accelerator context.

### Problem 4: TCG Frontend and Code Generation

**Current Challenges:**

Functions like `tcg_gen_code()` are too target-specific and need to
be made target-agnostic. The TCG frontend is built multiple times
for different endianness and word sizes, leading to redundancy.

**Proposed Solutions:**

1. **Make TCG Code Target-Agnostic:**

   - Refactor `tb_gen_code()` and `tcg_gen_code()` functions to be
     target-agnostic, keeping `gen_intermediate_code()`
     target-specific.
   - Ensure that TCG frontends leverage the `MemOp` argument to be
     built once, reducing redundancy.

2. **Modularize TCG Frontends:**

   - Build the TCG frontend for each architecture as a library
     (e.g., `libtcg-$ARCH.so`). Such a library should be
     exclusively composed of compilation units in `target/$ARCH`,
     possibly including `target/$arch/hw/` specific hardware models.
   - Modularize the set of TCG frontends for all targets.

### Problem 5: Cross-Directory API Calls and Target-Specific Device Models

**Current Challenges:**

Problematic API calls between `hw/` and `target/` directories blur
separation and complicate modularization. Target-specific device
models placed in `hw/` lead to dependencies that hinder
modularization.

**Proposed Solutions:**

1. **Enforce API Separation:**

   - Remove non-QDev API calls from `target/` to `hw/` (and
     vice-versa), restricting to QOM, QDev, IRQ, and Clock APIs.
   - Enforce a clearer API separation to avoid direct calls and
     enable both `hw/` and `target/` components to be built as
     modules.

2. **Move Target-Specific Device Models:**

   - When a device model is target-specific, move it from `hw/` to
     `target/$ARCH/hw/`. This simplifies the `hw/` <-> `target/`
     access problem and reduces dependencies.

### Problem 6: Device Models and Buses

**Current Challenges:**

The current bus models and APIs assume singleton buses and device
ownership by a single controller, which doesn't scale for
heterogeneous machines that require multiple buses and shared
ownership.

**Proposed Solutions:**

1. **Rework Bus Models and Ownership:**

   - Remove device ownership on multi-controller buses, making the
     single owner the `MachineState`. This allows buses to be
     accessed by multiple controller models without ownership
     conflicts.

2. **Make Bus Parameters Explicit:**

   - Modify APIs to make the bus parameter explicit, so devices can
     be accessed on any bus. This removes the reliance on singleton
     buses and allows for multiple distinct buses in heterogeneous
     machines.

3. **Remove Singleton Buses:**

   - Remove the `ISA_BUS` singleton and replace ISA bus-dependent
     devices with stubs to simplify proof of concept.

### Problem 7: Lack of a Clear QOM Object Life Cycle

**Current Challenges:**

QOM lacks a unified and well-defined life cycle model, leading to
inconsistencies and complexity in managing object states during
configuration.

**Proposed Solutions:**

1. **Clarify QDev Methods and Lifecycle:**

   - Clarify which QDev methods belong to the QOM layer, such as
     'realize'. Define a unified life cycle model that can be
     applied across all object types in QOM, ensuring predictable
     transitions between states.

2. **Add a "Wiring" Phase:**

   - Add an extra "wiring" phase in the QDev state machine to link
     parts together. This is essential when composing complex
     device models from smaller parts and when devices need to
     reference each other before realization.

3. **Merge SysBus Methods into QDev:**

   - Merge most of the `SysBus` methods into the more generic QDev
     layer: named GPIO and MemoryRegion. This unification simplifies
     the device model and reduces redundancy.

### Problem 8: Deficiencies in QOM's Object Configuration Interface

**Current Challenges:**

The current QOM object configuration interface is dynamic and often
unintuitive, with properties being added or removed during runtime.
This leads to a complex and sometimes error-prone configuration
process, particularly when trying to achieve a declarative
configuration style.

**Proposed Solutions:**

1. **Integrate Configuration into QAPI:**

   - Use static QAPI-based QOM properties to provide a more
     standardized, introspectable, and documented approach to
     object configuration. Restrict dynamic read-write properties
     to debugging and introspection.

2. **Make Config Properties Read-Only After Realization:**

   - Ensure that configuration properties are read-only after the
     device is realized. This simplifies the configuration
     interface and enforces consistency.

3. **Differentiate Property Types:**

   - Distinguish between static properties (set during creation)
     and dynamic properties (modifiable at runtime), simplifying
     the configuration process.

### Problem 9: Initial Configuration & CLI Challenges

**Current Challenges:**

The current CLI is cumbersome and unintuitive, making it difficult
for both human users and management applications to configure
machines efficiently. Human users often prefer simpler, legacy-
style configurations but are sometimes forced to engage with low-
level, detailed configurations when dealing with specific features,
leading to a frustrating user experience. Management applications,
on the other hand, require a more expressive and detailed
configuration system, further complicating the situation.

**Proposed Solutions:**

1. **Split Human vs. Machine Data:**

   - Propose a clean separation between human-friendly and machine-
     oriented configurations. Extract or factor out the CLI API
     (human-facing), keeping the minimum needed to start a QMP
     interface (machine-facing).

2. **Simplify the QEMU Startup Sequence:**

   - Rework the QEMU startup code to greatly simplify it. This
     involves simplifying CLI limitations (e.g. where command line
     order matters) and eventually moving towards data-driven machine
     configurations.

3. **Make All Devices User-Creatable:**

   - Eventually, all devices will become user-creatable, allowing
     for more flexible and dynamic machine configurations. Audit
     the effect on the final Realize phase in
     `qdev_machine_creation_done()`. Restrict UserCreatable API
     to backends.

## Roadmap

The roadmap is structured into phases, each building upon the
previous and allowing for parallel work where feasible. Time
estimates are provided for each task, assuming a dedicated
development team.

### Phase 0: Preparatory Work (Estimated Duration: 2 Months)

**Objective**: Establish a foundation for multi-architecture
support and improve module handling.

1. **Enhance Module Loading API** (3 Weeks)

   - Modify the module loading API to handle failures gracefully
     without crashing the main process. This includes strengthening
     the module API to propagate errors properly and avoid
     unexpected exits.

2. **Implement DSO Module Loading on Windows** (2 Weeks)

   - Add feature parity for DSO module support on Windows OS. This
     ensures that QEMU's module loading capabilities are consistent
     across platforms, enabling broader adoption and testing of
     modular components on Windows systems.

3. **Deprecate Non-QOM Code** (2 Weeks)

   - Introduce runtime warnings for the use of non-QOM code to
     encourage migration to QOM-compliant implementations.
     Explicitly deprecate non-QOM code, guiding developers towards
     the modern object model.

### Phase 1: Enable Heterogeneous Emulation (Estimated Duration: 6 Months)

**Objective**: Modify the core infrastructure to support multiple
architectures concurrently.

1. **Enumerate and Isolate Global Variables and Singletons** (6
   Weeks)

   - Identify all global variables and singletons that have to be
     isolated. Refactor the codebase to eliminate or encapsulate
     them to prevent conflicts when multiple architectures are
     emulated concurrently.

2. **Resolve Function Name Clashes** (2 Weeks)

   - Implement dispatch mechanisms for target-specific functions.
     For functions that have the same name across targets, create
     generic function names that dispatch to the proper target
     handler using the vCPU to discriminate which architecture
     handler to call.

3. **Rework QMP Handlers for Multi-Architecture** (3 Weeks)

    - Modify QMP target-specific handlers to handle multiple
      architectures gracefully. Alter QMP introspection as needed,
      so previously not implemented methods are now dispatched and,
      if not available for a particular target, return appropriate
      error messages like "Not Available".

4. **Make TCG Code Target-Agnostic** (6 Weeks)

   - Refactor `tb_gen_code()` and `tcg_gen_code()` functions to be
     target-agnostic, keeping `gen_intermediate_code()`
     target-specific. Ensure that `TranslationBlock` and
     `TCGContext` structures become target-agnostic.

5. **Convert Static Definitions to Runtime Variables** (6 Weeks)

   - Change target-specific static definitions to runtime variables
     or structure fields. For instance:

     - Convert `TCG_GUEST_DEFAULT_MO` definition to an
       `ArchAccelClass` field.
     - Convert `TARGET_PHYS_ADDR_SPACE_BITS` and
       `TARGET_VIRT_ADDR_SPACE_BITS` definitions to `CPUClass`
       fields.
     - Convert `TARGET_PAGE_BITS_MIN` definition to a `CPUClass`
       field.
     - Convert `TARGET_LONG_BITS` definition to a `CPUClass`
       helper function.
     - Convert `TARGET_PAGE_*` definitions to runtime variables.

6. **Support Variable Page Sizes** (3 Weeks)

   - Convert all MTTCG-enabled targets to use
     `TARGET_PAGE_BITS_VARY`. Since multiple vCPUs can share the
     same accelerator and page sizes, create a set of compatible
     vCPUs that share the same accelerator context.

7. **Compile `accel/tcg/` System Once** (2 Weeks)

   - Modify the build system to compile the TCG accelerator system
     only once, making it target-agnostic and shared across
     architectures.

8. **Modularize TCG Frontends** (4 Weeks)

   - Build TCG frontends as separate modules (e.g.,
     `libtcg-$ARCH.so`). Such a library should be exclusively
     composed of compilation units in `target/$ARCH`. Modularize
     the set of TCG frontends for all targets.

9. **Introduce `AccelCpuCluster` Concept** (5 Weeks)

   - Implement `AccelCpuCluster` to group similar vCPUs that share
     the same accelerator (TCG) state. This allows for efficient
     handling of vCPUs with common properties.

10. **Make `SoftFloat` context configuratble at runtime** (4 Weeks)

   - Convert per-target static definitions of SoftFloat context to
     runtime ones, possibly allowing re-use between vCPUs.

11. **Convert Non-MTTCG Targets** (4 Weeks)

   - Extend the previous steps to include non-MTTCG targets,
     ensuring that all targets can benefit from the new
     architecture.

12. **Make SoftFloat code target-agnostic** (2 Weeks)

   - Convert per-target static configurations to a runtime configurable
     context.

### Phase 2: Hardware Cleanups and API Separation (Estimated Duration: 3 Months)

**Objective**: Refine hardware models and APIs to support
modularization and multi-architecture support.

1. **Reduce `NEED_CPU_H` in Headers** (2 Weeks)

    - Split headers containing `NEED_CPU_H` into target-specific
      and target-agnostic ones. Use the `-common.h` suffix for
      target-agnostic headers and `-target.h` for target-specific
      ones.

2. **Eliminate Randomness in QOM Paths** (2 Weeks)

   - Remove auto-incremented IDs and random numbers in device QOM
     paths to keep the composition tree reproducible. Ensure that
     devices have stable and predictable QOM paths.

3. **Clarify QOM/QDev Methods and Lifecycle** (4 Weeks)

   - Clarify which QDev methods belong to the QOM layer, such as
     'realize'. Define a unified life cycle model for QOM objects.
     This includes adding an extra "wiring" phase in the QDev state
     machine to link parts together.

4. **Move Target-Specific Code from `hw/` to `target/hw/`** (3
   Weeks)

   - When a device model is target-specific, move it from `hw/` to
     `target/$ARCH/hw/`. This simplifies the `hw/` <-> `target/`
     access problem and reduces dependencies.

5. **Enforce API Separation Between `hw/` and `target/`** (4 Weeks)

   - Remove non-QDev API calls from `target/` to `hw/` (and
     vice-versa), restricting to QOM, QDev, IRQ, and Clock APIs.
     Enforce a clearer API separation to avoid direct calls and
     enable both `hw/` and `target/` components to be built as
     modules.

6. **Restrict Use of Global CPU Variables in `hw/`** (4 Weeks)

   - Prohibit the use of global CPU variables like `current_cpu`
     and `first_cpu` in hardware code (`hw/`). Instead, use
     `CpuCluster[]` or pass explicit references. This prevents
     casting errors and architecture-specific assumptions in common
     code.

7. **Merge SysBus Methods into QDev** (3 Weeks)

   - Merge most of the `SysBus` methods into the more generic QDev
     layer, starting with GPIO. This unification simplifies the
     device model and reduces redundancy.

8. **Remove `ISA_BUS` Singleton** (4 Weeks)

   - Remove the `ISA_BUS` singleton. Replace ISA bus-dependent
     devices with stubs or rework them to not rely on the singleton,
     simplifying the bus model.

9. **Optimize QOM Cast Macros** (2 Weeks)

    - Reduce and optimize the use of QOM cast macros in non-API
      code and callbacks to improve performance and maintainability.

10. **Consider Power/Clock/Reset Tree API** (1 Week)

    - Brainstorm whether a Power/Clock/Reset tree API is needed,
      which could be helpful later with device composition.

### Phase 3: Unify Binaries and Simplify Machine Modes (Estimated Duration: 2 Months)

**Objective**: Move towards a single QEMU binary supporting
multiple architectures and modes.

1. **Rework Bus Models and Ownership** (4 Weeks)

   - Remove device ownership on multi-controller buses, making the
     single owner the `MachineState`. Modify bus APIs to make the
     bus parameter explicit, allowing devices to be accessed on any
     bus.

2. **Eliminate `qdev_get_machine()` Usage** (5 Weeks)

   - Refactor code to remove reliance on `qdev_get_machine()`.
     Fields in `MachineState` that are target-specific (e.g.,
     `dtb`, `kernel`, `initrd`, `NumaState`, `CpuTopology`) should
     become per-vCPU or per cluster.

3. **Add Machine Property for Mode Selection** (2 Weeks)

   - Add a machine property to specify whether to start in 32-bit
     or 64-bit mode and forward the property down to devices. This
     allows for flexible configuration of machines that can operate
     in different modes.

4. **Unify Word Size Support** (4 Weeks)

   - Modify the build system to produce a single binary supporting
     both 32-bit and 64-bit architectures, unifying word size
     support.

5. **Unify Endianness Support** (4 Weeks)

   - Similarly, unify endianness support to produce a single binary
     capable of handling both big-endian and little-endian
     architectures.

6. **Investigate GDB Stub Restrictions** (2 Weeks)

   - Investigate any restrictions in the GDB stub that may prevent
     multi-architecture support or affect debugging capabilities.

7. **Disallow Unattached QOM Devices** (2 Weeks)

   - Do not allow unattached QOM devices. Ensure that all devices
     are part of the composition tree, eliminating the
     `/machine/unattached/` orphanage problem.

8. **Remove Target-Specific QMP and Monitor Commands** (2 Weeks)

   - Remove target-specific QMP and monitor commands to provide a
     consistent interface across architectures.

### Phase 4: Prepare for Declarative Machine Configuration (Estimated Duration: 3 Months)

**Objective**: Lay the groundwork for dynamic, data-driven machine
configurations.

1. **Static QAPI-Based QOM Properties** (8 Weeks)

   - Integrate the configuration interface into the QAPI schema
     using static QAPI-based QOM properties. Restrict dynamic
     read-write properties to debugging and introspection. Make
     configuration properties read-only after realization.

2. **Clarify Machine Phases Enum** (2 Weeks)

   - Clarify and extend the Machine Phases enum. Determine if a
     `PHASE_MACHINE_HARDWARE_WIRED` is needed before
     `PHASE_MACHINE_INITIALIZED` to be able to wire cyclic IRQs.

3. **Simplify QEMU Startup Sequence** (12 Weeks)

   - Rework the QEMU startup code to greatly simplify it. Extract
     or factor out the CLI API (human-facing), keeping the minimum
     needed to start a QMP interface (machine-facing).

4. **Enhance QOM Object Registration and Filtering** (3 Weeks)

   - Register all QOM objects at startup with appropriate
     filtering. Implement functionality to list devices filtered
     per machine. Handle cases where devices are only available in
     certain modes or architectures.

5. **Merge `UserCreatable` with QOM** (2 Weeks)

   - Integrate the `UserCreatable` class into QOM for streamlined
     object creation. If eventually all devices can be created by
     QMP when building a machine, all devices thus inherit the
     `UserCreatable` class.

6. **Early Availability of QMP** (4 Weeks)

   - Make QMP available earlier in the startup sequence for initial
     configuration. Provide a minimal CLI that is stable and
     sufficient to start QMP for further configuration.

### Phase 5: Implement Dynamic Machines (Estimated Duration: 4 Months)

**Objective**: Enable the creation of machines dynamically at
runtime using a declarative approach.

1. **Develop Declarative Language or DSL** (12 Weeks)

   - Create a Domain-Specific Language (DSL) or use existing
     formats to describe machine configurations. Devices are
     expressed as DSL, enabling composable machines.

2. **Refactor Machine Creation Process** (8 Weeks)

   - Rewrite the machine initialization code to consume declarative
     configurations. Transition from statically built machine
     models to a dynamic, data-driven configuration system.

3. **Simplify and Modularize Startup Code** (4 Weeks)

   - Streamline the startup sequence to accommodate dynamic
     machine creation. Simplify the QEMU startup sequence, focusing
     on modularity and flexibility.

4. **Provide Simplified Configuration Profiles** (3 Weeks)

   - Introduce predefined machine profiles for common use cases
     (e.g., `q35-minimal.cfg`, `q35-recommended.cfg`,
     `q35-simple.cfg`). Allow users to select a configuration that
     best fits their scenario without needing to fine-tune every
     parameter.

5. **Address Migration Compatibility** (4 Weeks)

   - Ensure that data-driven machine definitions support migration
     compatibility. Handle the "sensible defaults" problem by
     providing default settings for RAM size, CPU model, etc.

6. **Acknowledge and Plan for New Challenges** (Ongoing)

   - Recognize and address new challenges arising from the
     transition to data-driven machines, such as managing the QOM
     object life cycle, handling complex device compositions, and
     ensuring performance and stability.

### Phase 6: Enhance User Experience for Human Users (Estimated Duration: 2 Months)

**Objective**: Provide a user-friendly interface and configuration
profiles for human users.

1. **Develop Human-Focused Frontend** (4 Weeks)

   - Create a frontend that abstracts complexity and provides a
     simple CLI for human users. Recognize the divergent needs
     between human users and management applications, and offer a
     solution that caters to both.

2. **Implement Simplified Configuration Profiles** (3 Weeks)

   - Offer predefined profiles (e.g., minimal, recommended,
     simple) for different use cases. Allow users to easily select
     configurations without dealing with low-level details.

3. **Documentation and User Guides** (2 Weeks)

   - Provide comprehensive documentation to assist users in
     transitioning to the new interface. Include examples and
     guides for common tasks.

4. **Feedback and Iteration** (Ongoing)

   - Gather user feedback and iteratively improve the frontend and
     profiles. Ensure that the solution meets the needs of the
     community.

### Total Estimated Duration: Approximately 20 Months

**Note**: These time estimates are approximate and may vary based
on the size of the development team, complexity of tasks, and
unforeseen challenges.

## Conclusion

By addressing the identified problems through this structured
roadmap, QEMU can evolve into a more modular, flexible, and user-
friendly platform capable of concurrent multi-architecture
emulation. The proposed changes will enable dynamic machine
creation, improve code maintainability, and enhance the user
experience for both management applications and human users.

Implementing this roadmap requires coordinated effort and
collaboration among the development community. While new challenges
will undoubtedly arise, the long-term benefits of a more adaptable
and future-proof QEMU far outweigh the initial complexities.
