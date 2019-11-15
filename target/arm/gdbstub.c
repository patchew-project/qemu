/*
 * ARM gdb server stub
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2013 SUSE LINUX Products GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/gdbstub.h"

typedef struct RegisterSysregXmlParam {
    CPUState *cs;
    GString *s;
    int n;
} RegisterSysregXmlParam;

/* Old gdb always expect FPA registers.  Newer (xml-aware) gdb only expect
   whatever the target description contains.  Due to a historical mishap
   the FPA registers appear in between core integer regs and the CPSR.
   We hack round this by giving the FPA regs zero size when talking to a
   newer gdb.  */

int arm_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    if (n < 16) {
        /* Core integer register.  */
        return gdb_get_reg32(mem_buf, env->regs[n]);
    }
    if (n < 24) {
        /* FPA registers.  */
        if (gdb_has_xml) {
            return 0;
        }
        memset(mem_buf, 0, 12);
        return 12;
    }
    switch (n) {
    case 24:
        /* FPA status register.  */
        if (gdb_has_xml) {
            return 0;
        }
        return gdb_get_reg32(mem_buf, 0);
    case 25:
        /* CPSR */
        return gdb_get_reg32(mem_buf, cpsr_read(env));
    }
    /* Unknown register.  */
    return 0;
}

int arm_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint32_t tmp;

    tmp = ldl_p(mem_buf);

    /* Mask out low bit of PC to workaround gdb bugs.  This will probably
       cause problems if we ever implement the Jazelle DBX extensions.  */
    if (n == 15) {
        tmp &= ~1;
    }

    if (n < 16) {
        /* Core integer register.  */
        env->regs[n] = tmp;
        return 4;
    }
    if (n < 24) { /* 16-23 */
        /* FPA registers (ignored).  */
        if (gdb_has_xml) {
            return 0;
        }
        return 12;
    }
    switch (n) {
    case 24:
        /* FPA status register (ignored).  */
        if (gdb_has_xml) {
            return 0;
        }
        return 4;
    case 25:
        /* CPSR */
        cpsr_write(env, tmp, 0xffffffff, CPSRWriteByGDBStub);
        return 4;
    }
    /* Unknown register.  */
    return 0;
}

static void arm_gen_one_xml_sysreg_tag(GString *s, DynamicGDBXMLInfo *dyn_xml,
                                       ARMCPRegInfo *ri, uint32_t ri_key,
                                       int bitsize, int regnum)
{
    g_string_append_printf(s, "<reg name=\"%s\"", ri->name);
    g_string_append_printf(s, " bitsize=\"%d\"", bitsize);
    g_string_append_printf(s, " regnum=\"%d\"", regnum);
    g_string_append_printf(s, " group=\"cp_regs\"/>");
    dyn_xml->data.cpregs.keys[dyn_xml->num] = ri_key;
    dyn_xml->num++;
}

static void arm_register_sysreg_for_xml(gpointer key, gpointer value,
                                        gpointer p)
{
    uint32_t ri_key = *(uint32_t *)key;
    ARMCPRegInfo *ri = value;
    RegisterSysregXmlParam *param = (RegisterSysregXmlParam *)p;
    GString *s = param->s;
    int n = param->n;
    ARMCPU *cpu = ARM_CPU(param->cs);
    CPUARMState *env = &cpu->env;
    DynamicGDBXMLInfo *dyn_xml = &cpu->dyn_sysreg_xml;

    if (!(ri->type & (ARM_CP_NO_RAW | ARM_CP_NO_GDB))) {
        if (arm_feature(env, ARM_FEATURE_AARCH64)) {
            if (ri->state == ARM_CP_STATE_AA64) {
                arm_gen_one_xml_sysreg_tag(s , dyn_xml, ri, ri_key, 64, n);
            }
        } else {
            if (ri->state == ARM_CP_STATE_AA32) {
                if (!arm_feature(env, ARM_FEATURE_EL3) &&
                    (ri->secure & ARM_CP_SECSTATE_S)) {
                    return;
                }
                if (ri->type & ARM_CP_64BIT) {
                    arm_gen_one_xml_sysreg_tag(s , dyn_xml, ri, ri_key, 64, n);
                } else {
                    arm_gen_one_xml_sysreg_tag(s , dyn_xml, ri, ri_key, 32, n);
                }
            }
        }
    }
    param->n++;
}

int arm_gen_dynamic_sysreg_xml(CPUState *cs, int base_reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    GString *s = g_string_new(NULL);
    RegisterSysregXmlParam param = {cs, s, base_reg};

    cpu->dyn_sysreg_xml.num = 0;
    cpu->dyn_sysreg_xml.data.cpregs.keys = g_new(uint32_t, g_hash_table_size(cpu->cp_regs));
    g_string_printf(s, "<?xml version=\"1.0\"?>");
    g_string_append_printf(s, "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">");
    g_string_append_printf(s, "<feature name=\"org.qemu.gdb.arm.sys.regs\">");
    g_hash_table_foreach(cpu->cp_regs, arm_register_sysreg_for_xml, &param);
    g_string_append_printf(s, "</feature>");
    cpu->dyn_sysreg_xml.desc = g_string_free(s, false);
    return cpu->dyn_sysreg_xml.num;
}

struct TypeSize {
    const char *gdb_type;
    int  size;
    const char *suffix;
};

static struct TypeSize vec_lanes[] = {
    { "uint128", 128, "qu"},
    { "int128", 128, "qs" },
    { "uint64", 64, "lu"},
    { "int64", 64, "ls" },
    { "uint32", 32, "u"},
    { "int32", 32, "s" },
    { "uint16", 16, "hu"},
    { "int16", 16, "hs" },
    { "uint8", 8, "ub"},
    { "int8", 8, "sb" },
    { "ieee_double", 64, "df" },
    { "ieee_single", 32, "sf" },
    { "ieee_half", 16, "hf" },
};


int arm_gen_dynamic_svereg_xml(CPUState *cs, int base_reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    GString *s = g_string_new(NULL);
    DynamicGDBXMLInfo *info = &cpu->dyn_svereg_xml;
    g_autoptr(GString) ts = g_string_new("");
    g_autoptr(GString) us = g_string_new("");
    int i, j;
    info->num = 0;
    g_string_printf(s, "<?xml version=\"1.0\"?>");
    g_string_append_printf(s, "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">");
    g_string_append_printf(s, "<feature name=\"org.qemu.gdb.aarch64.sve\">");
    /* first define types and the union they belong to */
    for (i = 0; i < ARRAY_SIZE(vec_lanes); i++) {
        int count = 128 / vec_lanes[i].size;
        g_string_printf(ts, "vq%d%s", count, vec_lanes[i].suffix);
        g_string_append_printf(s, "<vector id=\"%s\" type=\"%s\" count=\"%d\"/>",
                               ts->str, vec_lanes[i].gdb_type, count);
        g_string_append_printf(us, "<field name=\"%s\" type=\"%s\"/>",
                               vec_lanes[i].suffix, ts->str);
    }
    /* wrap the union around define the overall vq type */
    us = g_string_prepend(us, "<union id=\"vq\">");
    us = g_string_append(us,"</union>");
    g_string_append(s, us->str);

    /* Then define each register in parts for each vq */
    for (i = 0; i < 32; i++) {
        for (j = 0; j < cpu->sve_max_vq; j++) {
            g_string_append_printf(s,
                                   "<reg name=\"z%dp%d\" bitsize=\"128\""
                                   " regnum=\"%d\" group=\"vector\""
                                   " type=\"vq\"/>",
                                   i, j, base_reg++);
            info->num++;
        }
    }
    /* fpscr & status registers */
    info->data.sve.fpsr_pos = info->num;
    g_string_append_printf(s, "<reg name=\"fpsr\" bitsize=\"32\""
                           " regnum=\"%d\" group=\"float\""
                           " type=\"int\"/>", base_reg++);
    g_string_append_printf(s, "<reg name=\"fpcr\" bitsize=\"32\""
                           " regnum=\"%d\" group=\"float\""
                           " type=\"int\"/>", base_reg++);
    info->num += 2;
    /*
     * Predicate registers aren't so big they are worth splitting up
     * but we do need to define a type to hold the array of quad
     * references.
     */
    g_string_append_printf(s,
                           "<vector id=\"vqp\" type=\"uint16\" count=\"%d\"/>",
                           cpu->sve_max_vq);
    for (i = 0; i < 16; i++) {
        g_string_append_printf(s,
                               "<reg name=\"p%d\" bitsize=\"%d\""
                               " regnum=\"%d\" group=\"vector\""
                               " type=\"vqp\"/>",
                               i, cpu->sve_max_vq * 16, base_reg++);
        info->num++;
    }
    g_string_append_printf(s,
                           "<reg name=\"ffr\" bitsize=\"%d\""
                           " regnum=\"%d\" group=\"vector\""
                           " type=\"vqp\"/>",
                           cpu->sve_max_vq * 16, base_reg++);
    info->num++;
    g_string_append_printf(s, "</feature>");
    cpu->dyn_svereg_xml.desc = g_string_free(s, false);
    return cpu->dyn_svereg_xml.num;
}


const char *arm_gdb_get_dynamic_xml(CPUState *cs, const char *xmlname)
{
    ARMCPU *cpu = ARM_CPU(cs);

    if (strcmp(xmlname, "system-registers.xml") == 0) {
        return cpu->dyn_sysreg_xml.desc;
    } else if (strcmp(xmlname, "sve-registers.xml") == 0) {
        return cpu->dyn_svereg_xml.desc;
    }
    return NULL;
}
