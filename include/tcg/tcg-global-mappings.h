/*
 *  Copyright(c) 2026 rev.ng Labs Srl. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TCG_GLOBAL_MAP_H
#define TCG_GLOBAL_MAP_H

/**
 * cpu_tcg_mapping: Declarative mapping of offsets into a struct to global
 *                  TCGvs.  Parseable by LLVM-based tools.
 * @tcg_var_name: String name of the TCGv to use as destination of the mapping.
 * @tcg_var_base_address: Address of the above TCGv.
 * @cpu_type_name: String name of the base type being mapped, e.g. "CPUArchState".
 * @cpu_var_names: Array of printable names of TCGvs, used when calling
 *                 tcg_global_mem_new from init_cpu_tcg_mappings.  Contains
 *                 @number_of_elements strings.
 * @cpu_var_base_offset: Base offset of field in the source struct.
 * @cpu_var_size: Size of field in the source struct, if the field is an array,
 *                this holds the size of the element type.
 * @cpu_var_stride: Stride between array elements in the source struct.  This
 *                  can be greater than the element size when mapping a field
 *                  in an array of structs.
 * @number_of_elements: Number of elements of array in the source struct.
 */
typedef struct cpu_tcg_mapping {
    const char *tcg_var_name;
    void *tcg_var_base_address;

    const char *cpu_type_name;
    const char *const *cpu_var_names;
    size_t cpu_var_base_offset;
    size_t cpu_var_size;
    size_t cpu_var_stride;

    size_t number_of_elements;
} cpu_tcg_mapping;

#define STRUCT_SIZEOF_FIELD(S, member) sizeof(((S *)0)->member)

#define STRUCT_ARRAY_SIZE(S, array)                                            \
    (STRUCT_SIZEOF_FIELD(S, array) / STRUCT_SIZEOF_FIELD(S, array[0]))

/*
 * Following are a few macros that aid in constructing
 * `cpu_tcg_mapping`s for a few common cases.
 */

/* Map between single CPU register and to TCG global */
#define CPU_TCG_MAP(struct_type, tcg_var, cpu_var)                             \
    (cpu_tcg_mapping)                                                          \
    {                                                                          \
        .tcg_var_name = stringify(tcg_var),                                    \
        .tcg_var_base_address = &tcg_var,                                      \
        .cpu_type_name = stringify(struct_type),                               \
        .cpu_var_names = (const char *[]){stringify(cpu_var)},                 \
        .cpu_var_base_offset = offsetof(struct_type, cpu_var),                 \
        .cpu_var_size = STRUCT_SIZEOF_FIELD(struct_type, cpu_var),             \
        .cpu_var_stride = 0, .number_of_elements = 1,                          \
    }

/* Map between array of CPU registers and array of TCG globals. */
#define CPU_TCG_MAP_ARRAY(struct_type, tcg_var, cpu_var, names)                \
    (cpu_tcg_mapping)                                                          \
    {                                                                          \
        .tcg_var_name = stringify(tcg_var),                                    \
        .tcg_var_base_address = tcg_var,                                       \
        .cpu_type_name = stringify(struct_type),                               \
        .cpu_var_names = names,                                                \
        .cpu_var_base_offset = offsetof(struct_type, cpu_var),                 \
        .cpu_var_size = STRUCT_SIZEOF_FIELD(struct_type, cpu_var[0]),          \
        .cpu_var_stride = STRUCT_SIZEOF_FIELD(struct_type, cpu_var[0]),        \
        .number_of_elements = STRUCT_ARRAY_SIZE(struct_type, cpu_var),         \
    }

/*
 * Map between single member in an array of structs to an array
 * of TCG globals, e.g. maps
 *
 *     cpu_state.array_of_structs[i].member
 *
 * to
 *
 *     tcg_global_member[i]
 */
#define CPU_TCG_MAP_ARRAY_OF_STRUCTS(struct_type, tcg_var, cpu_struct,         \
                                     cpu_var, names)                           \
    (cpu_tcg_mapping)                                                          \
    {                                                                          \
        .tcg_var_name = stringify(tcg_var),                                    \
        .tcg_var_base_address = tcg_var,                                       \
        .cpu_type_name = stringify(struct_type),                               \
        .cpu_var_names = names,                                                \
        .cpu_var_base_offset = offsetof(struct_type, cpu_struct[0].cpu_var),   \
        .cpu_var_size =                                                        \
            STRUCT_SIZEOF_FIELD(struct_type, cpu_struct[0].cpu_var),           \
        .cpu_var_stride = STRUCT_SIZEOF_FIELD(struct_type, cpu_struct[0]),     \
        .number_of_elements = STRUCT_ARRAY_SIZE(struct_type, cpu_struct),      \
    }

extern cpu_tcg_mapping tcg_global_mappings[];
extern size_t tcg_global_mapping_count;

void init_cpu_tcg_mappings(cpu_tcg_mapping *mappings, size_t size);

#endif /* TCG_GLOBAL_MAP_H */
