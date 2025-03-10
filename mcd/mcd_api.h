/*
 * Copyright (c) 2008, ARM Ltd., Infineon Technologies, NXP Semiconductors,
 * Lauterbach, STMicroelectronics and TIMA Laboratory.
 * All rights reserved.
 *
 * PREAMBLE
 *
 * The MCD API (Multi-Core Debug) has been designed as an interface between
 * software development tools and simulated or real systems with multi-core
 * SoCs. The target is to allow consistent software tooling throughout the
 * whole SoC development flow.
 * The MCD API (the "SOFTWARE") has been developed jointly by ARM Ltd.,
 * Infineon Technologies, NXP Semiconductors, Lauterbach,
 * STMicroelectronics and TIMA Laboratory as part of the SPRINT project
 * (www.sprint-project.net).
 * The SPRINT project has been funded by the European Commission.
 *
 * LICENSE
 *
 *  Any redistribution and use of the SOFTWARE in source and binary forms,
 *  with or without modification constitutes the full acceptance of the
 *  following disclaimer as well as of the license herein and is permitted
 *  provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the disclaimer detailed below.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the disclaimer detailed below in the
 *    documentation and/or other materials provided with the distribution.
 *  - Neither the name of its copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from the
 *    Software without specific prior written permission.
 *  - Modification of any or all of the source code, documentation and other
 *    materials provided under this license are subject to acknowledgement of
 *    the modification(s) by including a prominent notice on the modification(s)
 *    stating the change(s) to the file(s), identifying the date of such change
 *    and stating the name of the publisher of any such modification(s).
 *
 * DISCLAIMER OF WARRANTY AND LIABILITY
 *
 *  THE SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY, NON-INFRINGEMENT AND FITNESS FOR A
 *  PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 *  OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 *  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 *  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 *  OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *  WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE,
 *  MISREPRESENTATION OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * VERSION HISTORY
 *
 *  1.0 "SPRINT Release"     : SPRINT reference version
 *
 *  1.1 "Lauterbach Release" :
 *  - forces all boolean types to 8-bit on Linux and Mac-OS-X,
 *    but 32-bit on all other OS forces 32-bit enumeration types
 *  - additional memory spaces MCD_MEM_SPACE_IS_PHYSICAL,
 *    MCD_MEM_SPACE_IS_LOGICAL, MCD_MEM_SPACE_IS_AUX
 *  - changed type of 2nd argument of mcd_qry_input_handle_f from "int" to
 *    "uint32_t"
 *  - changed type of element "data" of of mcd_tx_st from "unsigned char" to
 *     "uint8_t"
 *  - specifying the calling convention for MS Windows (x86) to __cdecl
 *
 *  1.2 "QEMU Release"       :
 *  - changes formatting to accommodate QEMU's coding style guidelines
 *  - includes qemu/osdep.h instead of mcd_types.h
 */


/**
 * DOC: Using the MCD API
 *
 * The MCD API has been captured in a single header file and all API users have
 * to include this header file in their source code. The following has to be
 * noted when including this header file:
 *
 * - The MCD API extensively utilizes standard fixed size integer types as
 *   defined by ISO. These types can be found in the system library header named
 *   <stdint.h>. In case this file is not provided with the used platform and/or
 *   build environment, suitable definitions for the required types need to be
 *   created. In most cases it should be possible to find definitions of these
 *   platform-specific types on the internet.
 *
 * The MCD API is composed of two distinct parts:
 *
 * - An API in order to allow tools to access debug targets in a uniform way
 *   (ToolsAPI).
 * - An API in order to allow the MCD framework to access target components in a
 *   standard way (TargetAPI).
 *
 * The following naming conventions have been introduced for the definition of
 * the various data structures and function calls of the MCD API:
 *
 * - All data structures begin with the prefix 'mcd'. This stands for
 *   "Multi-Core Debugging".
 * - All data structures used by the API functions that are C Enumerations end
 *   in the suffix '_et'. This stands for "Enumeration Type". With version 1.1
 *   of the API all enumeration types are defined as 32-bit unsigned integers.
 * - All data structures used by the API functions that are C Structures end in
 *   the suffix '_st'. This stands for "Structure Type".
 * - All API function names begin with the prefix 'mcd'. This stands for
 *   "Multi-Core Debugging".
 * - All API function names end in the suffix '_f'. This stands for "Function".
 *
 * In addition to this, the following convention is assumed to be applied to all
 * implementations:
 *
 * - All strings are terminated by a zero character.
 * - All member of a structure must be aligned to their size E.g. members must
 *   of type uint64_t must be aligned to an offset divisible by 8.
 * - The calling convention for implementations on MS Windows for x86 CPUs is
 *   __cdecl
 *
 * If MCD API extensions are needed, it is strongly recommended to add them
 * outside of this header file for compatibility reasons. If this is not
 * possible it is mandatory to modify the MCD_API_VER_AUTHOR to a different
 * string than "SPRINT Release". New versions of the "SPRINT Release" may only
 * be created by the copyright holders listed in the license text.
 */


/*
 * mcd_api.h
 * The Multi-Core Debug (MCD) API defined as a part of the SPRINT Project
 * This is the definition of the Multi-Core Debug API as defined by SPRINT.
 */


#ifndef MCD_API_H
#define MCD_API_H

#include "qemu/osdep.h"


/**
 * DOC: Definitions of Constant
 *
 * This is a list of constant values as defined for the utilization by data
 * structures of the MCD API.
 */


/**
 * define MCD_API_VER_MAJOR - Major revision number of this API.
 */
#define MCD_API_VER_MAJOR 1

/**
 * define MCD_API_VER_MINOR - Minor revision number of this API.
 *
 * Version 1.2 only introduced formatting changes
 */
#define MCD_API_VER_MINOR 1

/**
 * define MCD_API_VER_AUTHOR - Author of this API
 *
 * Version 1.2 introduces MCD into QEMU
 * Version 1.1 extends 1.0 "SPRINT Release" by fixed types
 * Version 1.0 "SPRINT Release" is the SPRINT reference version
 */
#define MCD_API_VER_AUTHOR "QEMU Release"

/**
 * define MCD_API_VER_BUILD - Build revision number of this API.
 *
 * SVN revision not applicable to QEMU.
 */
#define MCD_API_VER_BUILD 0

/**
 * define MCD_HOSTNAME_LEN - Maximum length of the host's name which runs the
 *                           debug server (incl. terminating zero).
 */
#define MCD_HOSTNAME_LEN 64 /* Maximum length of the host's name */

/**
 * define MCD_REG_NAME_LEN - Maximum length of a register name
 *                            (incl. terminating zero).
 */
#define MCD_REG_NAME_LEN 32

/**
 * define MCD_MEM_SPACE_NAME_LEN - Maximum length of a memory space name
 *                                 (incl. terminating zero).
 */
#define MCD_MEM_SPACE_NAME_LEN 32

/**
 * define MCD_MEM_BLOCK_NAME_LEN - Maximum length of a memory block name
 *                                 (incl. terminating zero).
 */
#define MCD_MEM_BLOCK_NAME_LEN 32

/**
 * define MCD_MEM_BLOCK_NOPARENT - Parent ID to be assigned to a memory block
 *                                 at root level.
 */
#define MCD_MEM_BLOCK_NOPARENT 0

/**
 * define MCD_MEM_AUSIZE_NUM - Maximum number of supported Addressable Unit
 *                             sizes.
 */
#define MCD_MEM_AUSIZE_NUM 8

/**
 * define MCD_INFO_STR_LEN - Maximum length of an info string
 *                           (incl. terminating zero).
 */
#define MCD_INFO_STR_LEN 256

/**
 * define MCD_KEY_LEN - Maximum length of keys (incl. terminating zero).
 */
#define MCD_KEY_LEN 64

/**
 * define MCD_UNIQUE_NAME_LEN - Maximum length of a unique name string
 *                              (incl. terminating zero).
 */
#define MCD_UNIQUE_NAME_LEN 64

/**
 * define MCD_MAX_TRIGS - Maximum number of triggers supported per core.
 */
#define MCD_MAX_TRIGS 32

/**
 * define MCD_API_IMP_VENDOR_LEN - Maximum name length of the API implementation
 *                                 vendor (incl. terminating zero).
 */
#define MCD_API_IMP_VENDOR_LEN 32

/**
 * define MCD_CHL_NUM_MAX - Maximum number of supported communication channels.
 */
#define MCD_CHL_NUM_MAX 32

/**
 * define MCD_CHL_LOWEST_PRIORITY - Lowest channel priority
 *                                  [Range: 0 (highest) to 15 (lowest)].
 */
#define MCD_CHL_LOWEST_PRIORITY 15

/**
 * define MCD_TX_NUM_MAX - Maximum number of transactions supported per
 *                         transaction list.
 */
#define MCD_TX_NUM_MAX 64

/**
 * define MCD_GUARANTEED_MIN_PAYLOAD - Minimum payload guaranteed per
 *                                      transaction list (in bytes).
 */
#define MCD_GUARANTEED_MIN_PAYLOAD 16384

/**
 * define MCD_CORE_MODE_NAME_LEN - Maximum name length of a core mode, e.g.
 *                                 user, supervisor, secure
 *                                 (incl. terminating zero).
 */
#define MCD_CORE_MODE_NAME_LEN 32


/**
 * DOC: Definition of Enumerations
 *
 * This is a list of enumeration values as defined for the utilization
 * by data structures of the MCD API.
 */


/**
 * typedef mcd_return_et - Enumeration type defining the action a calling
 *                         function has to take after an MCD API function call.
 *
 * The calling function has to evaluate the return value of an MCD API function
 * call in order to check its success. If the function returned with an error an
 * appropriate action has to be taken as defined by the return value. All MCD
 * API functions return a value of type mcd_return_et. The calling function has
 * to decide the further proceeding based on it.
 *
 * A few return codes have been reserved for future API use and must not be
 * used. Any further value can be used for customized actions. All of these user
 * defined actions need to have values between %MCD_RET_ACT_CUSTOM_LO and
 * %MCD_RET_ACT_CUSTOM_HI.
 *
 * * %MCD_RET_ACT_NONE  - No special action required.
 * * %MCD_RET_ACT_AGAIN - Try to call the function again.
 * * %MCD_RET_ACT_HANDLE_EVENT - Handle the event or events.
 * * %MCD_RET_ACT_HANDLE_ERROR - Handle the error.
 * * %MCD_RET_ACT_RESERVED_LO - Begin Range: Action reserved for future API use.
 * * %MCD_RET_ACT_RESERVED_HI - End Range: Action reserved for future API use.
 * * %MCD_RET_ACT_CUSTOM_LO - Begin Range: For user defined actions.
 * * %MCD_RET_ACT_CUSTOM_HI - End Range: For user defined actions.
 */
typedef uint32_t mcd_return_et;
enum {
    MCD_RET_ACT_NONE         = 0x00000000,
    MCD_RET_ACT_AGAIN        = 0x00000001,
    MCD_RET_ACT_HANDLE_EVENT = 0x00000002,
    MCD_RET_ACT_HANDLE_ERROR = 0x00000003,
    MCD_RET_ACT_RESERVED_LO  = 0x00000004,
    MCD_RET_ACT_RESERVED_HI  = 0x00008000,
    MCD_RET_ACT_CUSTOM_LO    = 0x00010000,
    MCD_RET_ACT_CUSTOM_HI    = 0x40000000,
};


/**
 * typedef mcd_error_code_et - Enumeration type defining the detailed error
 *                             codes that can be returned by an MCD API function
 *                             call.
 *
 * The calling function has to evaluate the return value of an MCD API function
 * call in order to check its success. If the function returned with an error an
 * appropriate action has to be taken as defined by the return value. All MCD
 * API functions return a value of type mcd_return_et. If the returned value
 * indicates an error, the user has to retrieve the detailed information on the
 * occurred error by calling mcd_qry_error_info_f(). The following enumeration
 * is part of this information and describes the detailed error codes.
 *
 * The enumeration's values can be subdivided into the following categories:
 *
 * - GENERAL       (0x0000-0x0FFF)         : These errors can be returned by any
 *   MCD API function call.
 * - API_SPECIFIC  (0x1000-0x10000000)     : These errors are specific to
 *   certain MCD API function calls. Some of them may be valid for multiple MCD
 *   API function calls.
 * - CUSTOM        (0x10000000-0x7FFFFFFF) : These error codes can be defined by
 *   the user and carry user defined semantics.
 *
 * * %MCD_ERR_NONE - No error.
 * * %MCD_ERR_FN_UNIMPLEMENTED - Called function is not implemented.
 * * %MCD_ERR_USAGE - MCD API not correctly used.
 * * %MCD_ERR_PARAM - Passed invalid parameter.
 * * %MCD_ERR_CONNECTION - Server connection error.
 * * %MCD_ERR_TIMED_OUT - Function call timed out.
 * * %MCD_ERR_GENERAL - General error.
 * * %MCD_ERR_RESULT_TOO_LONG - String to return is longer than the provided
 *   character array.
 * * %MCD_ERR_COULD_NOT_START_SERVER - Could not start server.
 * * %MCD_ERR_SERVER_LOCKED - Server is locked.
 * * %MCD_ERR_NO_MEM_SPACES - No memory spaces defined.
 * * %MCD_ERR_NO_MEM_BLOCKS - No memory blocks defined for the requested memory
 *   space.
 * * %MCD_ERR_MEM_SPACE_ID - No memory space with requested ID exists.
 * * %MCD_ERR_NO_REG_GROUPS - No register groups defined.
 * * %MCD_ERR_REG_GROUP_ID - No register group with requested ID exists.
 * * %MCD_ERR_REG_NOT_COMPOUND - Register is not a compound register.
 * * %MCD_ERR_OVERLAYS - Error retrieving overlay information.
 * * %MCD_ERR_DEVICE_ACCESS - Cannot access device (power-down, reset active,
 *   etc.).
 * * %MCD_ERR_DEVICE_LOCKED - Device is locked.
 * * %MCD_ERR_TXLIST_READ - Read transaction of transaction list has failed.
 * * %MCD_ERR_TXLIST_WRITE - Write transaction of transaction list has failed.
 * * %MCD_ERR_TXLIST_TX - Other error (no R/W failure) for a transaction of the
 *   transaction list.
 * * %MCD_ERR_CHL_TYPE_NOT_SUPPORTED - Requested channel type is not supported
 *   by the implementation.
 * * %MCD_ERR_CHL_TARGET_NOT_SUPPORTED - Addressed target does not support
 *   communication channels.
 * * %MCD_ERR_CHL_SETUP - Channel setup is invalid or contains unsupported
 *   attributes.
 * * %MCD_ERR_CHL_MESSAGE_FAILED - Sending or receiving of the last message has
 *   failed.
 * * %MCD_ERR_TRIG_CREATE - Trigger could not be created.
 * * %MCD_ERR_TRIG_ACCESS - Error during trigger information access.
 * * %MCD_ERR_CUSTOM_LO - Begin Range: For user defined errors.
 * * %MCD_ERR_CUSTOM_HI - End Range: For user defined errors.
 */
typedef uint32_t mcd_error_code_et;
enum {

    MCD_ERR_NONE                        = 0,

    MCD_ERR_FN_UNIMPLEMENTED            = 0x0100,
    MCD_ERR_USAGE                       = 0x0101,
    MCD_ERR_PARAM                       = 0x0102,
    MCD_ERR_CONNECTION                  = 0x0200,
    MCD_ERR_TIMED_OUT                   = 0x0201,

    MCD_ERR_GENERAL                     = 0x0F00,

    MCD_ERR_RESULT_TOO_LONG             = 0x1000,

    MCD_ERR_COULD_NOT_START_SERVER      = 0x1100,
    MCD_ERR_SERVER_LOCKED               = 0x1101,

    MCD_ERR_NO_MEM_SPACES               = 0x1401,
    MCD_ERR_NO_MEM_BLOCKS               = 0x1402,
    MCD_ERR_MEM_SPACE_ID                = 0x1410,
    MCD_ERR_NO_REG_GROUPS               = 0x1440,
    MCD_ERR_REG_GROUP_ID                = 0x1441,
    MCD_ERR_REG_NOT_COMPOUND            = 0x1442,

    MCD_ERR_OVERLAYS                    = 0x1500,

    MCD_ERR_DEVICE_ACCESS               = 0x1900,
    MCD_ERR_DEVICE_LOCKED               = 0x1901,

    MCD_ERR_TXLIST_READ                 = 0x2100,
    MCD_ERR_TXLIST_WRITE                = 0x2101,
    MCD_ERR_TXLIST_TX                   = 0x2102,

    MCD_ERR_CHL_TYPE_NOT_SUPPORTED      = 0x3100,
    MCD_ERR_CHL_TARGET_NOT_SUPPORTED    = 0x3101,
    MCD_ERR_CHL_SETUP                   = 0x3102,
    MCD_ERR_CHL_MESSAGE_FAILED          = 0x3140,

    MCD_ERR_TRIG_CREATE                 = 0x3200,
    MCD_ERR_TRIG_ACCESS                 = 0x3201,

    MCD_ERR_CUSTOM_LO                   = 0x10000000,
    MCD_ERR_CUSTOM_HI                   = 0x7FFFFFFF,
};


/**
 * typedef mcd_error_event_et - Enumeration type defining the error events that
 *                              can be returned by an MCD API function call.
 *
 * The calling function has to evaluate the return value of an MCD API function
 * call in order to check its success. If the function returned with an error an
 * appropriate action has to be taken as defined by the return value. All MCD
 * API functions return a value of type mcd_return_et. If the returned value
 * indicates an event, the user has to retrieve the detailed information on the
 * occurred error by calling mcd_qry_error_info_f(). The following enumeration
 * is part of this information and describes the detailed event codes.
 *
 * Event codes are bitwise exclusive. This allows 32 different event codes. User
 * defined event codes need to have values between %MCD_ERR_EVT_CUSTOM_LO and
 * %MCD_ERR_EVT_CUSTOM_HI. Reserved error event codes must not be used.
 *
 * * %MCD_ERR_EVT_NONE  - No action required due to an event.
 * * %MCD_ERR_EVT_RESET - Target has been reset.
 * * %MCD_ERR_EVT_PWRDN - Target has been a powered down.
 * * %MCD_ERR_EVT_HWFAILURE - There has been a target hardware failure.
 * * %MCD_ERR_EVT_RESERVED_LO - Begin Range: Events reserved for future API use.
 * * %MCD_ERR_EVT_RESERVED_HI - End Range: Events reserved for future API use.
 * * %MCD_ERR_EVT_CUSTOM_LO - Begin Range: User defined events.
 * * %MCD_ERR_EVT_CUSTOM_HI - End Range: User defined events.
 */
typedef uint32_t mcd_error_event_et;
enum {
    MCD_ERR_EVT_NONE         = 0x00000000,
    MCD_ERR_EVT_RESET        = 0x00000001,
    MCD_ERR_EVT_PWRDN        = 0x00000002,
    MCD_ERR_EVT_HWFAILURE    = 0x00000004,
    MCD_ERR_EVT_RESERVED_LO  = 0x00000008,
    MCD_ERR_EVT_RESERVED_HI  = 0x00008000,
    MCD_ERR_EVT_CUSTOM_LO    = 0x00010000,
    MCD_ERR_EVT_CUSTOM_HI    = 0x40000000,
};


/**
 * typedef mcd_addr_space_type_et - Enumeration type defining the type of an
 *                                   address space ID.
 *
 * The type of the address space ID defines the interpretation of an address
 * space ID. This type refers to the addr_space_id member of mcd_addr_st which
 * is used to further extend the address information.
 *
 * * %MCD_NOTUSED_ID - Address space ID is not used.
 * * %MCD_OVERLAY_ID - Address space ID represents the memory overlay the
 *   address is valid in.
 * * %MCD_MEMBANK_ID - Address space ID represents the memory bank the address
 *   is valid in.
 * * %MCD_PROCESS_ID - Address space ID represents the process the address is
 *   valid in.
 * * %MCD_HW_THREAD_ID - Address space ID represents the hardware thread the
 *   address is valid in.
 */
typedef uint32_t mcd_addr_space_type_et;
enum {

    MCD_NOTUSED_ID    = 0,
    MCD_OVERLAY_ID    = 1,
    MCD_MEMBANK_ID    = 2,
    MCD_PROCESS_ID    = 3,
    MCD_HW_THREAD_ID  = 4,
};


/**
 * typedef mcd_mem_type_et - Enumeration type defining the type of a memory
 *                            space.
 *
 * Different types of memory spaces are possible. This enumeration type
 * describes them. The type values %MCD_MEM_SPACE_IS_REGISTERS,
 * %MCD_MEM_SPACE_IS_PROGRAM, %MCD_MEM_SPACE_IS_VIRTUAL and
 * %MCD_MEM_SPACE_IS_CACHE are bitwise mutually exclusive.
 * %MCD_MEM_SPACE_IS_PHYSICAL or %MCD_MEM_SPACE_IS_LOGICAL should be set when
 * the target contains a memory management unit (MMU) that translates memory
 * addresses between core and memory. User defined memory space types need to
 * have values between %MCD_MEMSPACE_CUSTOM_HI and %MCD_MEMSPACE_CUSTOM_HI.
 * Reserved memory space types must not be used.
 *
 * * %MCD_MEM_SPACE_DEFAULT - The memory space is of none of the types below.
 * * %MCD_MEM_SPACE_IS_REGISTERS - The memory space contains only registers.
 * * %MCD_MEM_SPACE_IS_PROGRAM - The memory space is a program memory.
 * * %MCD_MEM_SPACE_IS_VIRTUAL - The memory space is virtual (resource not
 *   existing in target).
 * * %MCD_MEM_SPACE_IS_CACHE - The memory space is a cache.
 * * %MCD_MEM_SPACE_IS_PHYSICAL - The memory space is physical memory (not
 *   translated by MMU).
 * * %MCD_MEM_SPACE_IS_LOGICAL - The memory space is logical memory (translated
 *   by MMU).
 * * %MCD_MEM_SPACE_RESERVED_LO - Begin Range: Reserved for future API use.
 * * %MCD_MEM_SPACE_RESERVED_HI - End Range: Reserved for future API use.
 * * %MCD_MEM_SPACE_CUSTOM_LO - Begin Range: User defined memory types.
 * * %MCD_MEM_SPACE_CUSTOM_HI - End Range: User defined memory types.
 */
typedef uint32_t mcd_mem_type_et;
enum {
    MCD_MEM_SPACE_DEFAULT      = 0x00000000,
    MCD_MEM_SPACE_IS_REGISTERS = 0x00000001,
    MCD_MEM_SPACE_IS_PROGRAM   = 0x00000002,
    MCD_MEM_SPACE_IS_VIRTUAL   = 0x00000004,
    MCD_MEM_SPACE_IS_CACHE     = 0x00000008,
    MCD_MEM_SPACE_IS_PHYSICAL  = 0x00000010,
    MCD_MEM_SPACE_IS_LOGICAL   = 0x00000020,
    MCD_MEM_SPACE_RESERVED_LO  = 0x00000040,
    MCD_MEM_SPACE_RESERVED_HI  = 0x00008000,
    MCD_MEM_SPACE_CUSTOM_LO    = 0x00010000,
    MCD_MEM_SPACE_CUSTOM_HI    = 0x40000000,
};


/**
 * typedef mcd_endian_et - Enumeration type defining the endianness of a memory
 *                          space or a memory block.
 *
 * The endianness of a memory can be either Little Endian or Big Endian. This
 * enumeration type describes the two possible values of endianness and is used
 * to set the corresponding property of a memory space and a memory block
 * description. If memory blocks are supported, the value of a memory block
 * overrides the one for the memory space it is part of.
 *
 * * %MCD_ENDIAN_DEFAULT - Endianness as defined by the target architecture or
 *   parent module (if available).
 * * %MCD_ENDIAN_LITTLE - Little Endian data representation.
 * * %MCD_ENDIAN_BIG - Big Endian data representation.
 */
typedef uint32_t mcd_endian_et;
enum {
    MCD_ENDIAN_DEFAULT =  0,
    MCD_ENDIAN_LITTLE  =  1,
    MCD_ENDIAN_BIG     =  2,
};


/**
 * typedef mcd_reg_type_et - Enumeration type defining the allowed register
 *                            types.
 *
 * A register can be a simple register, a compound register or a partial
 * register. This enumeration type describes the three register types.
 *
 * * %MCD_REG_TYPE_SIMPLE - Simple register.
 * * %MCD_REG_TYPE_COMPOUND - Compound register composed more than one simple
 *   register.
 * * %MCD_REG_TYPE_PARTIAL - Register that is part of a simple register.
 */
typedef uint32_t mcd_reg_type_et;
enum {
    MCD_REG_TYPE_SIMPLE   = 0,
    MCD_REG_TYPE_COMPOUND = 1,
    MCD_REG_TYPE_PARTIAL  = 2,
};


/**
 * typedef mcd_trig_type_et - Enumeration type defining the type of a trigger.
 *
 * This enumeration type describes the possible types of triggers for the
 * target. The type values are bitwise mutually exclusive and a member of type
 * mcd_trig_type_et may be a combination of several of them. The type
 * %MCD_TRIG_TYPE_CUSTOM refers to a custom trigger (not a custom trigger type)
 * using the standard format as defined by mcd_trig_custom_st. User defined
 * trigger types need to have values between %MCD_TRIG_TYPE_CUSTOM_LO and
 * %MCD_TRIG_TYPE_CUSTOM_HI.
 *
 * * %MCD_TRIG_TYPE_UNDEFINED - Undefined trigger type.
 * * %MCD_TRIG_TYPE_IP - Trigger on a changing instruction pointer.
 * * %MCD_TRIG_TYPE_READ - Trigger on a read data access to a specific address
 *   or address range.
 * * %MCD_TRIG_TYPE_WRITE - Trigger on a write data access to a specific address
 *   or address range.
 * * %MCD_TRIG_TYPE_RW - Trigger on a read or a write data access to a specific
 *   address or address range.
 * * %MCD_TRIG_TYPE_NOCYCLE - Trigger on core information other than an IP or
 *   data compare trigger.
 * * %MCD_TRIG_TYPE_TRIG_BUS - Trigger on a trigger bus combination.
 * * %MCD_TRIG_TYPE_COUNTER - Trigger on an elapsed trigger counter.
 * * %MCD_TRIG_TYPE_CUSTOM - Custom trigger using standard format as defined by
 *   mcd_trig_custom_st.
 * * %MCD_TRIG_TYPE_CUSTOM_LO - Begin Range: User defined trigger types.
 * * %MCD_TRIG_TYPE_CUSTOM_HI - End Range: User defined trigger types.
 */
typedef uint32_t mcd_trig_type_et;
enum {
    MCD_TRIG_TYPE_UNDEFINED = 0x00000000,
    MCD_TRIG_TYPE_IP        = 0x00000001,
    MCD_TRIG_TYPE_READ      = 0x00000002,
    MCD_TRIG_TYPE_WRITE     = 0x00000004,
    MCD_TRIG_TYPE_RW        = 0x00000008,
    MCD_TRIG_TYPE_NOCYCLE   = 0x00000010,
    MCD_TRIG_TYPE_TRIG_BUS  = 0x00000020,
    MCD_TRIG_TYPE_COUNTER   = 0x00000040,
    MCD_TRIG_TYPE_CUSTOM    = 0x00000080,
    MCD_TRIG_TYPE_CUSTOM_LO = 0x00010000,
    MCD_TRIG_TYPE_CUSTOM_HI = 0x40000000,
};


/**
 * typedef mcd_trig_opt_et - Enumeration type defining additional options for a
 *                           trigger.
 *
 * This enumeration type describes the additionally possible options for
 * triggers in a target. The type values are bitwise mutually exclusive and a
 * member of type mcd_trig_opt_et may be a combination of several of them. User
 * defined trigger options need to have values between %MCD_TRIG_OPT_CUSTOM_LO
 * and %MCD_TRIG_OPT_CUSTOM_HI.
 *
 * * %MCD_TRIG_OPT_DEFAULT - Default trigger options, e.g. chosen by the
 *   platform.
 * * %MCD_TRIG_OPT_IMPL_HARDWARE - The trigger shall be implemented by hardware.
 * * %MCD_TRIG_OPT_IMPL_SOFTWARE - The trigger shall be implemented by software
 *   (code substitution).
 * * %MCD_TRIG_OPT_OUT_OF_RANGE - The trigger is activated when a data access is
 *   performed outside the specified range.
 * * %MCD_TRIG_OPT_DATA_IS_CONDITION - The value of a data access is part of the
 *   trigger condition.
 * * %MCD_TRIG_OPT_DATASIZE_IS_CONDITION - The size of a data access is part of
 *   the trigger condition.
 * * %MCD_TRIG_OPT_NOT_DATA - The data comparison done in a trigger condition is
 *   negated.
 * * %MCD_TRIG_OPT_SIGNED_DATA - The data values are considered as signed for
 *   the trigger condition. This usually requires the setting of
 *   %MCD_TRIG_OPT_DATASIZE_IS_CONDITION.
 * * %MCD_TRIG_OPT_HW_THREAD_IS_CONDITION - The hardware thread ID is part of
 *   the trigger condition.
 * * %MCD_TRIG_OPT_NOT_HW_THREAD - The comparison of the hardware thread ID is
 *   negated.
 * * %MCD_TRIG_OPT_SW_THREAD_IS_CONDITION - The software thread ID is part of
 *   the trigger condition.
 * * %MCD_TRIG_OPT_NOT_SW_THREAD - The comparison of the software thread ID is
 *   negated.
 * * %MCD_TRIG_OPT_DATA_MUST_CHANGE - The data value of the cycle must change
 *   the value of the target location. This applies only to triggers on write
 *   cycles. The data_mask field defines which bits are considered for the
 *   comparison.
 * * %MCD_TRIG_OPT_CORE_MODE_IS_CONDITION - The core mode as defined by the
 *   member core_mode_mask of a mcd_trig_complex_core_st is part of the trigger
 *   condition. Each set bit prevents the related core mode from activating the
 *   trigger.
 * * %MCD_TRIG_OPT_STATE_IS_CONDITION - The state of the trigger set's state
 *   machine is part of the trigger condition.
 * * %MCD_TRIG_OPT_NOT - The trigger condition is negated, i.e. action is taken
 *   if the whole trigger condition is NOT met. This should not be mixed up with
 *   %MCD_TRIG_OPT_OUT_OF_RANGE which inverts just the address range.
 * * %MCD_TRIG_OPT_CUSTOM_LO - Begin Range: User defined trigger options.
 * * %MCD_TRIG_OPT_CUSTOM_HI - End Range: User defined trigger options.
 */
typedef uint32_t mcd_trig_opt_et;
enum {
    MCD_TRIG_OPT_DEFAULT                = 0x00000000,
    MCD_TRIG_OPT_IMPL_HARDWARE          = 0x00000001,
    MCD_TRIG_OPT_IMPL_SOFTWARE          = 0x00000002,
    MCD_TRIG_OPT_OUT_OF_RANGE           = 0x00000004,
    MCD_TRIG_OPT_DATA_IS_CONDITION      = 0x00000008,
    MCD_TRIG_OPT_DATASIZE_IS_CONDITION  = 0x00000010,
    MCD_TRIG_OPT_NOT_DATA               = 0x00000020,
    MCD_TRIG_OPT_SIGNED_DATA            = 0x00000040,
    MCD_TRIG_OPT_HW_THREAD_IS_CONDITION = 0x00000080,
    MCD_TRIG_OPT_NOT_HW_THREAD          = 0x00000100,
    MCD_TRIG_OPT_SW_THREAD_IS_CONDITION = 0x00000200,
    MCD_TRIG_OPT_NOT_SW_THREAD          = 0x00000400,
    MCD_TRIG_OPT_DATA_MUST_CHANGE       = 0x00000800,
    MCD_TRIG_OPT_CORE_MODE_IS_CONDITION = 0x00020000,
    MCD_TRIG_OPT_STATE_IS_CONDITION     = 0x00040000,
    MCD_TRIG_OPT_NOT                    = 0x00080000,
    MCD_TRIG_OPT_CUSTOM_LO              = 0x00100000,
    MCD_TRIG_OPT_CUSTOM_HI              = 0x40000000,
};


/**
 * typedef mcd_trig_action_et - Enumeration type defining the trigger action
 *                              types.
 *
 * This enumeration type describes the possible actions for triggers in a
 * target. The type values are bitwise mutually exclusive and a member of type
 * mcd_trig_action_et may be a combination of several of them. User defined
 * trigger actions need to have values between %MCD_TRIG_ACTION_CUSTOM_LO and
 * %MCD_TRIG_ACTION_CUSTOM_HI.
 *
 * * %MCD_TRIG_ACTION_DEFAULT - No action has to be taken except from setting
 *   the trigger to be captured.
 * * %MCD_TRIG_ACTION_DBG_DEBUG - Stop this core and bring it into debug mode.
 * * %MCD_TRIG_ACTION_DBG_GLOBAL - Stop all cores and bring them into debug
 *   mode.
 * * %MCD_TRIG_ACTION_DBG_MONITOR - Issue an exception (monitor interrupt) on
 *   this core in order to execute the monitor code.
 * * %MCD_TRIG_ACTION_TRIG_BUS_EVENT - Signal the according event on the trigger
 *   bus (for the duration of one core cycle). The corresponding bitmask is
 *   specified by the member action_param of the used trigger data structure.
 * * %MCD_TRIG_ACTION_TRIG_BUS_SET - Set bits on the trigger bus. The
 *   corresponding bitmask is specified by the member action_param of the used
 *   trigger data structure.
 * * %MCD_TRIG_ACTION_TRIG_BUS_CLEAR - Clear bits on the trigger bus. The
 *   corresponding bitmask is specified by the member action_param of the used
 *   trigger data structure.
 * * %MCD_TRIG_ACTION_TRACE_QUALIFY - Trace this cycle.
 * * %MCD_TRIG_ACTION_TRACE_QUALIFY_PROGRAM - Trace this cycle, affects program
 *   trace only.
 * * %MCD_TRIG_ACTION_TRACE_QUALIFY_DATA - Trace this cycle, affects data trace
 *   only.
 * * %MCD_TRIG_ACTION_TRACE_START - Start tracing.
 * * %MCD_TRIG_ACTION_TRACE_STOP - Stop tracing.
 * * %MCD_TRIG_ACTION_TRACE_TRIGGER - Trigger trace unit.
 * * %MCD_TRIG_ACTION_ANA_START_PERFM - Start performance analysis or profiling.
 * * %MCD_TRIG_ACTION_ANA_STOP_PERFM - Stop performance analysis or profiling.
 * * %MCD_TRIG_ACTION_STATE_CHANGE - Set the trigger set's state machine to a
 *   new state. The corresponding state is specified by the member action_param
 *   of the used trigger data structure.
 * * %MCD_TRIG_ACTION_COUNT_QUALIFY - Increment the counter specified by the
 *   member action_param of the used trigger data structure.
 * * %MCD_TRIG_ACTION_COUNT_START - Start the counter specified by the member
 *   action_param of the used trigger data structure.
 * * %MCD_TRIG_ACTION_COUNT_STOP - Stop the counter specified by the member
 *   action_param of the used trigger data structure.
 * * %MCD_TRIG_ACTION_COUNT_RESTART - Restart the counter specified by the
 *   member action_param of the used trigger data structure.
 * * %MCD_TRIG_ACTION_CUSTOM_LO - Begin Range: User defined trigger actions.
 * * %MCD_TRIG_ACTION_CUSTOM_HI - End Range: User defined trigger actions.
 */
typedef uint32_t mcd_trig_action_et;
enum {
    MCD_TRIG_ACTION_DEFAULT               = 0x00000000,
    MCD_TRIG_ACTION_DBG_DEBUG             = 0x00000001,
    MCD_TRIG_ACTION_DBG_GLOBAL            = 0x00000002,
    MCD_TRIG_ACTION_DBG_MONITOR           = 0x00000004,
    MCD_TRIG_ACTION_TRIG_BUS_EVENT        = 0x00000010,
    MCD_TRIG_ACTION_TRIG_BUS_SET          = 0x00000020,
    MCD_TRIG_ACTION_TRIG_BUS_CLEAR        = 0x00000040,
    MCD_TRIG_ACTION_TRACE_QUALIFY         = 0x00000100,
    MCD_TRIG_ACTION_TRACE_QUALIFY_PROGRAM = 0x00000200,
    MCD_TRIG_ACTION_TRACE_QUALIFY_DATA    = 0x00000400,
    MCD_TRIG_ACTION_TRACE_START           = 0x00000800,
    MCD_TRIG_ACTION_TRACE_STOP            = 0x00001000,
    MCD_TRIG_ACTION_TRACE_TRIGGER         = 0x00002000,
    MCD_TRIG_ACTION_ANA_START_PERFM       = 0x00010000,
    MCD_TRIG_ACTION_ANA_STOP_PERFM        = 0x00020000,
    MCD_TRIG_ACTION_STATE_CHANGE          = 0x00040000,
    MCD_TRIG_ACTION_COUNT_QUALIFY         = 0x00080000,
    MCD_TRIG_ACTION_COUNT_START           = 0x00100000,
    MCD_TRIG_ACTION_COUNT_STOP            = 0x00200000,
    MCD_TRIG_ACTION_COUNT_RESTART         = 0x00400000,
    MCD_TRIG_ACTION_CUSTOM_LO             = 0x01000000,
    MCD_TRIG_ACTION_CUSTOM_HI             = 0x40000000,
};


/**
 * typedef mcd_tx_access_type_et - Enumeration type defining access types for
 *                                 transactions of transaction lists.
 *
 * This enumeration type describes the four possible access types for
 * transaction of a transaction list.
 *
 * * %MCD_TX_AT_R - Read access transaction.
 * * %MCD_TX_AT_W - Write access transaction.
 * * %MCD_TX_AT_RW - Read then write access transaction (atomic swap).
 * * %MCD_TX_AT_WR - Write then read access transaction (write and verify).
 */
typedef uint32_t mcd_tx_access_type_et;
enum {
    MCD_TX_AT_R    = 0x00000001,
    MCD_TX_AT_W    = 0x00000002,
    MCD_TX_AT_RW   = 0x00000003,
    MCD_TX_AT_WR   = 0x00000004,
};


/**
 * typedef mcd_tx_access_opt_et - Enumeration type defining access options for
 *                                transactions of transaction lists.
 *
 * This enumeration type describes the possible access options for transactions
 * of a transaction list. The type values are bitwise mutually exclusive and a
 * member of type mcd_tx_access_opt_et may be a combination of several of them.
 * User defined access options need to have values between %MCD_TX_OPT_CUSTOM_LO
 * and %MCD_TX_OPT_CUSTOM_HI. Reserved access options must not be used.
 *
 * Marking the last transaction of a transaction list with
 * %MCD_TX_OPT_ATOMIC_WITH_NEXT causes it to be atomic with the first
 * transaction of the next list to be executed for this core connection.
 *
 * * %MCD_TX_OPT_DEFAULT - MCD implementation decides on applied access options.
 * * %MCD_TX_OPT_SIDE_EFFECTS - Trigger side effects for the access.
 * * %MCD_TX_OPT_NO_SIDE_EFFECTS - Omit side effects for the access.
 * * %MCD_TX_OPT_BURST_ACCESSES - Perform burst accesses if possible.
 * * %MCD_TX_OPT_NO_BURST_ACCESSES - Avoid burst accesses if possible.
 * * %MCD_TX_OPT_ALTERNATE_PATH - Dual port or DAP memory access.
 * * %MCD_TX_OPT_PRIORITY_ACCESS - High priority access.
 * * %MCD_TX_OPT_DCACHE_WRITE_THRU - Force D-cache and unified caches to be
 *   write-through.
 * * %MCD_TX_OPT_CACHE_BYPASS - Bypass caches and read/write directly to the
 *   memory.
 * * %MCD_TX_OPT_NOINCREMENT - Do not increment address after each cycle. Useful
 *   for reading or writing to FIFOs.
 * * %MCD_TX_OPT_ATOMIC_WITH_NEXT - Transaction is executed atomic with the next
 *   one.
 * * %MCD_TX_OPT_RESERVED_LO - Begin Range: Reserved for future API use.
 * * %MCD_TX_OPT_RESERVED_HI - End Range: Reserved for future API use.
 * * %MCD_TX_OPT_CUSTOM_LO - Begin Range: User defined access options.
 * * %MCD_TX_OPT_CUSTOM_HI - End Range: User defined access options.
 */
typedef uint32_t mcd_tx_access_opt_et;
enum {
    MCD_TX_OPT_DEFAULT            = 0x00000000,
    MCD_TX_OPT_SIDE_EFFECTS       = 0x00000001,
    MCD_TX_OPT_NO_SIDE_EFFECTS    = 0x00000002,
    MCD_TX_OPT_BURST_ACCESSES     = 0x00000004,
    MCD_TX_OPT_NO_BURST_ACCESSES  = 0x00000008,
    MCD_TX_OPT_ALTERNATE_PATH     = 0x00000010,
    MCD_TX_OPT_PRIORITY_ACCESS    = 0x00000020,
    MCD_TX_OPT_DCACHE_WRITE_THRU  = 0x00000040,
    MCD_TX_OPT_CACHE_BYPASS       = 0x00000080,
    MCD_TX_OPT_NOINCREMENT        = 0x00000100,
    MCD_TX_OPT_ATOMIC_WITH_NEXT   = 0x00000200,
    MCD_TX_OPT_RESERVED_LO        = 0x00000400,
    MCD_TX_OPT_RESERVED_HI        = 0x00008000,
    MCD_TX_OPT_CUSTOM_LO          = 0x00010000,
    MCD_TX_OPT_CUSTOM_HI          = 0x40000000,
};


/**
 * typedef mcd_core_step_type_et - Enumeration type defining step types for a
 *                                 target core.
 *
 * This enumeration type describes the possible step types for a target core.
 * The step type depends on the core type. A programmable core can be for
 * example stepped in terms of cycles or instructions. User defined step types
 * need to have values between %MCD_CORE_STEP_TYPE_CUSTOM_LO and
 * %MCD_CORE_STEP_TYPE_CUSTOM_HI. They for example can be based on
 * specifications provided by the IP developer of a core. The step type values
 * %MCD_CORE_STEP_TYPE_RESERVED_LO to %MCD_CORE_STEP_TYPE_RESERVED_HI are
 * reserved for future API extensions and must not be used.
 *
 * * %MCD_CORE_STEP_TYPE_CYCLES - Step the core for core specific cycles.
 * * %MCD_CORE_STEP_TYPE_INSTR - Step the core for core specific instructions.
 * * %MCD_CORE_STEP_TYPE_RESERVED_LO - Begin Range: Reserved for future API use.
 * * %MCD_CORE_STEP_TYPE_RESERVED_HI - End Range: Reserved for future API use.
 * * %MCD_CORE_STEP_TYPE_CUSTOM_LO - Begin Range: User defined step types.
 * * %MCD_CORE_STEP_TYPE_CUSTOM_HI - End Range: User defined step types.
 * * %MCD_CORE_STEP_TYPE_MAX_TYPES - Maximum number of supported step types.
 */
typedef uint32_t mcd_core_step_type_et;
enum {
    MCD_CORE_STEP_TYPE_CYCLES      = 0x00000001,
    MCD_CORE_STEP_TYPE_INSTR       = 0x00000002,
    MCD_CORE_STEP_TYPE_RESERVED_LO = 0x00000004,
    MCD_CORE_STEP_TYPE_RESERVED_HI = 0x000000FF,
    MCD_CORE_STEP_TYPE_CUSTOM_LO   = 0x00000100,
    MCD_CORE_STEP_TYPE_CUSTOM_HI   = 0x00000F00,
    MCD_CORE_STEP_TYPE_MAX_TYPES   = 0x7FFFFFFF
};


/**
 * typedef mcd_core_state_et - Enumeration type defining the execution states of
 *                             a target core.
 *
 * This enumeration type describes the possible execution states of a target
 * core from a debugger perspective. The HALTED state is defined to differ from
 * the DEBUG state by the fact that a core in debug mode is under debugger
 * control. In contrast to this a core in HALTED state is not under the
 * execution control of the debugger but in a state from which the debugger can
 * only push it to DEBUG state. The same applies to the RUNNING state. User
 * defined core states need to have values between %MCD_CORE_STATE_CUSTOM_LO and
 * %MCD_CORE_STATE_CUSTOM_HI.
 *
 * * %MCD_CORE_STATE_UNKNOWN - Target core state is unknown.
 * * %MCD_CORE_STATE_RUNNING - Target core is running.
 * * %MCD_CORE_STATE_HALTED - Target core is halted.
 * * %MCD_CORE_STATE_DEBUG - Target core is in debug mode.
 * * %MCD_CORE_STATE_CUSTOM_LO - Begin Range: User defined core states.
 * * %MCD_CORE_STATE_CUSTOM_HI - End Range: User defined core states.
 * * %MCD_CORE_STATE_MAX_STATES - Maximum number of supported core states.
 */
typedef uint32_t mcd_core_state_et;
enum {
    MCD_CORE_STATE_UNKNOWN    = 0x00000000,
    MCD_CORE_STATE_RUNNING    = 0x00000001,
    MCD_CORE_STATE_HALTED     = 0x00000002,
    MCD_CORE_STATE_DEBUG      = 0x00000003,
    MCD_CORE_STATE_CUSTOM_LO  = 0x00000100,
    MCD_CORE_STATE_CUSTOM_HI  = 0x00000800,
    MCD_CORE_STATE_MAX_STATES = 0x7FFFFFFF
};


/**
 * typedef mcd_core_event_et - Enumeration type defining the possible events for
 *                             a target core.
 *
 * This enumeration type describes the possible core events for a target core
 * from a debugger perspective. These allow to optimize the polling of specific
 * target information and to support multiple clients connected to one target
 * core. Some core events may be reported just once. User defined core events
 * need to have values between %MCD_CORE_EVENT_CUSTOM_LO and
 * %MCD_CORE_EVENT_CUSTOM_HI.
 *
 * * %MCD_CORE_EVENT_NONE - No since the last poll.
 * * %MCD_CORE_EVENT_MEMORY_CHANGE - Memory content has changed.
 * * %MCD_CORE_EVENT_REGISTER_CHANGE - Register contents have changed.
 * * %MCD_CORE_EVENT_TRACE_CHANGE - Trace contents or states have changed.
 * * %MCD_CORE_EVENT_TRIGGER_CHANGE - Triggers or trigger states have changed.
 * * %MCD_CORE_EVENT_STOPPED - Target was stopped at least once since the last
 *   poll, it may already be running again.
 * * %MCD_CORE_EVENT_CHL_PENDING - A target communication channel request from
 *   the target is pending.
 * * %MCD_CORE_EVENT_CUSTOM_LO - Begin Range: User defined core events.
 * * %MCD_CORE_EVENT_CUSTOM_HI - End Range: User defined core events.
 */
typedef uint32_t mcd_core_event_et;
enum {
    MCD_CORE_EVENT_NONE            = 0x00000000,
    MCD_CORE_EVENT_MEMORY_CHANGE   = 0x00000001,
    MCD_CORE_EVENT_REGISTER_CHANGE = 0x00000002,
    MCD_CORE_EVENT_TRACE_CHANGE    = 0x00000004,
    MCD_CORE_EVENT_TRIGGER_CHANGE  = 0x00000008,
    MCD_CORE_EVENT_STOPPED         = 0x00000010,
    MCD_CORE_EVENT_CHL_PENDING     = 0x00000020,
    MCD_CORE_EVENT_CUSTOM_LO       = 0x00010000,
    MCD_CORE_EVENT_CUSTOM_HI       = 0x40000000,
};


/**
 * typedef mcd_chl_type_et - Enumeration type defining the communication channel
 *                           types.
 *
 * There can be different types of communication channels between a host side
 * tool and the target. This enumeration describes these possible types of
 * communication channels. User defined communication channel types need to have
 * values between %MCD_CHL_TYPE_CUSTOM_LO and %MCD_CHL_TYPE_CUSTOM_HI.
 *
 * * %MCD_CHL_TYPE_COMMON - Common communication channel to the target.
 * * %MCD_CHL_TYPE_CONFIG - Communication channel for configuration purposes,
 *   e.g. to configure the analysis setup.
 * * %MCD_CHL_TYPE_APPLI - Communication channel to an application running on
 *   the target, e.g. for semi-hosting purposes.
 * * %MCD_CHL_TYPE_CUSTOM_LO - Begin Range: User defined communication channel
 *   types.
 * * %MCD_CHL_TYPE_CUSTOM_HI - End Range: User defined communication channel
 *   types.
 */
typedef uint32_t mcd_chl_type_et;
enum {
    MCD_CHL_TYPE_COMMON    = 0x00000001,
    MCD_CHL_TYPE_CONFIG    = 0x00000002,
    MCD_CHL_TYPE_APPLI     = 0x00000003,
    MCD_CHL_TYPE_CUSTOM_LO = 0x00000100,
    MCD_CHL_TYPE_CUSTOM_HI = 0x00000F00,
};


/**
 * typedef mcd_chl_attributes_et - Enumeration type defining communication
 *                                  channel attributes.
 *
 * A communication channel can be defined with several attributes concerning the
 * channel's direction, accessibility and priority. This enumeration type
 * describes them. The type values are bitwise mutually exclusive and a member
 * of type mcd_chl_attributes_et may be a combination of several of them.
 *
 * * %MCD_CHL_AT_RCV - Receive channel.
 * * %MCD_CHL_AT_SND - Send channel.
 * * %MCD_CHL_AT_MEM_MAPPED - Channel is memory mapped.
 * * %MCD_CHL_AT_HAS_PRIO - Channel has a defined priority.
 */
typedef uint32_t mcd_chl_attributes_et;
enum {
    MCD_CHL_AT_RCV           = 0x00000001,
    MCD_CHL_AT_SND           = 0x00000002,
    MCD_CHL_AT_MEM_MAPPED    = 0x00000040,
    MCD_CHL_AT_HAS_PRIO      = 0x00000800,
};


/**
 * typedef mcd_trace_type_et - Enumeration type defining basic trace types.
 *
 * This enumeration type describes the type of a trace source. The type values
 * are bitwise mutually exclusive. User defined trace types need to have values
 * between %MCD_TRACE_TYPE_CUSTOM_LO and %MCD_TRACE_TYPE_CUSTOM_HI.
 *
 * * %MCD_TRACE_TYPE_UNKNOWN - Unknown trace source.
 * * %MCD_TRACE_TYPE_CORE - Traces the instruction and (optional) data trace
 *   stream as seen from the core.
 * * %MCD_TRACE_TYPE_BUS - Traces a bus that is not related to the program flow.
 * * %MCD_TRACE_TYPE_EVENT - Traces logical signals (can include buses) that
 *   have an asynchronous nature.
 * * %MCD_TRACE_TYPE_STAT - Traces statistical or profiling information.
 * * %MCD_TRACE_TYPE_CUSTOM_LO - Begin Range: User defined trace types.
 * * %MCD_TRACE_TYPE_CUSTOM_HI - End Range: User defined trace types.
 */
typedef uint32_t mcd_trace_type_et;
enum {
    MCD_TRACE_TYPE_UNKNOWN    = 0x00000000,
    MCD_TRACE_TYPE_CORE       = 0x00000001,
    MCD_TRACE_TYPE_BUS        = 0x00000002,
    MCD_TRACE_TYPE_EVENT      = 0x00000004,
    MCD_TRACE_TYPE_STAT       = 0x00000008,
    MCD_TRACE_TYPE_CUSTOM_LO  = 0x00000100,
    MCD_TRACE_TYPE_CUSTOM_HI  = 0x40000000,
};


/**
 * typedef mcd_trace_format_et - Enumeration type defining trace data formats.
 *
 *  This enumeration type describes the format of the trace data. Each trace
 *  source can deliver data in exactly one format, only. Standard formats should
 *  be used whenever possible. User defined trace types need to have values
 *  between %MCD_TRACE_FORMAT_CUSTOM_LO and %MCD_TRACE_FORMAT_CUSTOM_HI.
 *
 * * %MCD_TRACE_FORMAT_UNKNOWN - Trace data format not readable via API.
 * * %MCD_TRACE_FORMAT_CORE_FETCH - Execution trace extracted from bus fetch
 *   cycles (use struct mcd_trace_data_core_st for this format).
 * * %MCD_TRACE_FORMAT_CORE_EXECUTE - Execution trace (use struct
 *   mcd_trace_data_core_st for this format)
 * * %MCD_TRACE_FORMAT_CORE_FLOW_ICOUNT - Flowtrace data format similar to NEXUS
 *   traces, instruction count (use struct mcd_trace_data_core_st for this
 *   format).
 * * %MCD_TRACE_FORMAT_CORE_FLOW_BCOUNT - Flowtrace data format similar to NEXUS
 *   traces, bytes count (use struct mcd_trace_data_core_st for this format).
 * * %MCD_TRACE_FORMAT_CORE_FLOW_IPREDICATE - Flowtrace data format with
 *   predicates and instruction count (use struct mcd_trace_data_core_st for
 *   this format).
 * * %MCD_TRACE_FORMAT_EVENT - Logic and system event trace (use struct
 *   mcd_trace_data_event_st for this format).
 * * %MCD_TRACE_FORMAT_STAT - Statistics trace (use struct
 *   mcd_trace_data_stat_st for this format).
 * * %MCD_TRACE_FORMAT_CUSTOM_LO - Begin Range: User defined trace data formats.
 * * %MCD_TRACE_FORMAT_CUSTOM_HI - End Range: User defined trace data formats.
 */
typedef uint32_t mcd_trace_format_et;
enum {
    MCD_TRACE_FORMAT_UNKNOWN              = 0x00000000,
    MCD_TRACE_FORMAT_CORE_FETCH           = 0x00000001,
    MCD_TRACE_FORMAT_CORE_EXECUTE         = 0x00000002,
    MCD_TRACE_FORMAT_CORE_FLOW_ICOUNT     = 0x00000003,
    MCD_TRACE_FORMAT_CORE_FLOW_BCOUNT     = 0x00000004,
    MCD_TRACE_FORMAT_CORE_FLOW_IPREDICATE = 0x00000005,
    MCD_TRACE_FORMAT_EVENT                = 0x00000010,
    MCD_TRACE_FORMAT_STAT                 = 0x00000020,
    MCD_TRACE_FORMAT_CUSTOM_LO            = 0x00000100,
    MCD_TRACE_FORMAT_CUSTOM_HI            = 0x7FFFFFFF,
};


/**
 * typedef mcd_trace_mode_et - Enumeration type defining operation modes of a
 *                             trace buffer.
 *
 * This enumeration type describes the possible operation modes of a trace
 * buffer. The type values are bitwise mutually exclusive. User defined
 * operation modes need to have values between %MCD_TRACE_MODE_CUSTOM_LO and
 * %MCD_TRACE_MODE_CUSTOM_HI.
 *
 * * %MCD_TRACE_MODE_NOCHANGE - Do not change trace buffer mode.
 * * %MCD_TRACE_MODE_FIFO - Circular trace buffer.
 * * %MCD_TRACE_MODE_STACK - Trace stops when buffer is full.
 * * %MCD_TRACE_MODE_LEACH - Target is stopped (brought into debug state) when
 *   buffer is almost full.
 * * %MCD_TRACE_MODE_PIPE - Trace data are continuously streamed through API,
 *   buffer is a FIFO for temporary storage.
 * * %MCD_TRACE_MODE_CUSTOM_LO - Begin Range: User defined operation modes.
 * * %MCD_TRACE_MODE_CUSTOM_HI - End Range: User defined operation modes.
 */
typedef uint32_t mcd_trace_mode_et;
enum {
    MCD_TRACE_MODE_NOCHANGE   = 0x00000000,
    MCD_TRACE_MODE_FIFO       = 0x00000001,
    MCD_TRACE_MODE_STACK      = 0x00000002,
    MCD_TRACE_MODE_LEACH      = 0x00000004,
    MCD_TRACE_MODE_PIPE       = 0x00000008,
    MCD_TRACE_MODE_CUSTOM_LO  = 0x00000100,
    MCD_TRACE_MODE_CUSTOM_HI  = 0x40000000,
};


/**
 * typedef mcd_trace_state_et - Enumeration type defining trace states.
 *
 * This enumeration type describes the possible states of a trace. User defined
 * trace states need to have values between %MCD_TRACE_STATE_CUSTOM_LO and
 * %MCD_TRACE_STATE_CUSTOM_HI.
 *
 * * %MCD_TRACE_STATE_NOCHANGE - Do not change state (only for
 *   mcd_set_trace_state_f()).
 * * %MCD_TRACE_STATE_DISABLE - Trace is disabled and no resources are
 *   allocated.
 * * %MCD_TRACE_STATE_OFF - Trace is off and does not trace data, but is ready
 *   for tracing.
 * * %MCD_TRACE_STATE_ARM - Trace is armed.
 * * %MCD_TRACE_STATE_TRIGGER - Trace is triggered and waits for the post
 *   trigger delay.
 * * %MCD_TRACE_STATE_STOP - Trace has stopped (after trigger and post trigger
 *   delay have elapsed).
 * * %MCD_TRACE_STATE_INIT - Clears trace buffer and goes into OFF state (only
 *   for mcd_set_trace_state_f()).
 * * %MCD_TRACE_STATE_CUSTOM_LO - Begin Range: User defined trace states.
 * * %MCD_TRACE_STATE_CUSTOM_HI - End Range: User defined trace states.
 */
typedef uint32_t mcd_trace_state_et;
enum {

    MCD_TRACE_STATE_NOCHANGE   = 0x00000000,
    MCD_TRACE_STATE_DISABLE    = 0x00000001,
    MCD_TRACE_STATE_OFF        = 0x00000002,
    MCD_TRACE_STATE_ARM        = 0x00000003,
    MCD_TRACE_STATE_TRIGGER    = 0x00000004,
    MCD_TRACE_STATE_STOP       = 0x00000005,
    MCD_TRACE_STATE_INIT       = 0x00000010,
    MCD_TRACE_STATE_CUSTOM_LO  = 0x00000100,
    MCD_TRACE_STATE_CUSTOM_HI  = 0x7FFFFFFF,
};


/**
 * typedef mcd_trace_marker_et - Enumeration type defining trace markers.
 *
 * This enumeration type describes markers associated with a single trace frame.
 * The type values are bitwise mutually exclusive and a member of type
 * mcd_trace_marker_et may be a combination of several of them. User defined
 * trace markers need to have values between %MCD_TRACE_MARKER_CUSTOM_LO and
 * %MCD_TRACE_MARKER_CUSTOM_HI.
 *
 * * %MCD_TRACE_MARKER_NONE - No marker set.
 * * %MCD_TRACE_MARKER_RUN - Core has started execution in this trace frame
 *   (first cycle).
 * * %MCD_TRACE_MARKER_DEBUG - Core has stopped execution in this trace frame
 *   (last cycle).
 * * %MCD_TRACE_MARKER_START - Tracing has started in this trace frame
 *   (controlled by trigger).
 * * %MCD_TRACE_MARKER_STOP - Tracing has stopped in this trace frame
 *   (controlled by trigger).
 * * %MCD_TRACE_MARKER_ERROR - Error marker (hardware failure or program flow
 *   reconstruction error).
 * * %MCD_TRACE_MARKER_GAP - Gap in trace (caused by bandwidth limitation on
 *   trace port).
 * * %MCD_TRACE_MARKER_CUSTOM_LO - Begin Range: User defined trace markers.
 * * %MCD_TRACE_MARKER_CUSTOM_HI - End Range: User defined trace markers.
 */
typedef uint32_t mcd_trace_marker_et;
enum {
    MCD_TRACE_MARKER_NONE      = 0x00000000,
    MCD_TRACE_MARKER_RUN       = 0x00000001,
    MCD_TRACE_MARKER_DEBUG     = 0x00000002,
    MCD_TRACE_MARKER_START     = 0x00000004,
    MCD_TRACE_MARKER_STOP      = 0x00000008,
    MCD_TRACE_MARKER_ERROR     = 0x00000010,
    MCD_TRACE_MARKER_GAP       = 0x00000020,
    MCD_TRACE_MARKER_CUSTOM_LO = 0x00000100,
    MCD_TRACE_MARKER_CUSTOM_HI = 0x40000000,
};


/**
 * typedef mcd_trace_cycle_et - Enumeration type defining basic trace cycles.
 *
 * This enumeration type describes the basic trace cycle types for bus and core
 * traces. User defined trace cycle types need to have values between
 * %MCD_TRACE_CYCLE_CUSTOM_LO and %MCD_TRACE_CYCLE_CUSTOM_HI.
 *
 * * %MCD_TRACE_CYCLE_UNKNOWN - Trace cycle contains no valid data for this
 *   core.
 * * %MCD_TRACE_CYCLE_NONE - No trace cycle, control information (marker,
 *   timestamp) is valid.
 * * %MCD_TRACE_CYCLE_EXECUTE - Program execution cycle, marks the execution of
 *   one instruction. For a program flow trace this marks the execution of a
 *   block which is ended with a taken branch.
 * * %MCD_TRACE_CYCLE_NOTEXECUTE - Program execution cycle, marks the execution
 *   of one conditional instruction with a "failing" condition code. For a
 *   program flow trace this marks the execution of a block which is ended
 *   without a branch.
 * * %MCD_TRACE_CYCLE_FETCH - Program fetch cycle, the instruction related to
 *   the cycle may just be prefetched.
 * * %MCD_TRACE_CYCLE_READ - Data read cycle.
 * * %MCD_TRACE_CYCLE_WRITE - Data write cycle.
 * * %MCD_TRACE_CYCLE_OWNERSHIP - Ownership change cycle, usually indicates a
 *   change of the executed software thread.
 * * %MCD_TRACE_CYCLE_CUSTOM_LO - Begin Range: User defined trace cycles.
 * * %MCD_TRACE_CYCLE_CUSTOM_HI - End   Range: User defined trace cycles.
 */
typedef uint32_t mcd_trace_cycle_et;
enum {
    MCD_TRACE_CYCLE_UNKNOWN    = 0x00000000,
    MCD_TRACE_CYCLE_NONE       = 0x00000001,
    MCD_TRACE_CYCLE_EXECUTE    = 0x00000002,
    MCD_TRACE_CYCLE_NOTEXECUTE = 0x00000003,
    MCD_TRACE_CYCLE_FETCH      = 0x00000004,
    MCD_TRACE_CYCLE_READ       = 0x00000005,
    MCD_TRACE_CYCLE_WRITE      = 0x00000006,
    MCD_TRACE_CYCLE_OWNERSHIP  = 0x00000007,
    MCD_TRACE_CYCLE_CUSTOM_LO  = 0x00000100,
    MCD_TRACE_CYCLE_CUSTOM_HI  = 0x7FFFFFFF,
};


/**
 * DOC: Definition of Structures
 *
 * This is the list of data structures exchanged by the functions of the MCD
 * API.
 */


/**
 * struct mcd_api_version_st - Structure type containing the MCD API version
 *                             information of the tool.
 *
 * @v_api_major: API major version.
 * @v_api_minor: API minor version.
 * @author:      API name of the author of this MCD API version.
 *
 * This structure type contains version information about the MCD API
 * implementation of the tool. Reference version at end of SPRINT project is:
 *
 * * @v_api_major = 1
 * * @v_api_minor = 0
 * * @author = "SPRINT Release"
 */
typedef struct mcd_api_version_st {
    uint16_t   v_api_major;
    uint16_t   v_api_minor;
    char       author[MCD_API_IMP_VENDOR_LEN];
} mcd_api_version_st;


/**
 * struct mcd_impl_version_info_st - Structure type containing the MCD API
 *                                   implementation information.
 *
 * @v_api:       Implemented API version.
 * @v_imp_major: Major version number of this implementation.
 * @v_imp_minor: Minor version number of this implementation.
 * @v_imp_build: Build number of this implementation.
 * @vendor:      Name of vendor of the implementation.
 * @date:        String from __DATE__ macro at compile time.
 *
 * This structure type contains important information about the particular
 * implementation of the MCD API.
 */
typedef struct mcd_impl_version_info_st {
    mcd_api_version_st  v_api;
    uint16_t            v_imp_major;
    uint16_t            v_imp_minor;
    uint16_t            v_imp_build;
    char                vendor[MCD_API_IMP_VENDOR_LEN];
    char                date[16];
} mcd_impl_version_info_st;


/**
 * struct mcd_error_info_st - Structure type containing the error status and
 *                            error event notification.
 *
 * @return_status: Return status from the last API call.
 * @error_code:    Detailed error code from the last API call.
 * @error_events:  Detailed event code from the last API call.
 * @error_str:     Detailed error text string from the last API call.
 *
 * All API functions return a value of type mcd_return_et. If this value
 * indicates an error or an error event that has happened during the last API
 * call, the calling function has to handle it appropriately. This can be
 * achieved by asking for more information about the occurred error or error
 * event. This structure type contains all the required details about the error
 * and/or the error event as reported by the target.
 */
typedef struct mcd_error_info_st {
    mcd_return_et       return_status;
    mcd_error_code_et   error_code;
    mcd_error_event_et  error_events;
    char                error_str[MCD_INFO_STR_LEN];
} mcd_error_info_st;


/**
 * struct mcd_server_info_st - Structure type containing the server information.
 *
 * @server:          String containing the server name.
 * @system_instance: String containing the unique system instance identifier.
 * @acc_hw:          String containing the unique device access hardware name.
 *
 * This structure type contains the information about a running or an installed
 * server.
 *
 * @server contains a string with the server name. For a running simulation
 * server, @system_instance has the same value as @system_instance in
 * mcd_core_con_info_st, and @acc_hw contains an empty string. For a real
 * hardware server it is the other way around.
 */
typedef struct mcd_server_info_st {
    char        server[MCD_UNIQUE_NAME_LEN];
    char        system_instance[MCD_UNIQUE_NAME_LEN];
    char        acc_hw[MCD_UNIQUE_NAME_LEN];
} mcd_server_info_st;


/**
 * struct mcd_server_st - Structure type containing the server connection
 *                        instance.
 *
 * @instance: Server connection instance of an implementation at lower level.
 * @host: String containing the host name.
 * @config_string: Server configuration information.
 *
 *  This structure type contains a server connection instance.
 *
 *  For the MCD API a server provides the capability to connect to a system, its
 *  devices and/or cores. A server can arrange connections to several systems. A
 *  system again consists of devices and cores, where devices may subsume
 *  several cores, e.g. a SoC on a real hardware board. Consequently, a
 *  multi-core simulation is a system with several processor cores.
 */
typedef struct mcd_server_st {
     void              *instance;
     const char        *host;
     const char        *config_string;
} mcd_server_st;


/**
 * struct mcd_core_con_info_st - Structure type containing the core connection
 *                               information.
 *
 * @host:            String containing the IP host name.
 * @server_port:     Port number of the server.
 * @server_key:      String containing the server key as provided by
 *                   mcd_open_server_f().
 * @system_key:      String containing the system key as provided by
 *                   mcd_open_server_f().
 * @device_key:      String containing the device key, optional for
 *                   mcd_open_core_f().
 * @system:          String containing the system name. Predefined value is
 *                   "Real HW" for physical devices. Note that in case of "Real
 *                   HW" the @acc_hw always needs to be defined.
 * @system_instance: String containing the unique system instance identifier.
 *                   Allows to differentiate between several system instances
 *                   with the same name. A typical use case is a simulator where
 *                   different instances can be distinguished by their process
 *                   ID. (For example @system_instance could be: "Process ID:
 *                   1234")
 * @acc_hw:          String containing the unique device access hardware name.
 * @device_type:     Device type identifier (IEEE 1149.1 device ID)
 * @device:          String containing the system unique device instance name.
 *                   For Real HW this is usually the sales name of the device.
 *                   If the access hardware operates a multi device target
 *                   system (e.g. over IEEE1149.7), this device string can
 *                   contain an index to differentiate between several devices
 *                   of the same type.
 * @device_id:       Unique device ID.
 * @core:            String containing the device unique core name.
 * @core_type:       Core type identifier (taken from ELF predefined
 *                   architecture)
 * @core_id:         Unique core ID representing the core version.
 *
 * The MCD hierarchy's top-level is a system. The next level are devices and
 * followed by cores at the lowest level are cores. The MCD API is core centric,
 * i.e. connections are established to specific cores and not to a device or a
 * system. The core connection information is used to open this connection with
 * mcd_open_core_f(). In order to establish a core connection, the core
 * connection information does not have to complete but it has to be
 * unambiguous. A set of hierarchical query functions, starting at system level,
 * allows to parse each system top down. It is recommended to exclude
 * unnecessary and redundant hierarchy information from @core and @device.
 * @device needs to be readable and unambiguous within a @system. @core again
 * has to be readable and unambiguous within its superior @device instance.
 *
 * This structure type contains all information required to establish a core
 * connection.
 */
typedef struct mcd_core_con_info_st {
    char        host[MCD_HOSTNAME_LEN];
    uint32_t    server_port;
    char        server_key[MCD_KEY_LEN];
    char        system_key[MCD_KEY_LEN];
    char        device_key[MCD_KEY_LEN];
    char        system[MCD_UNIQUE_NAME_LEN];
    char        system_instance[MCD_UNIQUE_NAME_LEN];
    char        acc_hw[MCD_UNIQUE_NAME_LEN];
    uint32_t    device_type;
    char        device[MCD_UNIQUE_NAME_LEN];
    uint32_t    device_id;
    char        core[MCD_UNIQUE_NAME_LEN];
    uint32_t    core_type;
    uint32_t    core_id;
} mcd_core_con_info_st;


/**
 * struct mcd_core_st - Structure type containing the core connection instance.
 *
 * @instance:      Core connection instance of an implementation at lower level.
 *                 This void pointer must not be null except from function calls
 *                 concerning communication channels. For these calls, null
 *                 pointers are allowed in order to address hierarchical levels
 *                 higher than core level.
 * @core_con_info: Core connection information of the core instance.
 *
 *  This structure type contains a core connection instance.
 */
typedef struct mcd_core_st {
    void                        *instance;
    const mcd_core_con_info_st  *core_con_info;
} mcd_core_st;


/**
 * struct mcd_core_mode_info_st - Structure type containing information about a
 *                                core mode.
 *
 * @core_mode: Contains one of the 32 possible core modes,
 *             values can be 1 to 32.
 * @name:      The name of this core mode.
 *
 * This structure type contains information about a specific core mode. Most
 * cores have for example "supervisor" or "user" operation modes. @core_mode can
 * be a value within the range of 1 to 32. Some API structures contain bitmasks
 * of which each bit corresponds to @core_mode of exactly one core mode (bit 0
 * corresponds to core mode 1). Core mode 0 is used to define a default core
 * mode - usually the most permissive core mode.
 */
typedef struct mcd_core_mode_info_st {
    uint8_t                core_mode;
    char                   name[MCD_CORE_MODE_NAME_LEN];
} mcd_core_mode_info_st;


/**
 * struct mcd_addr_st - Structure type containing a completely resolved logical
 *                      or physical memory address.
 *
 * @address:         Address value within a memory space, expressed in bytes.
 * @mem_space_id:    ID of the memory space associated with this address, e.g. a
 *                   program memory, a data memory or registers .
 * @addr_space_id:   ID of the address space in which this address is valid.
 * @addr_space_type: Type of the address space in which this address is valid.
 *
 * This structure type contains a completely resolved logical or physical memory
 * address. The @address is always expressed in bytes, even if the minimum
 * access unit (MAU) size is larger than a byte. The
 * @addr_space_id can be used for different purposes as defined by
 * @addr_space_type.
 */
typedef struct mcd_addr_st {
    uint64_t               address;
    uint32_t               mem_space_id;
    uint32_t               addr_space_id;
    mcd_addr_space_type_et addr_space_type;
} mcd_addr_st;


/**
 * struct mcd_memspace_st - Structure type containing information about a memory
 *                          space.
 *
 * @mem_space_id:             ID of this memory space, ID 0 is reserved.
 * @mem_space_name:           Unique name of the memory space.
 * @mem_type:                 Type of the memory space.
 * @bits_per_mau:             Bits per minimum addressable unit (MAU). The
 *                            minimum addressable unit of a memory is defined as
 *                            the size in bits of its basic block that may have
 *                            a unique address. For example for a byte
 *                            addressable memory this value would be set to '8'
 *                            according to the 8 bits of a byte block.
 * @invariance:               The total number of bytes in a memory word, which
 *                            is @bits_per_mau divided by 8, consists of groups
 *                            of "invariant" bytes. These groups can be arranged
 *                            in Big Endian or Little Endian order.
 *                            For example an @invariance of '2' and '64'
 *                            @bits_per_mau, a Little Endian word are
 *                            represented as b0 b1 b2 b3 b4 b5 b6 b7.
 *                            In contrast to this, a Big Endian word is
 *                            represented as b6 b7 b4 b5 b2 b3 b0 b1.
 * @endian:                   Endianness of this memory space. Can be overriden
 *                            by @endian of a mcd_memblock_st.
 * @min_addr:                 Minimum address of this memory space.
 * @max_addr:                 Maximum address of this memory space.
 * @num_mem_blocks:           Number of memory blocks in this memory space. Each
 *                            memory space may have a certain number of memory
 *                            blocks. Memory blocks contain additional
 *                            information pertaining to the intended purpose of
 *                            the memory. This information may be used as a hint
 *                            for memory data representation within a tool's
 *                            memory view. This field specifies the number of
 *                            memory blocks present in this memory space.
 * @supported_access_options: Supported memory access options (OR'ed bitmask).
 *                            Can be overriden by @supported_access_options of a
 *                            mcd_memblock_st.
 * @core_mode_mask_read:      Mask of core modes for which read accesses are
 *                            impossible. A set bit indicates that read accesses
 *                            are denied in this mode. Bit 0 represents core
 *                            mode '1', bit 31 represents core mode '32'. Can be
 *                            overriden by
 *                            @core_mode_mask_read of a mcd_memblock_st.
 * @core_mode_mask_write:     Mask of core modes for which write accesses are
 *                            impossible; a set bit indicates that write
 *                            accesses are denied in this mode. Bit 0 represents
 *                            core mode '1', bit 31 represents core mode '32'.
 *                            Can be overriden by
 *                            @core_mode_mask_write of a mcd_memblock_st.
 *
 * This structure type contains information about a memory space. of a target
 * core. A memory space defines a region of memory used in different processor
 * architectures, e.g. "program" and "data" memory of a Harvard architecture or
 * "P"/"X"/"Y"/"Z" of a DSP architecture.
 *
 * Users must note that the sematics used in order to access a cache memory are
 * the same as for accessing regular memory. On the target side, each cache
 * memory implementation must be provided as a different memory space with a
 * unique memory space ID. All accesses to such a memory space must be
 * understood by the target as debug access to the cache.
 */
typedef struct mcd_memspace_st {
    uint32_t             mem_space_id;
    char                 mem_space_name[MCD_MEM_SPACE_NAME_LEN];
    mcd_mem_type_et      mem_type;
    uint32_t             bits_per_mau;
    uint8_t              invariance;
    mcd_endian_et        endian;
    uint64_t             min_addr;
    uint64_t             max_addr;
    uint32_t             num_mem_blocks;
    mcd_tx_access_opt_et supported_access_options;
    uint32_t             core_mode_mask_read;
    uint32_t             core_mode_mask_write;
} mcd_memspace_st;


/**
 * struct mcd_memblock_st - Structure type containing information about a memory
 *                          block.
 *
 * @mem_block_id:             ID of this memory block, ID 0 is reserved.
 * @mem_block_name:           Memory block name.
 * @has_children:             Indicating that this block has children.
 * @parent_id:                ID of this block's parent (%MCD_MEM_BLOCK_NOPARENT
 *                            if no parent exists). Memory blocks by definition
 *                            can be hierarchical. This field describes the ID
 *                            of the parent memory block. In case this memory
 *                            block is at root level (and therefore has no
 *                            parent) the @parent_id field has to be set to
 *                            %MCD_MEM_BLOCK_NOPARENT.
 * @start_addr:               Start address of this block.
 * @end_addr:                 End address of this block.
 * @endian:                   Endianness of this memory block. Overrides @endian
 *                            of the corresponding mcd_memspace_st.
 * @supported_au_sizes:       This array has a maximum of %MCD_MEM_AUSIZE_NUM
 *                            entries. Each entry different from '0' indicates
 *                            the permissible size of an addressable memory unit
 *                            in bits. All entries represent an allowed multiple
 *                            of the @bits_per_mau field in the corresponding
 *                            mcd_memspace_st data structure. For example, the
 *                            supported addressable unit sizes for a memory
 *                            block in a memory space with '32' bits_per_mau
 *                            would be {32, 64, 96, 128}. This array field would
 *                            then contain the values {1, 2, 3, 4}.
 * @supported_access_options: Supported memory access options (OR'ed bitmask).
 *                            Overrides @supported_access_options of the
 *                            corresponding mcd_memspace_st.
 * @core_mode_mask_read:      Mask of core modes for which read accesses are
 *                            impossible. A set bit indicates that read accesses
 *                            are denied in this mode. Bit 0 represents core
 *                            mode '1', bit 31 represents core mode '32'.
 *                            Overrides @core_mode_mask_read of the
 *                            corresponding mcd_memspace_st.
 * @core_mode_mask_write:     Mask of core modes for which write accesses are
 *                            impossible. A set bit indicates that write
 *                            accesses are denied in this mode. Bit 0 represents
 *                            core mode '1', bit 31 represents core mode '32'.
 *                            Overrides @core_mode_mask_write of the
 *                            corresponding mcd_memspace_st.
 *
 *  This structure type contains information about a memory block. A memory
 *  block is defined as a continuous range of memory addresses with same
 *  properties. A memory block is owned by a memory space.
 */
typedef struct mcd_memblock_st {
    uint32_t               mem_block_id;
    char                   mem_block_name[MCD_MEM_BLOCK_NAME_LEN];
    bool                   has_children;
    uint32_t               parent_id;
    uint64_t               start_addr;
    uint64_t               end_addr;
    mcd_endian_et          endian;
    uint32_t               supported_au_sizes[MCD_MEM_AUSIZE_NUM];
    mcd_tx_access_opt_et   supported_access_options;
    uint32_t               core_mode_mask_read;
    uint32_t               core_mode_mask_write;
} mcd_memblock_st;


/**
 * struct mcd_register_group_st - Structure type containing register group
 *                                information.
 *
 * @reg_group_id:   Contains the ID of this register group. A register group ID
 *                  must be unique within the scope of a target core. ID '0' is
 *                  reserved.
 * @reg_group_name: The name of a register group. A register group name cannot
 *                  be longer than %MCD_REG_NAME_LEN characters (use
 *                  representative names).
 * @n_registers:    Number of registers part of this group.
 *
 * This structure type contains the properties of a register group of a target
 * core.
 */
typedef struct mcd_register_group_st {
    uint32_t    reg_group_id;
    char        reg_group_name[MCD_REG_NAME_LEN];
    uint32_t    n_registers;
} mcd_register_group_st;


/**
 * struct mcd_register_info_st - Structure type containing register information
 *                               for a single register.
 *
 * @addr:                   Either the address of a memory mapped register or
 *                          the register address in a dedicated "register memory
 *                          space"
 * @reg_group_id:           ID of the group this register belongs to.
 * @regname:                The name of a register. A register name cannot be
 *                          longer than %MCD_REG_NAME_LEN characters (use
 *                          representative names).
 * @regsize:                Register size in bits.
 * @core_mode_mask_read:    Mask of core modes for which read accesses are
 *                          impossible. A set bit indicates that read accesses
 *                          are denied in this mode. Bit 0 represents core mode
 *                          '1', bit 31 represents core mode 32. Overrides
 *                          @core_mode_mask_read of the corresponding
 *                          mcd_memspace_st.
 * @core_mode_mask_write:   Mask of core modes for which write accesses are
 *                          impossible. A set bit indicates that write accesses
 *                          are denied in this mode. Bit 0 represents core mode
 *                          '1', bit 31 represents core mode '32'. Overrides
 *                          @core_mode_mask_write of the corresponding
 *                          mcd_memspace_st.
 * @has_side_effects_read:  Reading this register can trigger side effects.
 * @has_side_effects_write: Writing this register can trigger side effects.
 * @reg_type:               Register type (simple, compound or partial)
 * @hw_thread_id:           Hardware thread ID this register belongs to. The ID
 *                          must be set to '0' if the register is not assigned
 *                          to a hardware thread.
 *
 * This structure contains the properties of a single register of a target
 * core.
 */
typedef struct mcd_register_info_st {
    mcd_addr_st       addr;
    uint32_t          reg_group_id;
    char              regname[MCD_REG_NAME_LEN];
    uint32_t          regsize;
    uint32_t          core_mode_mask_read;
    uint32_t          core_mode_mask_write;
    bool              has_side_effects_read;
    bool              has_side_effects_write;
    mcd_reg_type_et   reg_type;
    uint32_t          hw_thread_id;
} mcd_register_info_st;


/**
 * struct mcd_trig_info_st - Structure type containing information about trigger
 *                           capabilities.
 *
 * @type:           Supported trigger types (OR'ed bitmask).
 * @option:         Supported trigger options (OR'ed bitmask).
 * @action:         Supported trigger actions (OR'ed bitmask).
 * @trig_number:    Number of usable triggers (or 0 if number not known).
 * @state_number:   Number of states of the trigger set's state machine (or 0 if
 *                  not known).
 * @counter_number: Number of usable counters (or 0 if not known)
 * @sw_breakpoints: True if software breakpoints via code patch are available.
 *
 * This structure type contains information about the trigger capabilities of a
 * target.
 *
 * Note: @trig_number, @state_number and @counter_number should NOT be used to
 * determine if the appropriate trigger resource is available. It can just
 * provide hints about the maximum number. The availability should be checked
 * evaluating @action.
 */
typedef struct mcd_trig_info_st {
    mcd_trig_type_et         type;
    mcd_trig_opt_et          option;
    mcd_trig_action_et       action;
    uint32_t                 trig_number;
    uint32_t                 state_number;
    uint32_t                 counter_number;
    bool                     sw_breakpoints;
} mcd_trig_info_st;


/**
 * struct mcd_ctrig_info_st - Structure type containing information about a
 *                            custom trigger.
 *
 * @ctrig_id: Custom trigger ID, ID 0 is reserved.
 * @info_str: Description of the custom trigger.
 *
 * This structure type contains information about a custom trigger. These custom
 * triggers can be used via the mcd_trig_custom_st structure type.
 *
 * Note: This is NOT related to custom trigger formats - they use a format not
 * defined by the MCD API.
 */
typedef struct mcd_ctrig_info_st {
    uint32_t          ctrig_id;
    char              info_str[MCD_INFO_STR_LEN];
} mcd_ctrig_info_st;


/**
 * struct mcd_trig_complex_core_st - Structure type containing information about
 *                                   a complex core trigger condition.
 *
 * @struct_size:    Size of this structure in bytes.
 * @type:           Trigger type, for this structure type it must be one of:
 *
 *                  - %MCD_TRIG_TYPE_IP
 *                  - %MCD_TRIG_TYPE_READ
 *                  - %MCD_TRIG_TYPE_WRITE
 *                  - %MCD_TRIG_TYPE_RW
 *                  - %MCD_TRIG_TYPE_NOCYCLE
 *
 * @option:         Adds further qualifiers to the trigger or overrides the
 *                  behaviour (multiple options possible)
 * @action:         Action to be taken on trigger. Only one per trigger allowed.
 * @action_param:   Parameter for action - depends on the selected action.
 * @modified:       Set to "TRUE" on return of mcd_create_trig_f() if trigger
 *                  was modified by implementation, untouched otherwise.
 * @state_mask:     Set bits indicate that this trigger is inactive when
 *                  reaching the corresponding state of the state machine. Bit 0
 *                  represents state '1' of the state machine. Only to be
 *                  considered if %MCD_TRIG_OPT_STATE_IS_CONDITION is set in
 *                  @option.
 * @addr_start:     Start address for the address range the trigger shall be
 *                  activated for.
 * @addr_range:     Size of the address range for the trigger (in bytes). If it
 *                  is set to '0', the trigger is activated by an access to a
 *                  single address. If it is set to '1', the range of addresses
 *                  is two (@addr_start + 1). The address range can be
 *                  "inverted" if %MCD_TRIG_OPT_OUT_OF_RANGE is set in @option.
 * @data_start:     Data comparison value of the trigger. Only considered if
 *                  %MCD_TRIG_OPT_DATA_IS_CONDITION is set in @option. Setting
 *                  option %MCD_TRIG_OPT_NOT_DATA activates the trigger on a
 *                  data mismatch.
 * @data_range:     Size of the data value range for the trigger. If it is set
 *                  to '0', the trigger is activated on a match with a single
 *                  value. If it is set to '1', the range of values is two
 *                  (@data_range + 1). Option %MCD_TRIG_OPT_SIGNED_DATA may be
 *                  set in @option if the data shall be interpreted as signed.
 *                  This usually also requires the option
 *                  %MCD_TRIG_OPT_DATASIZE_IS_CONDITION to be set in @option.
 * @data_mask:      Only value bits are considered for which the mask is set to
 *                  '0'
 * @data_size:      Size of the access in bytes. If set to '0' the size shall
 *                  not be considered. Shall be only considered if
 *                  %MCD_TRIG_OPT_DATASIZE_IS_CONDITION is set in @option.
 * @hw_thread_id:   ID of the hardware thread this trigger is associated with.
 * @sw_thread_id:   ID of the software thread this trigger is associated with.
 * @core_mode_mask: Mask of core modes for which the trigger shall not be
 *                  activated. A set bit disables the trigger for the
 *                  corresponding mode. Bit 0 represents core mode '1', bit 31
 *                  represents core mode '32'.
 *
 * This structure type contains information about a complex core based trigger
 * of the target system.
 */
typedef struct mcd_trig_complex_core_st {
    uint32_t             struct_size;
    mcd_trig_type_et     type;
    mcd_trig_opt_et      option;
    mcd_trig_action_et   action;
    uint32_t             action_param;
    bool                 modified;
    uint32_t             state_mask;
    mcd_addr_st          addr_start;
    uint64_t             addr_range;
    uint64_t             data_start;
    uint64_t             data_range;
    uint64_t             data_mask;
    uint32_t             data_size;
    uint32_t             hw_thread_id;
    uint64_t             sw_thread_id;
    uint32_t             core_mode_mask;
} mcd_trig_complex_core_st;


/**
 * struct mcd_trig_simple_core_st - Structure type containing information about
 *                                  a simple core trigger condition.
 * @struct_size:    Size of this structure in bytes.
 * @type:           Trigger type, for this structure type it must be one of:
 *
 *                  - %MCD_TRIG_TYPE_IP
 *                  - %MCD_TRIG_TYPE_READ
 *                  - %MCD_TRIG_TYPE_WRITE
 *                  - %MCD_TRIG_TYPE_RW
 *                  - %MCD_TRIG_TYPE_NOCYCLE
 *
 * @option:         Adds further qualifiers to the trigger or overrides the
 *                  behaviour (multiple options possible)
 * @action:         Action to be taken on trigger. Only one per trigger allowed.
 * @action_param:   Parameter for action - depends on the selected action.
 * @modified:       Set to "TRUE" on return of mcd_create_trig_f() if trigger
 *                  was modified by implementation, untouched otherwise.
 * @state_mask:     Set bits indicate that this trigger is inactive when
 *                  reaching the corresponding state of the state machine. Bit 0
 *                  represents state '1' of the state machine. Only to be
 *                  considered if %MCD_TRIG_OPT_STATE_IS_CONDITION is set in
 *                  @option.
 * @addr_start:     Start address for the address range the trigger shall be
 *                  activated for.
 * @addr_range:     Size of the address range for the trigger (in bytes). If it
 *                  is set to '0', the trigger is activated by an access to a
 *                  single address. If it is set to '1', the range of addresses
 *                  is two (@addr_start + 1). The address range can be
 *                  "inverted" if %MCD_TRIG_OPT_OUT_OF_RANGE is set in @option.
 *
 * This structure type contains information about a simple core based trigger of
 * the target system. It is a subset of mcd_trig_complex_core_st.
 */
typedef struct mcd_trig_simple_core_st {
    uint32_t             struct_size;
    mcd_trig_type_et     type;
    mcd_trig_opt_et      option;
    mcd_trig_action_et   action;
    uint32_t             action_param;
    bool                 modified;
    uint32_t             state_mask;
    mcd_addr_st          addr_start;
    uint64_t             addr_range;
} mcd_trig_simple_core_st;


/**
 * struct mcd_trig_trig_bus_st - Structure type containing information about a
 *                               trigger bus based trigger condition.
 *
 * @struct_size:    Size of this structure in bytes.
 * @type:           Trigger type, for this structure type it must be
 *                  %MCD_TRIG_TYPE_TRIG_BUS.
 * @option:         Trigger options, for this structure the following are
 *                  allowed:
 *
 *                  - %MCD_TRIG_OPT_NOT
 *                  - %MCD_TRIG_OPT_STATE_IS_CONDITION
 *
 * @action:         Action to be taken on trigger. Only one per trigger allowed.
 * @action_param:   Parameter for action - depends on the selected action.
 * @modified:       Set to "TRUE" on return of mcd_create_trig_f() if trigger
 *                  was modified by implementation, untouched otherwise.
 * @state_mask:     Set bits indicate that this trigger is inactive when
 *                  reaching the corresponding state of the state machine. Bit 0
 *                  represents state '1' of the state machine. Only to be
 *                  considered if %MCD_TRIG_OPT_STATE_IS_CONDITION is set in
 *                  @option.
 * @trig_bus_value: Trigger bus value.
 * @trig_bus_mask:  Only value bits are considered for which the bitmask is set
 *                  to '0'.
 *
 * Trigger buses exist that can be optionally activated. This structure type
 * contains information about a trigger on the target system based on such a
 * trigger bus.
 *
 * A trigger bus is split into a core local trigger (bits 0 to 15) and a global
 * trigger (bits 16 to 31). On real silicon some bits of the trigger bus may
 * also be available on device pins.
 */
typedef struct mcd_trig_trig_bus_st {
    uint32_t              struct_size;
    mcd_trig_type_et      type;
    mcd_trig_opt_et       option;
    mcd_trig_action_et    action;
    uint32_t              action_param;
    bool                  modified;
    uint32_t              state_mask;
    uint32_t              trig_bus_value;
    uint32_t              trig_bus_mask;

} mcd_trig_trig_bus_st;


/**
 * struct mcd_trig_counter_st - Structure type containing information about a
 *                              trigger counter on the target.
 *
 * @struct_size:    Size of this s  tructure in bytes.
 * @type:           Trigger type, for this structure type it must be
 *                  %MCD_TRIG_TYPE_TRIG_COUNTER.
 * @option:         Trigger options, for this structure the following are
 *                  allowed:
 *
 *                  - %MCD_TRIG_OPT_NOT
 *                  - %MCD_TRIG_OPT_STATE_IS_CONDITION
 *
 * @action:         Action to be taken on trigger. Only one per trigger allowed.
 * @action_param:   Parameter for action - depends on the selected action.
 * @modified:       Set to "TRUE" on return of mcd_create_trig_f() if trigger
 *                  was modified by implementation, untouched otherwise.
 * @state_mask:     Set bits indicate that this trigger is inactive when
 *                  reaching the corresponding state of the state machine. Bit 0
 *                  represents state '1' of the state machine. Only to be
 *                  considered if %MCD_TRIG_OPT_STATE_IS_CONDITION is set in
 *                  @option.
 * @count_value:    Current value of counter.
 * @reload_value:   Reload value of counter.
 *
 * This structure type contains information about a trigger counter on the
 * target system.
 */
typedef struct mcd_trig_counter_st {
    uint32_t             struct_size;
    mcd_trig_type_et     type;
    mcd_trig_opt_et      option;
    mcd_trig_action_et   action;
    uint32_t             action_param;
    bool                 modified;
    uint32_t             state_mask;
    uint64_t             count_value;
    uint64_t             reload_value;
} mcd_trig_counter_st;


/**
 * struct mcd_trig_custom_st - Structure type containing information about a
 *                             custom trigger on the target.
 *
 * @struct_size:    Size of this structure in bytes.
 * @type:           Trigger type, for this structure type it must be
 *                  %MCD_TRIG_TYPE_CUSTOM.
 * @option:         Trigger options, for this structure the following are
 *                  allowed:
 *
 *                  - %MCD_TRIG_OPT_NOT
 *                  - %MCD_TRIG_OPT_STATE_IS_CONDITION
 *
 * @action:         Action to be taken on trigger. Only one per trigger allowed.
 * @action_param:   Parameter for action - depends on the selected action.
 * @modified:       Set to "TRUE" on return of mcd_create_trig_f() if trigger
 *                  was modified by implementation, untouched otherwise.
 * @state_mask:     Set bits indicate that this trigger is inactive when
 *                  reaching the corresponding state of the state machine. Bit 0
 *                  represents state '1' of the state machine. Only to be
 *                  considered if %MCD_TRIG_OPT_STATE_IS_CONDITION is set in
 *                  @option.
 * @ctrig_id:       Custom trigger ID.
 * @ctrig_args:     Custom trigger arguments.
 *
 * This structure type contains information about a custom trigger on the target
 * system.
 */
typedef struct mcd_trig_custom_st {
    uint32_t             struct_size;
    mcd_trig_type_et     type;
    mcd_trig_opt_et      option;
    mcd_trig_action_et   action;
    uint32_t             action_param;
    bool                 modified;
    uint32_t             state_mask;
    uint32_t             ctrig_id;
    uint32_t             ctrig_args[4];
} mcd_trig_custom_st;


/**
 * struct mcd_trig_state_st - Structure type containing a trigger state.
 *
 * @active:         Was active at the point of time the trigger set was
 *                  uploaded.
 * @captured:       Activated at least once after trigger got downloaded to the
 *                  target.
 * @captured_valid: The information in @captured is valid.
 * @count_value:    Current value of the counter (for counter triggers).
 * @count_valid:    The information in @count_value is valid.
 *
 * This structure type contains the state of a single trigger on the target
 * system.
 */
typedef struct mcd_trig_state_st {
    bool       active;
    bool       captured;
    bool       captured_valid;
    uint64_t   count_value;
    bool       count_valid;
} mcd_trig_state_st;


/**
 * struct mcd_trig_set_state_st - Structure type containing a trigger set state.
 *
 * @active: Set if the trigger set is currently active.
 * @state: Current state of the trigger set's state machine.
 * @state_valid: Current state is valid.
 * @trig_bus: Current state of trigger bus.
 * @trig_bus_valid: Current state of trig_bus is valid.
 * @trace: Current state of trace start/stop.
 * @trace_valid: Current state is valid.
 * @analysis: Current state of performance analysis start/stop.
 * @analysis_valid: Current state is valid.
 *
 * This structure type contains the state of the trigger set of the target
 * system.
 */
typedef struct mcd_trig_set_state_st {
    bool       active;
    uint32_t   state;
    bool       state_valid;
    uint32_t   trig_bus;
    bool       trig_bus_valid;
    bool       trace;
    bool       trace_valid;
    bool       analysis;
    bool       analysis_valid;
} mcd_trig_set_state_st;


/**
 * struct mcd_tx_st - Structure type containing information about a single
 *                    transaction.
 *
 * @addr:         The address of the first memory cell/register.
 * @access_type:  Type of access: Read/Write/Read+Write/Write+Verify.
 * @options:      Access options: burst, side-effects, alternate path, cache,
 *                etc.
 * @access_width: Access size in bytes (or 0 if access size does not matter).
 * @core_mode:    The core mode in which the access should be performed (or 0
 *                for most permissive mode).
 * @data:         Byte array of size @num_bytes storing the access data.
 * @num_bytes:    Size of the memory/register access. The buffer @data needs to
 *                be of this size.
 * @num_bytes_ok: Number of successfully received/sent bytes.
 *
 * This structure type contains all information required for a single
 * transaction. The transaction itself can be a memory read/write operation or a
 * register read/write operation.
 *
 * For memory access transactions, the data is stored to the buffer in the
 * target's endianess format. For register access transaction, the data is
 * stored to the buffer in Little Endian format. Targets need to read/fill the
 * buffer, accordingly.
 */
typedef struct mcd_tx_st {
    mcd_addr_st            addr;
    mcd_tx_access_type_et  access_type;
    mcd_tx_access_opt_et   options;
    uint8_t                access_width;
    uint8_t                core_mode;
    uint8_t               *data;
    uint32_t               num_bytes;
    uint32_t               num_bytes_ok;
} mcd_tx_st;


/**
 * struct mcd_txlist_st - Structure type containing a transaction list.
 *
 * @tx:        Array of size @num_tx storing the transactions.
 * @num_tx:    Number of transactions.
 * @num_tx_ok: Number of transactions which succeeded without any errors.
 *
 * This structure type contains a transaction list.
 */
typedef struct mcd_txlist_st {
    mcd_tx_st *tx;
    uint32_t   num_tx;
    uint32_t   num_tx_ok;
} mcd_txlist_st;


/**
 * struct mcd_core_state_st - Structure type containing the state of a core.
 *
 * @state:        Core state.
 * @event:        Core events (OR'ed bitmask)
 * @hw_thread_id: ID of the hardware thread that caused the core to stop.
 * @trig_id:      ID of the trigger that caused the core to stop.
 * @stop_str:     Detailed description of a special stop reason.
 * @info_str:     Detailed description of the core state.
 *
 * This structure type contains information about the state of a core.
 *
 * Note that the additional information provided in @info_str is not a
 * repetition of the general core state provided by @state.
 */
typedef struct mcd_core_state_st {
    mcd_core_state_et state;
    mcd_core_event_et event;
    uint32_t          hw_thread_id;
    uint32_t          trig_id;
    char              stop_str[MCD_INFO_STR_LEN];
    char              info_str[MCD_INFO_STR_LEN];
} mcd_core_state_st;


/**
 * struct mcd_rst_info_st - Structure type containing information about a
 *                          particular reset class.
 *
 * @class_vector: Reset class vector which issues this reset. Exactly one bit
 *                may be set.
 * @info_str:     Description of the reset class.
 *
 * This structure type contains information about a particular reset class. Only
 * a single bit of the 32 bit field @class_vector can be '1'. It represents the
 * reset class for this particular reset. At target system level, there cannot
 * be two objects of type mcd_rst_info_st bound to the same reset class.
 */
typedef struct mcd_rst_info_st {
    uint32_t   class_vector;
    char       info_str[MCD_INFO_STR_LEN];
} mcd_rst_info_st;


/**
 * struct mcd_chl_st - Structure type containing information about
 *                      communication channels.
 *
 * @chl_id:          Channel ID.
 * @type:            Type of the requested channel.
 * @attributes:      Attributes the requested channel has to provide.
 * @max_msg_len:     Maximum message length (e.g. size of the message buffer as
 *                   specified by @msg_buffer_addr).
 * @msg_buffer_addr: Address of the message buffer for memory mapped channels.
 * @prio:            Channel priority for a prioritized channel. Range is from 0
 *                   (highest priority) to %MCD_CHL_LOWEST_PRIORITY.
 *
 * This structure type contains information about the setup of a communication
 * channel and about its properties.
 */
typedef struct mcd_chl_st {
    uint32_t              chl_id;
    mcd_chl_type_et       type;
    mcd_chl_attributes_et attributes;
    uint32_t              max_msg_len;
    mcd_addr_st           msg_buffer_addr;
    uint8_t               prio;
} mcd_chl_st;


/**
 * struct mcd_trace_info_st - Structure type containing information about a
 *                            trace.
 *
 * @trace_id:            ID of this trace source, ID 0 is reserved. This ID is
 *                       used to identify the trace by all trace related f
 * @trace_name:          Trace source name.
 * @trace_type:          Type of this trace.
 * @trace_format:        Used trace data format.
 * @trace_modes:         Possible modes of this trace (OR'ed bitmask).
 * @trace_no_timestamps: Is set if the target has no global "time" concept. It
 *                       may still provide clock cycle information.
 * @trace_shared:        Is set if the trace buffer used by this trace is shared
 *                       with other traces.
 * @trace_size_is_bytes: Is set when the tracebuffer size (in
 *                       mcd_trace_state_st) is defined in bytes instead of
 *                       frames.
 *
 * This structure type contains information about a trace.
 */
typedef struct mcd_trace_info_st {
    uint32_t            trace_id;
    char                trace_name[MCD_INFO_STR_LEN];
    mcd_trace_type_et   trace_type;
    mcd_trace_format_et trace_format;
    mcd_trace_mode_et   trace_modes;
    bool                trace_no_timestamps;
    bool                trace_shared;
    bool                trace_size_is_bytes;
} mcd_trace_info_st;


/**
 * struct mcd_trace_state_st - Structure type containing the trace state.
 *
 * @state:              Trace state.
 * @mode:               Trace buffer mode.
 * @autoarm:            Trace's ARM/OFF state follows core run state.
 * @wraparound:         Set if the frame counter has wrapped around (in FIFO
 *                      mode) or overflowed (in PIPE mode).
 * @frames:             Number of valid trace frames in the buffer.
 * @count:              Counts frames, but is not reset due to a wraparound if
 *                      running in FIFO mode (serves as progress indicator).
 * @size:               Maximum size of trace (frames or bytes).
 * @trigger_delay:      Trigger delay. Input has the same unit as @size (frames
 *                      or bytes). Output is the actually elapsed number of
 *                      frames.
 * @timestamp_accuracy: Accuracy of timestamping in percent (0 to 100). Higher
 *                      values indicate more accurate timestamps.
 * @timestamp_is_time:  Set when timestamp is a time value (in picoseconds).
 *                      Otherwise it represents clock cycles.
 * @options:            Implementation specific options.
 * @modified:           Set on return from  mcd_set_trace_state_f when
 *                      implementation could not exactly match requests..
 * @info_str:           Additional information about the trace (only special
 *                      state information).
 *
 * This structure type contains information about the trace state.
 *
 * Note that the additional information provided by @info_str is no repetition
 * of the general trace state provided by @state.
 */
typedef struct mcd_trace_state_st {
    mcd_trace_state_et state;
    mcd_trace_mode_et  mode;
    bool               autoarm;
    bool               wraparound;
    uint64_t           frames;
    uint64_t           count;
    uint64_t           size;
    uint64_t           trigger_delay;
    uint8_t            timestamp_accuracy;
    bool               timestamp_is_time;
    uint32_t           options;
    bool               modified;
    char               info_str[MCD_INFO_STR_LEN];
} mcd_trace_state_st;


/**
 * struct mcd_trace_data_core_st - Structure type containing simple core trace
 *                                 data.
 *
 * @timestamp:  Timestamp of this cycle (picoseconds or clock cycles).
 * @marker:     Markers for this cycle.
 * @cycle:      Basic cycle type.
 * @addr:       Address.
 * @data:       Data (code length for program flow).
 * @data_width: Width of data (in bytes), zero if @data_mask is used.
 * @data_mask:  Data bitmask, set bits indicate that the related byte in @data
 *              is valid. Zero if @data_width is used.
 * @source:     Additional source information (hardware thread ID, bus
 *              initiator, etc.
 * @aux_info:   Auxiliary information, e.g. endianess, burst information or core
 *              execution mode.
 *
 * This structure type contains simple trace data of cores and buses.
 */
typedef struct mcd_trace_data_core_st {
    uint64_t            timestamp;
    mcd_trace_marker_et marker;
    mcd_trace_cycle_et  cycle;
    mcd_addr_st         addr;
    uint64_t            data;
    uint8_t             data_width;
    uint8_t             data_mask;
    uint16_t            source;
    uint32_t            aux_info;
} mcd_trace_data_core_st;


/**
 * struct mcd_trace_data_event_st - Structure type containing logic analyzer
 *                                  trace data.
 * @timestamp: Timestamp of this cycle (either picoseconds or clock cycles).
 * @marker:    Markers for this cycle.
 * @data:      User data, array of 256 bits.
 *             LSB of data[0] represents channel0.
 *
 * This structure type contains "logic analyzer"-like trace data (256 channels).
 */
typedef struct mcd_trace_data_event_st {
    uint64_t            timestamp;
    mcd_trace_marker_et marker;
    uint32_t            data[8];
} mcd_trace_data_event_st;


/**
 * struct - Structure type containing statistic counter data.
 *
 * @timestamp: Timestamp of this cycle (either picoseconds or clock cycles).
 * @marker:    Markers for this cycle.
 * @count:     Array of 8 statistic counters ('-1' represents an invalid value).
 *
 * This structure type contains "logic analyzer"-like trace data (8 channels).
 */
typedef struct mcd_trace_data_stat_st {
    uint64_t            timestamp;
    mcd_trace_marker_et marker;
    uint64_t            count[8];
} mcd_trace_data_stat_st;


/**
 * DOC: Target Initialization API
 *
 * API initialization functions are dedicated to Target interface initialization
 * and closure. They allow to initialize the interaction between a tool and a
 * target, as well as clean-up connections before closure.
 */


/**
 * mcd_initialize_f() - Function initializing the interaction between a
 *                      tool-side implementation and target-side implementation.
 *
 * @version_req: [in] MCD API version as requested by an upper layer.
 * @impl_info:  [out] Information about the implementation of the MCD API
 *                    implementation.
 *
 * This function returns the version and vendor information for a particular
 * implementation of the MCD API in order to initialize the interaction between
 * a tool and a target-side implementation.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_GENERAL if target implementation is incompatible.
 */
mcd_return_et mcd_initialize_f(const mcd_api_version_st *version_req,
                               mcd_impl_version_info_st *impl_info);


/**
 * mcd_exit_f() - Function cleaning up all core and server connections from a
 *                tool.
 *
 * This function allows to perform some cleanup functionality for all core
 * connections to a particular debugger before closing the connections.
 */
void mcd_exit_f(void);


/**
 * DOC: Server Connection API
 *
 * Server-connection API functions are used to setup a connection between a tool
 * and a target through a target server. They allow to locate a target server
 * open or close a connection to a target server. They also allow to retrieve
 * and change a target server configuration.
 */


/**
 * mcd_qry_servers_f() - Function returning a list of available servers.
 *
 * @host:         [in] String containing the host name.
 * @running:      [in] Selects between running and installed servers.
 * @start_index:  [in] Start index of the queried servers. This refers to an
 *                     internal list of the target side implementation.
 * @num_servers:
 * * [in] The number of queried servers starting from the defined
 *   @start_index. If it is set to '0', no server descriptions are returned but
 *   the number of all available servers.
 * * [out] The number of returned servers. In case the input value of
 *   @num_servers is '0', this is the number of all available servers.
 * @server_info: [out] Server information. This is an array allocated by the
 *                     calling function.
 *
 * This function returns a list of available (running or installed) servers.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE       if successful.
 * - %MCD_ERR_PARAM      if @start_index is equal or larger than the number of
 *   available servers.
 * - %MCD_ERR_CONNECTION if query failed.
 */
mcd_return_et mcd_qry_servers_f(const char *host, bool running,
                                uint32_t start_index, uint32_t *num_servers,
                                mcd_server_info_st *server_info);


/**
 * mcd_open_server_f() - Function opening the connection to a server on a host
 *                       computer.
 *
 * @config_string: [in] Allows the configuration of the server connection by a
 *                      character string. Delimiters are blanks, tabs and line
 *                      breaks. Value strings are always enclosed with "double
 *                      quotes". Bool values can be "TRUE" or "FALSE" (both in
 *                      small letters).
 * @system_key:    [in] A server is claimed by this key when being opened.
 * @server:       [out] Pointer to the server connection instance. In contrast
 *                      to the MCD API's usual calling scheme, the target has to
 *                      allocate the object the pointer refers to.
 *
 * Pre-defined @config_string string parameters:
 *
 * - McdHostName= <string\>      : Optional host name. Default value is
 *   "localhost".
 * - McdServerName= <string>     : Name of the server to connect to.
 * - McdSystemInstance= <string> : Name of the simulation system instance this
 *   server is associated with.
 * - McdServerKey= <string>      : Static key for this specific server.
 * - McdExitIfLastClient <bool>  : If mcd_close_server_f() is called for the
 *   last client connection, the server will terminate.
 *
 * Additional pre-defined string parameters for real hardware:
 *
 * - McdAccHw= <string>               : Restricts this server to connect to
 *   devices via a specific access hardware as determined by the string.
 * - McdAccHw.Frequency=<unsigned>    : Decimal (32 bit) value setting the
 *   frequency of the physical I/F (e.g. according to IEEE 1149.1)
 * - McdAccHw.PostRstDelay=<unsigned> : Delay [microseconds] after reset before
 *   first interaction with the device is allowed.
 * - McdAccHw.Device="<string>"       : Description of connected device.
 * - McdAccHw.DeviceId=<unsigned>     : Device ID (e.g. IEEE 1149.1 ID) of
 *   connected device.
 * - McdAccHw.AutoDetect=<bool>       : If set to "TRUE" the access HW detects
 *   the device (DeviceId and Device will be ignored).
 *
 * Interactive Server Connection Setup
 *
 * If a server(s) is running, mcd_open_server_f() can be called with an empty or
 * NULL pointer @config_string. Then it connects to the first possible
 * simulation system or, for real hardware, access hardware path. A second call
 * (while the first server is still open) will open the second possible
 * simulation system or access hardware path and so on. In order to restrict the
 * potential list of connections to a server, "McdServerName" (and
 * "McdServerKey") can be optionally provided with @config_string.
 *
 * mcd_qry_server_config_f() returns the complete configuration string for a
 * server/device connection. This allows storing this configuration to avoid an
 * interactive server connection setup for the next debug session. This is in
 * particular useful for Real HW multi device systems in order to connect the
 * devices step by step.
 *
 * Server and System Keys
 *
 * A server can optionally require a key for access (\c config_string parameter
 * "McdServerKey"). This allows for example to prevent an unauthorized access to
 * a test stand which might cause damage. A system key additionally allows to
 * dynamically claim a server or to prevent several users from unintentionally
 * accessing the same system at the same time through a specific set of servers.
 *
 * A key can be a password string or a sequence of decimal or hexadecimal
 * numbers separated by whitespaces.
 *
 * This function opens the connection to a server on a host computer and updates
 * the internal core information data base. It contains the information about
 * all cores of devices which are simulated on the host computer or which are
 * accessible on real silicon through a specific tool access hardware to the
 * host. This data base can then be queried at system, device and core level.
 *
 * For real hardware devices, a server connection needs to be opened for each
 * access hardware. This allows individual control of the access parameters.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE        if successful.
 * - %MCD_ERR_CONNECTION  if server connection failed.
 */
mcd_return_et mcd_open_server_f(const char *system_key,
                                const char *config_string,
                                mcd_server_st **server);


/**
 * mcd_close_server_f() - Function closing the connection to a debug server on a
 *                        host computer.
 *
 * @server: [in] Pointer to the server connection instance of the opened server.
 *
 * This function closes the connection to an opened debug server on a host
 * computer.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE       if successful.
 * - %MCD_ERR_CONNECTION if closing the server connection failed.
 */
mcd_return_et mcd_close_server_f(const mcd_server_st *server);


/**
 * mcd_set_server_config_f() - Function changing the configuration of an open
 *                             debug server.
 *
 * @server:        [in] Pointer to the server connection instance of the
 *                      opened server.
 * @config_string: [in] String to configure the server or access hardware
 *                      device.
 *
 * This function allows to change the configuration of an open server. Note that
 * McdHostName, McdServerName and McdSystemInstance can't be changed with this
 * function. When the @config_string contains such parameter which can't be
 * changed or parameters which can't be changed to the requested value (e.g. new
 * McdAccHw.Frequency not supported by the Access HW), these parameters will be
 * ignored or e.g. the closest possible value will be chosen by the
 * implementation. This behavior allows to use the same config strings/files for
 * mcd_set_server_config_f() as for mcd_open_server_f(). The tool should always
 * read back the actual config parameter values with mcd_qry_server_config_f().
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE       if successful.
 * - %MCD_ERR_CONNECTION if configuration of the server or access hardware
 *   device failed.
 */
mcd_return_et mcd_set_server_config_f(const mcd_server_st *server,
                                      const char *config_string);


/**
 * mcd_qry_server_config_f() - Function retrieving the configuration string of
 *                             a debug server.
 *
 * @server:         [in] Pointer to the server connection instance.
 * @max_len:
 * * [in] Maximum length of @config_string (as allocated by the calling
 *   function).
 * * [out] Actual length required by the returned configuration string.
 * @config_string: [out] String describing the configuration of the server or
 *                       the access hardware device.
 *
 * This function retrieves the configuration string of an opened
 * debug server.
 *
 * The string can be used to retrieve the configuration of a server
 * for the following cases:
 * - Server has been opened without setting "McdServerName" via @config_string.
 * - Server has been configured with a server specific proprietary tool.
 *
 * Calling mcd_qry_server_config_f() with @max_len being a NULL pointer returns
 * the required string length for @config_string. The returned length includes
 * the terminating zero. This retrieved configuration can be stored by an MCD
 * based tool in order to configure the server connection of the next session.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE            if successful.
 * - %MCD_ERR_CONNECTION      if server connection could not be accessed.
 * - %MCD_ERR_RESULT_TOO_LONG if requested configuration string is longer than
 *   @max_len.
 */
mcd_return_et mcd_qry_server_config_f(const mcd_server_st *server,
                                      uint32_t *max_len,
                                      char *config_string);


/**
 * DOC: Target System Features API
 *
 * Target System Features API functions allow to query the core connection
 * information according to several cases: for a specified number of systems;
 * for a specified number of devices of a particular system or for a specified
 * number of cores of a system (or device). This API subset also allows querying
 * the available modes of a specific core.
 */


/**
 * mcd_qry_systems_f() - Function querying the core connection information of a
 *                       specified number of systems.
 *
 * @start_index:     [in] Start index of the queried systems. This refers to an
 *                   internal list of the target side implementation.
 * @num_systems:
 * * [in] The number of queried systems starting from the defined @start_index.
 *   If it is set to '0', no core connection information is returned but the
 *   number of available systems.
 * * [out] : The number of systems the core connection info was returned for. In
 *   case the input value of @num_systems is '0', this is the number of all
 *   available systems.
 * @system_con_info: [out] Core connection information of the requested systems.
 *                   This is an array allocated by the calling function.
 *
 * This function queries for the core connection information of a specified
 * number of systems. The returned core_con_info data are distinguished for
 * different systems only by the name of the system. If @num_systems is set to
 * '0', the function call returns the number of all available systems.
 *
 * Only the following information of @system_con_info shall be set by the
 * target:
 *
 * - system_key
 * - system
 * - system_instance
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_PARAM   if @start_index is equal or larger than the number of
 *   available systems.
 * - %MCD_ERR_GENERAL on any other error.
 */
mcd_return_et mcd_qry_systems_f(uint32_t start_index, uint32_t *num_systems,
                                mcd_core_con_info_st *system_con_info);


/**
 * mcd_qry_devices_f() - Function querying the core connection information of a
 *                       specified number of devices of a system.
 *
 * @system_con_info:  [in] Core connection information of the system the devices
 *                    are queried from.
 * @start_index:      [in] Start index of the requested devices. This refers to
 *                    an internal list of the target side implementation.
 * @num_devices:
 * * [in] The number of queried devices (e.g. simulated on or connected to this
 *   host computer) starting from the defined @start_index. If it is set to '0',
 *   no core connection information is returned but the number of all available
 *   devices.
 * * [out] The number of devices the core connection information was returned
 *   for. In case the input value of @num_devices is '0', this is the number of
 *   all available devices for the selected system.
 * @device_con_info: [out] Core connection information of the requested devices.
 *                   This is an array allocated by the calling function.
 *
 * This function queries for the core connection information of a specified
 * number of devices of a particular system. If @num_devices is set to '0', the
 * function call returns the number of all available devices for the system.
 *
 * Only the system and system_instance information of system_con_info are used
 * for system selection.
 *
 * Only the following information of @device_con_info shall be set by the
 * target:
 *
 * - host
 * - server_port
 * - system_key
 * - device_key       (zero length string if no device key)
 * - system
 * - system_instance  (zero length string for Real HW)
 * - acc_hw           (for Real HW)
 * - device_type
 * - device
 * - device_id
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_PARAM   if @start_index is equal or larger than the number of
 *   available devices.
 * - %MCD_ERR_GENERAL on any other error.
 */
mcd_return_et mcd_qry_devices_f(const mcd_core_con_info_st *system_con_info,
                                uint32_t start_index, uint32_t *num_devices,
                                mcd_core_con_info_st *device_con_info);


/**
 * mcd_qry_cores_f() - Function querying the core connection information of a
 *                     specified number of cores of a system/device.
 *
 * @connection_info: [in] Core connection information of the system or device
 *                   the cores are queried from.
 * @start_index:     [in] Start index of the requested cores. This refers to an
 *                   internal list of the target side implementation.
 * @num_cores:
 * * [in] The number of queried cores starting from the defined @start_index. If
 *   it is set to '0', no core connection information is returned but the number
 *   of all available cores.
 * * [out] The number of cores the core connection information is returned for.
 *   In case the input value of @num_cores is '0', this is the number of all
 *   available cores for the selected system or device.
 * @core_con_info:  [out] Core connection information of the requested cores.
 *                  This is an array allocated by the calling function.
 *
 * This function queries the core connection information of a specified number
 * of cores of a system/device.
 *
 * Only the system and system_instance information of connection_info are used
 * for system selection.
 *
 * For selecting a specific device, the following information of
 * @connection_info is used:
 *
 * - host
 * - server_port
 * - system_key
 * - device_key       (zero length string if no device key)
 * - system
 * - system_instance  (zero length string for Real HW)
 * - acc_hw           (for Real HW)
 * - device_type
 * - device
 * - device_id
 *
 * If device and acc_hw are given for Real HW, only the cores of this specific
 * device will be returned.
 *
 * The output parameter @core_con_info shall contain the complete
 * mcd_core_con_info_st information except from device_key.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_PARAM   if @start_index is equal or larger than the number of
 *   available cores.
 * - %MCD_ERR_GENERAL on any other error.
 */
mcd_return_et mcd_qry_cores_f(const mcd_core_con_info_st *connection_info,
                              uint32_t start_index, uint32_t *num_cores,
                              mcd_core_con_info_st *core_con_info);


/**
 * mcd_qry_core_modes_f() - Function querying the available modes of a core.
 *
 * @core:           [in] A reference to the core the calling function addresses.
 * @start_index:    [in] Start index of the requested modes. This refers to an
 *                  internal list of the target side implementation.
 * @num_modes:
 * * [in] The number of queried core modes starting from the defined
 *   @start_index. If it is set to '0', no core modes are returned but the
 *   number of all available core modes.
 * * [out] : The number of returned core modes. In case the input value of
 *   @num_modes is '0', this is the number of all available core modes for the
 *   selected core.
 * @core_mode_info: [out] : Core mode information of the requested core. This is
 *                  an array allocated by the calling function.
 *
 * This function queries the available modes of a specific core.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_PARAM   if @start_index is equal or larger than the number of
 *   available core modes.
 * - %MCD_ERR_GENERAL on any other error.
 */
mcd_return_et mcd_qry_core_modes_f(const mcd_core_st *core,
                                   uint32_t start_index, uint32_t *num_modes,
                                   mcd_core_mode_info_st *core_mode_info);


/**
 * DOC: Core Connection API
 *
 * Core Connection API functions allow the management of a connection to a core,
 * such as: opening or closing a specific core connection ; retrieving detailed
 * error and/or event information after an API call ; as well as querying
 * payload size for a transaction list.
 */


/**
 * mcd_open_core_f() - Function opening a core connection.
 *
 * @core_con_info: [in]  Unambiguous core information (e.g. from
 *                 mcd_qry_cores_f()).
 * @core:          [out] Pointer to the requested core connection instance (In
 *                 contrast to the API's usual scheme, the target has to
 *                 allocate the object the pointer refers to).
 *
 * Note that device_key needs to be set in core_con_info in case of opening a
 * locked device.
 *
 * This function opens a specific core connection.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE          if successful.
 * - %MCD_ERR_PARAM         if @core_con_info is ambiguous.
 * - %MCD_ERR_DEVICE_LOCKED if the requested device is locked.
 * - %MCD_ERR_CONNECTION    if opening the core connection failed.
 */
mcd_return_et mcd_open_core_f(const mcd_core_con_info_st *core_con_info,
                              mcd_core_st **core);


/**
 * mcd_close_core_f() - Function closing a core connection.
 *
 * @core: [in] Pointer to the core connection instance of the core to close.
 *
 * This function closes a specific core connection.
 *
 * Allowed error codes:
 *
 * * - %MCD_ERR_NONE       if successful.
 * * - %MCD_ERR_CONNECTION if closing the core connection failed.
 */
mcd_return_et mcd_close_core_f(const mcd_core_st *core);


/**
 * mcd_qry_error_info_f() - Function allowing the access to detailed error
 *                          and/or event information after an API call.
 *
 * @core:        [in] A reference to the core the calling function addresses.
 *               This parameter can be NULL if the error occurred at a function
 *               without a parameter of type mcd_core_st.
 * @error_info: [out] Pointer to a structure containing the detailed error/event
 *              information.
 *
 * Almost all MCD API functions return a value of type mcd_return_et. This is an
 * enumeration type informing the calling function how to react on the API
 * function call's results. If an error occurred, the calling function has to
 * call this function in order to obtain details about the error and/or event
 * which occurred during the previous call and in order to gain further details
 * on it.
 *
 */
void mcd_qry_error_info_f(const mcd_core_st *core,
                          mcd_error_info_st *error_info);


/**
 * mcd_qry_device_description_f () - Function retrieving the file information of
 *                                   an IP-XACT description of the addressed
 *                                   component.
 *
 * @core:       [in]  A reference to the core the calling function addresses.
 * @url:        [out] A pointer to the string containing the URL pointing to the
 *              IP-XACT description is returned through this parameter. Space
 *              for the URL must be reserved by the caller. The string returned
 *              must be null terminated except if it is too large to fit the
 *              buffer. If called with a null pointer then the required buffer
 *              size will be returned in the url_length parameter.
 * @url_length: [in] Pointing to the size of the buffer allocated by the caller.
 * @url_length: [out] Pointing to the size of the URL returned excluding the
 *              terminating '\\0' character. When called with url=0 returns the
 *              size of the buffer required including the terminating '\\0'
 *              character.
 *
 * This functions can be used to request the URL where an IP-XACT
 * description describing a system can be acquired. The most common for
 * is to use a URL starting with "file://..." referring to a local
 * file where the description is stored in the local filesystem. This
 * is also the only mandatory URI scheme ("protocol") which must be
 * supported in every tool.  Other possible options are URLs starting
 * with "http://..." or "ftp://...". URLs might either point to the
 * MCD server itself, but could also point to locations on other
 * servers.
 *
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_GENERAL if URL could not be provided.
 */
mcd_return_et mcd_qry_device_description_f(const mcd_core_st *core,
                                           char *url, uint32_t *url_length);


/**
 * mcd_qry_max_payload_size_f() - Function querying the maximum payload size
 *                                for a transaction list.
 *
 * @core:        [in]  A reference to the core the calling function addresses.
 * @max_payload: [out] Maximum (and optimum) supported payload size for a
 *               transaction list.
 *
 * Different systems will support a different maximum in transaction list
 * payload sizes. The payload is the net number of bytes that are read or
 * written. This function queries the maximum payload size for a transaction
 * list. Since a tool needs to be able to deal with smaller payload sizes, the
 * only reason to use larger payloads is an improved performance. In order to
 * achieve this performance, it is recommended that @max_payload is equal to the
 * payload allowing the optimum performance.
 * @max_payload then should be obeyed by the sent transaction lists.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_GENERAL on any other error.
 */
mcd_return_et mcd_qry_max_payload_size_f(const mcd_core_st *core,
                                         uint32_t *max_payload);


/**
 * mcd_qry_input_handle_f() - Function querying the input handle for the
 *                            connection.
 *
 * @core:         [in]  A reference to the core the calling function addresses.
 * @input_handle: [out] Input handle or -1 if handle not defined.
 *
 * Fast and efficient reaction on target system events with a single threaded
 * application requires that the application can wait for user input or
 * asynchronous activity from the target. Obtaining the handle used for the
 * communication to the target (usually a socket) allows the application to wait
 * for activity there without frequent polling. If the communication is not done
 * by sockets then there may be no such handle.
 *
 * Allowed error codes:
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_GENERAL on any other error.
 */
mcd_return_et mcd_qry_input_handle_f(const mcd_core_st *core,
                                     uint32_t *input_handle);


/**
 * DOC: Target Memory Access API
 *
 * Target Memory Access API functions are related to the configuration
 * of memories. They allow retrieving memory spaces for a particular component,
 * or memory blocks of a specified memory space.
 */


/**
 * mcd_qry_mem_spaces_f() - Function querying the available memory spaces for a
 *                          particular component.
 *
 * @core:        [in] A reference to the core the calling function addresses.
 * @start_index: [in] Start index of the requested memory spaces. This refers to
 *               an internal list of the target side implementation.
 * @num_mem_spaces:
 * * [in] Number of memory spaces, information is requested of. If it is set to
 *   '0', no memory space information is returned but the number of all
 *   available memory spaces for the selected core.
 * * [out] : The number of returned memory spaces. In case the input value of
 *   @num_mem_spaces is '0', this is the number of all available memory spaces
 *   for the selected core.
 * @mem_spaces: [out] Memory space information. This is an array allocated by
 *              the calling function.
 *
 * There can be various memory spaces visible to a core depending on its
 * architecture. For Harvard architectures these can be "program" and "data",
 * for DSP architecture these can be "P"/"X"/"Y", etc. This function queries all
 * memory spaces available for a particular target core.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE if successful.
 * - %MCD_ERR_NO_MEM_SPACES if no memory spaces are defined for this core.
 */
mcd_return_et mcd_qry_mem_spaces_f(const mcd_core_st *core,
                                   uint32_t start_index,
                                   uint32_t *num_mem_spaces,
                                   mcd_memspace_st *mem_spaces);

/**
 * mcd_qry_mem_blocks_f() - Function querying the available memory blocks of a
 *                          specified memory space.
 *
 * @core:          [in] A reference to the core the calling function addresses.
 * @mem_space_id:  [in] The ID of the memory space the calling function queries
 *                 the memory block information from.
 * @start_index:   [in] Start index of the requested memory blocks. This refers
 *                 to an internal list of the target side implementation.
 * @num_mem_blocks:
 * * [in] Number of memory blocks, information is requested of. If it is set to
 *   '0', no memory block information is returned but the number of all
 *   available memory blocks for the selected memory.
 * * [out] : Number of returned memory blocks. In case the input value of
 *   @num_mem_blocks is '0', this is the number of all available memory blocks
 *   for the selected memory space.
 * @mem_blocks:  [out] Memory block information. This is an array allocated by
 *               the calling function.
 *
 * There can be various memory blocks within a particular memory space of a
 * core. The memory blocks define the layout of the memory space. Memory blocks
 * can be hierarchical in nature, and this query function returns information
 * about all available memory blocks in the memory space. Memory blocks with the
 * same parent must not overlap. This call returns existing memory blocks only.
 * If a target side implementation supports memory block descriptions, the
 * calling function may assume that memory which does not belong to any memory
 * block is not addressable.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE          if successful.
 * - %MCD_ERR_NO_MEM_BLOCKS if no memory blocks are defined for this memory
 *   space.
 */
mcd_return_et mcd_qry_mem_blocks_f(const mcd_core_st *core,
                                   uint32_t mem_space_id, uint32_t start_index,
                                   uint32_t *num_mem_blocks,
                                   mcd_memblock_st *mem_blocks);

/**
 * mcd_qry_active_overlays_f() - Function querying the active (swapped-in)
 *                               overlays at the current time.
 *
 * @core:              [in] A reference to the core the calling function
 * addresses.
 * @start_index:       [in]  Start index of the requested active memory
 *                     overlays. This refers to an internal list of the target
 *                     side implementation.
 * @num_active_overlays:
 * * [in] Number of active memory overlays, information is requested of. If it
 *   is set to '0', no active memory overlay information is returned but the
 *   number of all available active memory overlays for the selected core.
 * * [out] Number of returned active memory overlays. In case the input value of
 *   @num_active_overlays is '0', this is the number of all available active
 *   memory overlays for the selected core.
 * @active_overlays:  [out] Active memory overlay information. This is an array
 *                    allocated by the calling function.
 *
 * This function is called when the caller wants to retrieve the list of active
 * memory overlays. This is typically done when a breakpoint is hit.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE     if successful.
 * - %MCD_ERR_OVERLAYS if retrieving active memory overlay information failed.
 */
mcd_return_et mcd_qry_active_overlays_f(const mcd_core_st *core,
                                        uint32_t start_index,
                                        uint32_t *num_active_overlays,
                                        uint32_t *active_overlays);


/**
 * DOC: Target Register Access API
 *
 * Target Register Access API functions are related to the access and
 * configuration of registers. Registers in an IP may be of the following two
 * types:
 *
 * - Internal IP registers: These registers are internal to an IP and cannot be
 *   accessed by other system components connected to the bus. Special means
 *   must be provided in order to make these registers visible to the external
 *   tools such as debugging and profiling tools. An example of a mechanism
 *   commonly used to expose such internal registers of an IP to external tools
 *   is the use of scan chains and an IP specific TAP controller whose data
 *   registers are mapped to a few of these internal registers. These registers
 *   must be accessed by the debugging and profiling tools using their ID, which
 *   need to be unique within the scope of a particular instance of an IP.
 *
 * - Memory Mapped registers: These registers are mapped to memory addresses
 *   which are an offset to a base address belonging to that IP. They can
 *   therefore be accessed via the bus infrastructure using common memory
 *   addressing mechanisms. These registers may be accessed by the debugging and
 *   profiling tools using their ID, which must be unique within the scope of a
 *   particular instance of an IP. Alternatively, they may be accessed by
 *   external tools using their memory mapped addresses via the memory bus.
 */


/**
 * mcd_qry_reg_groups_f() - Function querying the register groups defined for a
 *                          particular component.
 *
 * @core:         [in] A reference to the core the calling function addresses.
 * @start_index:  [in] Start index of the requested register groups. This refers
 *                to an internal list of the target side implementation.
 * @num_reg_groups:
 * * [in] Number of register groups, information is requested of. If it is set
 *   to '0', no register groups information is returned but the number of all
 *   available register groups for the selected core.
 * * [out] Number of returned register groups. In case the input value of
 *   @num_reg_groups is '0', this is the number of all available register groups
 *   for the selected core.
 * @reg_groups:  [out] Register group information. This is an array allocated by
 *               the calling function.
 *
 * There can be various register groups defined for a core depending on its
 * architecture. This function queries information about these register groups.
 *
 * The parameter @num_reg_groups is used as an input/output parameter.
 * As input parameter it is set to the desired number of register groups. As
 * output parameter it set to the actual number of register groups information
 * is returned for in @reg_groups.
 * If the target does not define any register groups, it is assumed that a
 * virtual register group with ID 0 exists which contains all registers of the
 * corresponding component. Then the information about this default 'virtual'
 * register group has to be sent back as only register group information.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE          if successful.
 * - %MCD_ERR_NO_REG_GROUPS if no register groups are defined for this core.
 */
mcd_return_et mcd_qry_reg_groups_f(const mcd_core_st *core,
                                   uint32_t start_index,
                                   uint32_t *num_reg_groups,
                                   mcd_register_group_st *reg_groups);


/**
 * mcd_qry_reg_map_f() - Function querying the register information of a
 *                       particular register group.
 *
 * @core:         [in] A reference to the core the calling function addresses.
 * @reg_group_id: [in] ID of the register group detailed register information is
 *                requested for.
 * @start_index:  [in] Start index of the requested registers. This refers to an
 *                internal list of the target side implementation.
 * @num_regs:
 * * [in] Number of registers, information is requested of. If it is set to '0',
 *   no register information is returned but the number of all available
 *   registers within for the selected register group.
 * * [out] Number of returned registers. In case the input value of @num_regs is
 *   '0', this is the number of all available register for the selected register
 *   group.
 * @reg_info:    [out] Register information. This is an array allocated by the
 *               calling function.
 *
 * There can be various register groups defined for a core depending on its
 * architecture. Within each register group there can be many registers. This
 * function allows the user to query information about the registers contained
 * within a register group. Information all registers which have to be exposed
 * to the debug environment have to be returned as a result of such a query.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE         if successful.
 * - %MCD_ERR_REG_GROUP_ID if no register group with this ID is available
 *   for this core.
 */
mcd_return_et mcd_qry_reg_map_f(const mcd_core_st *core, uint32_t reg_group_id,
                                uint32_t start_index, uint32_t *num_regs,
                                mcd_register_info_st *reg_info);


/**
 * mcd_qry_reg_compound_f() - Function querying the component registers of a
 *                            compound register
 *
 * @core:            [in] A reference to the core the calling function
 *                   addresses.
 * @compound_reg_id: [in] ID of the compound register component register IDs are
 *                   queried for.
 * @start_index:     [in] Start index of the requested component registers. This
 *                   refers to an internal list of the target side
 *                   implementation.
 * @num_reg_ids:
 * * [in] Number of component registers the ID is requested of. If it is set to
 *   '0', no component register IDs are returned but the number of all available
 *   component register for the selected compound register.
 * * [out] : Number of returned component registers. In case the input value of
 *   @num_reg_ids is '0', this is the number of all available component
 *   registers for the selected compound register.
 * @reg_id_array:   [out] Component register IDs. This is an array allocated by
 *                  the calling function.
 *
 * Registers within a target component may be composed of several simple
 * registers. These are by definition called "compound registers". This function
 * allows a user to query information about the registers contained within a
 * particular compound register.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE             if successful.
 * - %MCD_ERR_REG_NOT_COMPOUND if no compound register with this ID is available
 *   for this core.
 */
mcd_return_et mcd_qry_reg_compound_f(const mcd_core_st *core,
                                     uint32_t compound_reg_id,
                                     uint32_t start_index,
                                     uint32_t *num_reg_ids,
                                     uint32_t *reg_id_array);


/**
 * DOC: Target Trigger Setup API
 *
 * Target Trigger Setup API functions allow management of triggers, such as
 * creation, activation, deletion or trigger status inquiry. Typical triggers
 * are breakpoints, but the API allows definition of complex triggers, as well
 * as complex trigger conditions. Triggers can be managed individually but also
 * as a trigger set defined for a core.
 */


/**
 * mcd_qry_trig_info_f() - Function querying information about trigger
 *                          capabilities.
 *
 * @core:       [in] A reference to the core the calling function addresses.
 * @trig_info: [out] Information about supported triggers.
 *
 * This function queries information about trigger capabilities implemented
 * in a target.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_GENERAL if trigger capabilities could not be retrieved.
 */
mcd_return_et mcd_qry_trig_info_f(const mcd_core_st *core,
                                  mcd_trig_info_st *trig_info);


/**
 * mcd_qry_ctrigs_f() - Function querying information about custom triggers.
 *
 * @core:        [in] A reference to the core the calling function addresses.
 * @start_index: [in] Start index of the requested custom triggers. This refers
 *               to an internal list of the target side implementation.
 * @num_ctrigs:
 * * [in] Number of custom triggers, information is requested of. If it is set
 *   to '0', no custom trigger information is returned but the number of all
 *   available custom triggers for the selected core.
 * * [out] : Number of returned custom triggers. In case the input value of
 *   @num_ctrigs is '0', this is the number of all available custom triggers for
 *   the selected core.
 * @ctrig_info: [out] Custom trigger information. This is an array allocated by
 *              the calling function.
 *
 * This function queries information about custom triggers of a component as
 * well as the number of available custom triggers.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE   if successful.
 * - %MCD_ERR_PARAM  if custom trigger ID does not exist.
 */
mcd_return_et mcd_qry_ctrigs_f(const mcd_core_st *core, uint32_t start_index,
                               uint32_t *num_ctrigs,
                               mcd_ctrig_info_st *ctrig_info);

/**
 * mcd_create_trig_f() - Function allowing the creation of a new trigger.
 *
 * @core:     [in] A reference to the core the calling function addresses.
 * @trig:
 * * [in] Pointer to the structure containing information about the trigger
 *   object to be created.
 * * [out] : Members of the structure may be modified by the function. In this
 *   case the modified member of the trigger structure as well as the modified
 *   members are set.
 * @trig_id: [out] Unique ID for the newly created trigger returned by the API
 *           implementation. A value of '0' indicates that the breakpoint is
 *           set, but cannot be identified by an ID. Removing such breakpoints
 *           is only possible by calling mcd_remove_trig_set_f().
 *
 * This function allows a user to create a new trigger. If the exact trigger
 * cannot be created, an approximate trigger is created instead and the modified
 * member of the trigger structure is set.
 *
 * The void pointer @trig usually points to a standard trigger structure like
 * mcd_trig_simple_core_st or mcd_trig_complex_core_st.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE        if successful.
 * - %MCD_ERR_TRIG_CREATE if trigger could not be created.
 */
mcd_return_et mcd_create_trig_f(const mcd_core_st *core, void *trig,
                                uint32_t *trig_id);


/**
 * mcd_qry_trig_f() - Function querying the contents of a trigger.
 *
 * @core:          [in] A reference to the core the calling function addresses.
 * @trig_id:       [in] ID of the trigger the user queries.
 * @max_trig_size: [in] Maximum size of the structure in bytes as expected by
 *                 the calling function.
 * @trig:         [out] Pointer to the structure receiving the information about
 *                the trigger object. The structure is allocated by the calling
 *                function.
 *
 * This function allows the user to query the contents of a trigger.
 * The @max_trig_size parameter is set to the maximum size of
 * the trigger structure the user expects in bytes.
 *
 * The void pointer trig usually points to a standard trigger
 * structure like mcd_trig_simple_core_st or mcd_trig_complex_core_st.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE            if successful.
 * - %MCD_ERR_RESULT_TOO_LONG if requested trigger is larger than
 *   @max_trig_size.
 * - %MCD_ERR_TRIG_ACCESS     if trigger could not be returned for any other
 *   reason.
 */
mcd_return_et mcd_qry_trig_f(const mcd_core_st *core, uint32_t trig_id,
                             uint32_t max_trig_size, void *trig);


/**
 * mcd_remove_trig_f() - Function allowing a user to delete a particular trigger
 *                       from a trigger set.
 *
 * @core:    [in] A reference to the core the calling function addresses.
 * @trig_id: [in] ID of the trigger the user wants to delete.
 *
 * This function allows the user to delete a particular trigger from a trigger
 * set.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE        if successful.
 * - %MCD_ERR_TRIG_ACCESS if trigger could not be accessed for deletion.
 */
mcd_return_et mcd_remove_trig_f(const mcd_core_st *core, uint32_t trig_id);


/**
 * mcd_qry_trig_state_f() - Function allowing a user to query the trigger states
 *                          from the target.
 *
 * @core:        [in] A reference to the core the calling function addresses.
 * @trig_id:     [in] ID of the trigger, the tool queries the state for.
 * @trig_state: [out] Queried Trigger state. The structure is allocated by the
 *              calling function.
 *
 * This function allows a user to query the status of a single trigger.
 * Note that mcd_qry_trig_set_state_f() needs to be called before to sample
 * the trigger state.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE        if successful.
 * - %MCD_ERR_TRIG_ACCESS if trigger could not be accessed.
 */
mcd_return_et mcd_qry_trig_state_f(const mcd_core_st *core, uint32_t trig_id,
                                   mcd_trig_state_st *trig_state);


/**
 * mcd_activate_trig_set_f() - Function allowing a user to activate a trigger
 *                             set on the target.
 *
 * @core: [in] A reference to the core the calling function addresses.
 *
 * This function downloads the current trigger set to the hardware in order to
 * activate it. If the trigger set is unchanged since the last call of this
 * function, it will just arm the triggers again.
 *
 * This function is only needed to activate triggers on the fly (while the
 * target is running) and in a consistent way - if supported by the target.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE        if successful.
 * - %MCD_ERR_TRIG_ACCESS if trigger set could not be activated.
 */
mcd_return_et mcd_activate_trig_set_f(const mcd_core_st *core);


/**
 * mcd_remove_trig_set_f() -  Function allowing a user to delete a trigger set.
 *
 * @core: [in] A reference to the core the calling function addresses.
 *
 * This function allows a user to delete a trigger set for a particular core.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE        if successful.
 * - %MCD_ERR_TRIG_ACCESS if trigger set could not be removed.
 */
mcd_return_et mcd_remove_trig_set_f(const mcd_core_st *core);


/**
 * mcd_qry_trig_set_f() - Function querying the contents of a trigger set.
 *
 * @core:        [in] A reference to the core the calling function addresses.
 * @start_index: [in] Start index of the requested triggers. This refers to an
 *                    internal list of the target side implementation.
 * @num_trigs:
 * * [in] The number of queried triggers starting from the defined
 *   @start_index. If it is set to '0', no triggers are returned but the number
 *   of all available triggers of the trigger set.
 * * [out] The number of returned triggers. In case the input value of
 *    @num_trigs is '0', this is the number of all available triggers of this
 *    core's trigger set.
 * @trig_ids:    [out] List of trigger IDs set in the target. This is an array
 *                allocated by the calling function.
 *
 * This function queries information about the current state of the trigger set
 * of a target core.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE        if successful.
 * - %MCD_ERR_TRIG_ACCESS if trigger set could not be queried.
 */
mcd_return_et mcd_qry_trig_set_f(const mcd_core_st *core, uint32_t start_index,
                                 uint32_t *num_trigs, uint32_t *trig_ids);


/**
 * mcd_qry_trig_set_state_f() -  Function querying the state of a trigger set.
 *
 * @core:       [in]  A reference to the core the calling function addresses.
 * @trig_state: [out] Information about the current state of the trigger set.
 *
 * This function queries information about the current state of the trigger set
 * of a target core. It will consistently sample the state of all triggers in
 * the set. This is in particular necessary for Real HW targets. The individual
 * triggers can then be queried with \ref mcd_qry_trig_state_f().
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE        if successful.
 * - %MCD_ERR_TRIG_ACCESS if state of the trigger set could not be queried.
 */
mcd_return_et mcd_qry_trig_set_state_f(const mcd_core_st *core,
                                       mcd_trig_set_state_st *trig_state);


/**
 * DOC: Target Execution Control API
 *
 * Target Execution Control API functions allow control of the execution, such
 * as run, stop and step. They allow querying the state of a core as well as the
 * execution time of the target. The API also allows execution of commands
 * grouped as transaction lists.
 *
 */


/**
 * mcd_execute_txlist_f() - Function executing a transaction list on the target.
 *
 * @core:   [in] A reference to the core the calling function addresses.
 * @txlist: [in] A pointer to the transaction list for execution.
 *
 * This function sends a transaction list to the target for execution and
 * retrieves the result. It is blocking, so it is the responsibility of the tool
 * to make sure that the execution time will be reasonable by creating a
 * transaction list with an appropriate payload size.
 *
 * Note that multiple tools can issue transaction lists requests to the same
 * core at the same time.
 *
 * In case of an error, the execution of the transaction list is immediately
 * aborted.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE         if successful.
 * - %MCD_ERR_TXLIST_READ  if execution of the transaction list aborted due to a
 *   specific read access.
 * - %MCD_ERR_TXLIST_WRITE if execution of the transaction list aborted due to a
 *   specific write access.
 * - %MCD_ERR_TXLIST_TX    if execution of the transaction list aborted due to
 *   any other reason.
 */
mcd_return_et mcd_execute_txlist_f(const mcd_core_st *core,
                                   mcd_txlist_st *txlist);


/**
 * mcd_run_f() - Function starting execution on a particular core.
 *
 * @core:   [in] A reference to the core the calling function addresses.
 * @global: [in] Set to "TRUE" if all cores of a system shall start execution.
 *          Otherwise, starting execution of selected core only.
 *
 * This function causes the corresponding target core to begin execution.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_GENERAL if starting execution failed.
 */
mcd_return_et mcd_run_f(const mcd_core_st *core, bool global);


/**
 * mcd_stop_f() - Function stopping execution on a particular core.
 *
 * @core:   [in] A reference to the core the calling function addresses.
 * @global: [in] Set to "TRUE" if all cores of a system shall stop
 *          execution. Otherwise, stopping execution of selected
 *          core only.
 *
 * This function causes the corresponding target core to stop execution.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_GENERAL if stopping execution failed.
 */
mcd_return_et mcd_stop_f(const mcd_core_st *core, bool global);


/**
 * mcd_stop_f() - Function running a particular core until a defined time.
 *
 * @core:           [in] A reference to the core the calling function addresses.
 * @global:         [in] Set to "TRUE" if all cores of a system shall start
 *                  execution. Otherwise, starting execution of selected core
 *                  only.
 * @absolute_time:  [in] Boolean value indicating whether the time parameter is
 *                  absolute or not.
 * @run_until_time: [in] The number of time units (picoseconds) until which the
 *                  target core shall run.
 *
 * This function causes the corresponding target core to run for a defined time
 * before it stops. If @absolute_time is "FALSE", @run_until_time is the value
 * of the system timer that is associated with this core. This means it starts
 * again from '0' for certain reset types, and it needs to be scaled depending
 * on the crystal and PLL settings in order to determine a time value. If
 * @absolute_time is "TRUE", @run_until_time is an absolute time in seconds.
 * Usually, a simulation model can only support this case.
 *
 * Allowed error codes:
 *
 *  - %MCD_ERR_NONE      if successful.
 *  - %MCD_ERR_GENERAL   if execution failed.
 */
mcd_return_et mcd_run_until_f(const mcd_core_st *core, bool global,
                              bool absolute_time, uint64_t run_until_time);

/**
 * mcd_qry_current_time_f() - Function querying the current time of execution
 *                            from the target system.
 *
 * @core:         [in]  A reference to the core the calling function addresses.
 * @current_time: [out] The current number of time units (picoseconds) the
 *                target system has been running.
 *
 * This function returns the current execution time of the target.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_GENERAL if querying for the time failed.
 */
mcd_return_et mcd_qry_current_time_f(const mcd_core_st *core,
                                     uint64_t *current_time);

/**
 * mcd_step_f() - Function stepping a target core based on the particular step
 *                type.
 *
 * @core:      [in] A reference to the core the calling function addresses.
 * @global:    [in] Set to "TRUE" if all cores of a system shall start
 *             execution. Otherwise, starting execution of selected core only.
 * @step_type: [in] The unit, the stepping of the target core is based on.
 * @n_steps:   [in] The number of steps, the target core is stepped for.
 *
 * This function causes the corresponding target core to step based on the
 * provided step type.
 *
 * Note that the function is blocking. It is the responsibility of the tool to
 * call it with a reasonable number of steps.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_GENERAL if stepping the target core failed.
 */
mcd_return_et mcd_step_f(const mcd_core_st *core, bool global,
                         mcd_core_step_type_et step_type, uint32_t n_steps);


/**
 * mcd_set_global_f() - Function enabling/disabling global stop and run
 *                      activities on this core.
 *
 * @core:   [in] A reference to the core the calling function addresses.
 * @enable: [in] Set to "TRUE" if this core should perform global run or stop
 *          activities.
 *
 * This function enables or disables the effect of a global run and stop on this
 * core. The default state is target specific.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.\n
 * - %MCD_ERR_GENERAL if enabling/disabling the global effect of execution
 *   functions failed.
 */
mcd_return_et mcd_set_global_f(const mcd_core_st *core, bool enable);


/**
 * mcd_qry_state_f() - Function querying the execution state of a target core.
 *
 * @core:   [in]  A reference to the core the calling function addresses.
 * @state: [out] The current execution state of the target core.
 *
 * This function queries the current execution state of a particular target
 * core.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.\n
 * - %MCD_ERR_GENERAL if querying the execution state failed.
 */
mcd_return_et mcd_qry_state_f(const mcd_core_st *core,
                              mcd_core_state_st *state);


/**
 * mcd_execute_command_f() - Function executing a command on the target
 *                           platform.
 *
 * @core:               [in] A reference to the core the calling function
 *                      addresses.
 * @command_string:     [in] The command string. This is implementation
 *                      specific.
 * @result_string_size: [in] The maximum size of the result string.
 * @result_string:      [out] The result string allocated by the calling
 *                      function.
 *
 * This function sends a command to the target platform and retrieves the result
 * in the form of a string.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_GENERAL if executing the command failed.
 */
mcd_return_et mcd_execute_command_f(const mcd_core_st *core,
                                    const char *command_string,
                                    uint32_t result_string_size,
                                    char *result_string);


/**
 * DOC: Reset Control API
 *
 * Reset Control API functions allow querying information about the reset
 * classes supported by the target system, as well as triggering one or more
 * reset signals in parallel on the target system.
 *
 */


/**
 * mcd_qry_rst_classes_f() - Function querying information about reset classes
 *                           supported by the target system.
 * @core:             [in]  A reference to the core the calling function
 *                    addresses.
 * @rst_class_vector: [out] A 32 bit vector that defines the available reset
 *                    classes.
 *
 * This function queries all available reset classes of the target system. Each
 * bit of @rst_class_vector represents an available reset class.
 *
 * It is recommended that the strongest reset (e.g. power-on reset) is of class
 * '0'.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_GENERAL if querying the reset classes failed.
 */
mcd_return_et mcd_qry_rst_classes_f(const mcd_core_st *core,
                                    uint32_t *rst_class_vector);


/**
 * mcd_qry_rst_class_info_f() - Function querying information about a particular
 *                              reset class supported by the target system.
 *
 * @core:      [in]  A reference to the core the calling function addresses.
 * @rst_class: [in]  Reset class ID which refers to a bit in the 32-bit reset
 *             class vector as obtained by mcd_qry_rst_classes_f().
 * @rst_info:  [out] Reference to an object of type mcd_rst_info_st containing
 *             detailed information about this reset class.
 *
 * This function queries more detailed information about a particular reset
 * class of the target system.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_PARAM   if reset class does not exist.
 * - %MCD_ERR_GENERAL if any other error occurred.
 */
mcd_return_et mcd_qry_rst_class_info_f(const mcd_core_st *core,
                                       uint8_t rst_class,
                                       mcd_rst_info_st *rst_info);


/**
 * mcd_rst_f() - Function triggering one or more reset signals in parallel on
 * the target system.
 *
 * @core:             [in] A reference to the core the calling function
 *                     addresses.
 * @rst_class_vector: [in] Reset vector specifying the resets which shall be
 *                     issued.
 * @rst_and_halt:     [in] Optionally halting the core if the reset changes the
 *                    core state.
 *
 * This function triggers one or more reset signals in parallel on the target
 * system.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_PARAM   if one or several reset classes do not exist.
 * - %MCD_ERR_GENERAL if any other error occurred.
 */
mcd_return_et mcd_rst_f(const mcd_core_st *core, uint32_t rst_class_vector,
                        bool rst_and_halt);


/**
 * DOC: Communication Channel API
 *
 * Communication channels allow the exchange of data between a tool and the
 * target. A channel requested by the tool is specified by mcd_chl_st. This
 * structure type contains information on the channel type and its attributes.
 * The number of channels for each server of the MCD API is limited to
 * MCD_CHL_NUM_MAX. A channel can be both uni- and bi-directional. It also may
 * be memory-mapped and prioritized. In the latter case, the channel priority
 * determines the sequence of communication transfers between the target and the
 * connected tools.
 *
 * Note that there must never be two or more open channels of the same priority
 * to a single target. In case of conflicts the channel will get the closest
 * free priority.
 */


/**
 * mcd_chl_open_f() - Function opening a communication channel between the host
 *                    tool and the target.
 *
 * @core: [in] A reference to the targeted system, device or core. Here, member
 *        instance is allowed to be a null pointer for levels higher than core
 *        level.
 * @channel:
 * * [in] Requested channel setup.
 * * [out] Accepted and at least for chl_id modified channel setup. Note that
 *   max_msg_len and prio can be changed as well if the requested values are not
 *   possible.
 *
 * This function opens a defined communication channel between a host side tool
 * and a target. The addressed target is identified by a core reference. This
 * function call allows to establish a communication channel between the host
 * side tool and any hierarchical level of the targeted system (i.e. at system
 * level, at device level or at core level). For this reason, this function call
 * accepts core structures which have their member instance set to a null
 * pointer for levels higher than core level. The target implementation actually
 * needs to determine the targeted hierarchical level based on the member
 * core_con_info of the core structure. The established channel is described by
 * channel. Only a single debugger may be attached to a communication channel at
 * a time.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE                     if successful.
 * - %MCD_ERR_CHL_TYPE_NOT_SUPPORTED   if unsupported channel type was
 *   requested.
 * - %MCD_ERR_CHL_TARGET_NOT_SUPPORTED if addressed target does not support
 *   communication channels.
 * - %MCD_ERR_CHL_SETUP                if channel setup is invalid or contains
 *   unsupported attributes.
 */
mcd_return_et mcd_chl_open_f(const mcd_core_st *core, mcd_chl_st *channel);


/**
 * mcd_send_msg_f() - Function send a message using a specified communication
 *                    channel.
 *
 * @core:    [in] A reference to the targeted system, device or core. Here,
 *           member instance is allowed to be a null pointer for levels higher
 *           than core level.
 * @channel: [in] Description of the addressed communication channel.
 * @msg_len: [in] The number of bytes sent with this message.
 * @msg:     [in] Message buffer.
 *
 * This function sends a message using a defined communication channel between
 * the host and the target.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE               if successful.
 * - %MCD_ERR_CHL_MESSAGE_FAILED if sending the message failed.
 */
mcd_return_et mcd_send_msg_f(const mcd_core_st *core, const mcd_chl_st *channel,
                             uint32_t msg_len, const uint8_t *msg);


/**
 * mcd_receive_msg_f() - Function receiving a message using a specified
 *                       communication channel.
 *
 * @core:    [in] A reference to the targeted system, device or core. Here, for
 *           member instance is allowed to be a null pointer for levels higher
 *           than core level.
 * @channel: [in]  Description of the addressed communication channel.
 * @timeout: [in]  Number of time units (milliseconds) until function call times
 *           out.
 * @msg_len: [in]  Maximum number of bytes that can be fetched with this call.
 * @msg_len: [out] Number of bytes that have been actually fetched with this
 *           call.
 * @msg:     [out] Message buffer.
 *
 * This function receives a message using a defined communication channel
 * between the host and the target.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE               if successful.
 * - %MCD_ERR_CHL_MESSAGE_FAILED if receiving of the message failed.
 */
mcd_return_et mcd_receive_msg_f(const mcd_core_st *core,
                                const mcd_chl_st *channel, uint32_t timeout,
                                uint32_t *msg_len, uint8_t *msg);

/**
 * mcd_chl_reset_f() - Function resetting a specified communication channel.
 *
 * @core:    [in] : A reference to the targeted system, device or core. Here,
 *           member instance is allowed to be a null pointer for levels higher
 *           than core level.
 * @channel: [in] : Description of the addressed communication channel.
 *
 * This function resets a communication channel between the host and the target.
 * This allows the communication to be setup again e.g. if the communication
 * hangs.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_GENERAL if resetting the communication channel failed.
 */
mcd_return_et mcd_chl_reset_f(const mcd_core_st *core,
                              const mcd_chl_st *channel);


/**
 * mcd_chl_close_f() - Function closing a specified communication channel.
 *
 * @core:    [in] : A reference to the targeted system, device or core.
 *           Here, member instance is allowed to be a null
 *           pointer for levels higher than core level.
 * @channel: [in] : Description of the addressed communication channel.
 *
 * This function closes a communication channel between the host and the target.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_GENERAL if closing the communication channel failed.
 */
mcd_return_et mcd_chl_close_f(const mcd_core_st *core,
                              const mcd_chl_st *channel);


/**
 * DOC: Trace API
 *
 * Traces allow information to be captured from a running target system or
 * platform. A target may contain different trace sources and sinks. A trace
 * source is generating trace data (e.g. a core trace or a bus trace unit),
 * whereas a trace sink is storing the trace data until it is retrieved via the
 * MCD API (e.g. an on-chip or off-chip trace buffer). The MCD API does not
 * differentiate between source and sink. Consequently, there needs to be a
 * "Trace" for each combination.
 */


/**
 * mcd_qry_traces_f() - Function querying information about available traces for
 *                      a core.
 *
 * @core:        [in]  A reference to the core of which the traces are
 *               requested.
 * @start_index: [in]  Start index of the requested traces. This refers to an
 *               internal list of the target side implementation.
 * @num_traces:
 * * [in] The number of queried traces starting from the defined
 *   @start_index. If it is set to '0', no traces are returned but the number of
 *   all available traces.
 * * [out] The number of returned traces. In case the input value of
 *   @num_traces is '0', this is the number of all available traces for the
 *   selected core.
 * @trace_info:  [out] Trace information of the requested traces. This is an
 *                array allocated by the calling function.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_PARAM   if @trace_index is equal or larger than the number of
 *   traces.
 * - %MCD_ERR_GENERAL on any other error.
 */
mcd_return_et mcd_qry_traces_f(const mcd_core_st *core, uint32_t start_index,
                               uint32_t *num_traces,
                               mcd_trace_info_st *trace_info);


/**
 * mcd_qry_trace_state_f() - Function querying the status of a trace.
 *
 * @core:     [in]  A reference to the core to which the trace belongs.
 * @trace_id: [in]  ID to which this trace refers to.
 * @state:    [out] The current state of the trace.
 *
 * This function queries the current status of a particular trace source.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_PARAM   if @trace_id is not a valid trace ID.
 * - %MCD_ERR_GENERAL on any other error.
 */
mcd_return_et mcd_qry_trace_state_f(const mcd_core_st *core, uint32_t trace_id,
                                    mcd_trace_state_st *state);


/**
 * mcd_set_trace_state_f() - Function setting the state and mode of a trace.
 *
 * This function sets the state and mode of a particular trace source.
 *
 * @core:     [in] A reference to the core to which the trace belongs.
 * @trace_id: [in] ID of the trace which is referenced.
 * @state:
 * * [in] The trace settings to be applied.
 * * [out] Returns the current state of the trace. Member @modified is set if a
 *   member has changed.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_PARAM   if @trace_id is not a valid trace ID.
 * - %MCD_ERR_GENERAL on any other error.
 */
mcd_return_et mcd_set_trace_state_f(const mcd_core_st *core, uint32_t trace_id,
                                    mcd_trace_state_st *state);


/**
 * mcd_read_trace_f () - Function reading trace data from a trace.
 *
 * @core:        [in] A reference to the core to which the trace belongs.
 * @trace_id:    [in] ID of the trace which is referenced.
 * @start_index: [in] Start index of frame to read (0 = oldest frame). This
 *                    refers to an internal list of the target implementation
 *                    which stores the trace frames.
 * @num_frames:
 * * [in] The number of queried trace frames starting from the defined
 *   @start_index. If it is set to '0', no trace data is returned but the number
 *   of all currently available trace frames.
 * * [out] The number of read trace frames. In case the input value of
 *   @num_frames is '0', this is the number of all currently available trace
 *   frames.
 * @trace_data_size: [in]  Size of one trace data frame in bytes (for type
 *                   checking).
 * @trace_data:      [out] Array of trace data structures filled by this
 *                   function. The format depends on the trace source. Standard
 *                   formats are mcd_trace_data_core_st, mcd_trace_data_event_st
 *                   and mcd_trace_data_stat_st.
 *
 * This function reads trace data from a particular trace source.
 *
 * Allowed error codes:
 *
 * - %MCD_ERR_NONE    if successful.
 * - %MCD_ERR_PARAM   if @trace_id is not a valid trace ID, or if start_index is
 *   larger than the number of available trace frames.
 * - %MCD_ERR_GENERAL on any other error.
 */
mcd_return_et mcd_read_trace_f(const mcd_core_st *core, uint32_t trace_id,
                               uint64_t start_index, uint32_t *num_frames,
                               uint32_t trace_data_size, void *trace_data);


#endif /* MCD_API_H */
