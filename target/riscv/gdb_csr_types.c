/* Copyright (c) 2021 Siemens AG, konrad.schwarz@siemens.com */

#include "qemu/osdep.h"
#include "gdb_csr_types.h"
#define STR(X) #X

char const riscv_gdb_csr_types[] =
#ifdef TARGET_RISCV32
   STR(
<enum id="sstatus-fs-type" size="4">
  <evalue name="off" value="0"/>
  <evalue name="initial" value="1"/>
  <evalue name="clean" value="2"/>
  <evalue name="dirty" value="3"/>
</enum><enum id="sstatus-xs-type" size="4">
  <evalue name="off" value="0"/>
  <evalue name="initial" value="1"/>
  <evalue name="clean" value="2"/>
  <evalue name="dirty" value="3"/>
</enum><enum id="sstatus-uxl-type" size="4">
  <evalue name="32" value="1"/>
  <evalue name="64" value="2"/>
  <evalue name="128" value="3"/>
</enum><enum id="stvec-mode-type" size="4">
  <evalue name="direct" value="0"/>
  <evalue name="vectored" value="1"/>
</enum><enum id="scause-exc-type" size="4">
  <evalue name="instruction_address_misaligned" value="0"/>
  <evalue name="instruction_access_fault" value="1"/>
  <evalue name="illegal_instruction" value="2"/>
  <evalue name="breakpoint" value="3"/>
  <evalue name="load_address_misaligned" value="4"/>
  <evalue name="load_access_fault" value="5"/>
  <evalue name="store_address_misaligned" value="6"/>
  <evalue name="store_access_fault" value="7"/>
  <evalue name="enironment_call_from_U_mode" value="8"/>
  <evalue name="enironment_call_from_S_mode" value="9"/>
  <evalue name="enironment_call_from_VS_mode" value="10"/>
  <evalue name="enironment_call_from_M_mode" value="11"/>
  <evalue name="instruction_page_fault" value="12"/>
  <evalue name="load_page_fault" value="13"/>
  <evalue name="store_page_fault" value="15"/>
  <evalue name="instruction_guest_page_fault" value="20"/>
  <evalue name="load_guest_page_fault" value="21"/>
  <evalue name="virtual_instruction" value="22"/>
  <evalue name="store_guest_page_fault" value="23"/>
</enum><enum id="satp-mode-type" size="4">
  <evalue name="bare" value="0"/>
  <evalue name="sv32" value="1"/>
  <evalue name="sv39" value="8"/>
  <evalue name="sv48" value="9"/>
  <evalue name="sv57" value="10"/>
  <evalue name="sv64" value="11"/>
</enum><enum id="hgatp-mode-type" size="4">
  <evalue name="bare" value="0"/>
  <evalue name="sv32x4" value="1"/>
  <evalue name="sv39x4" value="8"/>
  <evalue name="sv48x4" value="9"/>
  <evalue name="sv57x4" value="10"/>
</enum><flags id="sstatus-fields" size="4">
  <field name="sie" start="1" end="1"/>
  <field name="mie" start="3" end="3"/>
  <field name="spie" start="5" end="5"/>
  <field name="ube" start="6" end="6"/>
  <field name="mpie" start="7" end="7"/>
  <field name="spp" start="8" end="8"/>
  <field name="mpp" start="11" end="12"/>
  <field name="fs" start="13" end="14" type="sstatus-fs-type"/>
  <field name="xs" start="15" end="16" type="sstatus-xs-type"/>
  <field name="mprv" start="17" end="17"/>
  <field name="sum" start="18" end="18"/>
  <field name="mxr" start="19" end="19"/>
  <field name="tvm" start="20" end="20"/>
  <field name="tw" start="21" end="21"/>
  <field name="tsr" start="22" end="23"/>
  <field name="uxl" start="32" end="33" type="sstatus-uxl-type"/>
  <field name="sxl" start="34" end="35"/>
  <field name="sbe" start="36" end="36"/>
  <field name="mbe" start="37" end="37"/>
  <field name="gva" start="38" end="38"/>
  <field name="mpv" start="39" end="39"/>
  <field name="sd" start="63" end="63"/>
</flags><flags id="sie-fields" size="4">
  <field name="ssie" start="1" end="1"/>
  <field name="vssie" start="2" end="2"/>
  <field name="msie" start="3" end="3"/>
  <field name="stie" start="5" end="5"/>
  <field name="vstie" start="6" end="6"/>
  <field name="mtie" start="7" end="7"/>
  <field name="seie" start="9" end="9"/>
  <field name="vseie" start="10" end="10"/>
  <field name="meie" start="11" end="11"/>
  <field name="sgeie" start="12" end="12"/>
</flags><flags id="stvec-fields" size="4">
  <field name="mode" start="0" end="1" type="stvec-mode-type"/>
  <field name="base" start="2" end="63"/>
</flags><flags id="scounteren-fields" size="4">
  <field name="cy" start="0" end="0"/>
  <field name="tm" start="1" end="1"/>
  <field name="ir" start="2" end="2"/>
  <field name="hpm" start="3" end="31"/>
</flags><flags id="scause-fields" size="4">
  <field name="exc" start="0" end="30" type="scause-exc-type"/>
  <field name="interrupt" start="31" end="31"/>
</flags><flags id="sip-fields" size="4">
  <field name="ssip" start="1" end="1"/>
  <field name="vssip" start="2" end="2"/>
  <field name="msip" start="3" end="3"/>
  <field name="stip" start="5" end="5"/>
  <field name="vstip" start="6" end="6"/>
  <field name="mtip" start="7" end="7"/>
  <field name="seip" start="9" end="9"/>
  <field name="vseip" start="10" end="10"/>
  <field name="meip" start="11" end="11"/>
  <field name="sgeip" start="12" end="12"/>
</flags><flags id="satp-fields" size="4">
  <field name="ppn" start="0" end="43"/>
  <field name="asid" start="44" end="59"/>
  <field name="mode" start="60" end="63" type="satp-mode-type"/>
</flags><flags id="hstatus-fields" size="4">
  <field name="vsbe" start="5" end="5"/>
  <field name="gva" start="6" end="6"/>
  <field name="spv" start="7" end="7"/>
  <field name="spvp" start="8" end="8"/>
  <field name="hu" start="9" end="9"/>
  <field name="vgein" start="12" end="17"/>
  <field name="vtvm" start="20" end="20"/>
  <field name="vtsr" start="22" end="22"/>
  <field name="vsxl" start="32" end="33"/>
</flags><flags id="hedeleg-fields" size="4">
  <field name="instruction_address_misaligned" start="0" end="0"/>
  <field name="instruction_access_fault" start="1" end="1"/>
  <field name="illegal_instruction" start="2" end="2"/>
  <field name="breakpoint" start="3" end="3"/>
  <field name="load_address_misaligned" start="4" end="4"/>
  <field name="load_access_fault" start="5" end="5"/>
  <field name="store_address_misaligned" start="6" end="6"/>
  <field name="store_access_fault" start="7" end="7"/>
  <field name="enironment_call_from_U_mode" start="8" end="8"/>
  <field name="enironment_call_from_S_mode" start="9" end="9"/>
  <field name="enironment_call_from_VS_mode" start="10" end="10"/>
  <field name="enironment_call_from_M_mode" start="11" end="11"/>
  <field name="instruction_page_fault" start="12" end="12"/>
  <field name="load_page_fault" start="13" end="13"/>
  <field name="store_page_fault" start="15" end="15"/>
  <field name="instruction_guest_page_fault" start="20" end="20"/>
  <field name="load_guest_page_fault" start="21" end="21"/>
  <field name="virtual_instruction" start="22" end="22"/>
  <field name="store_guest_page_fault" start="23" end="23"/>
</flags><flags id="hie-fields" size="4">
  <field name="vssie" start="2" end="2"/>
  <field name="vstie" start="6" end="6"/>
  <field name="vseie" start="10" end="10"/>
  <field name="sgeie" start="12" end="12"/>
</flags><flags id="hip-fields" size="4">
  <field name="vssip" start="2" end="2"/>
  <field name="vstip" start="6" end="6"/>
  <field name="vseip" start="10" end="10"/>
  <field name="sgeip" start="12" end="12"/>
</flags><flags id="hvip-fields" size="4">
  <field name="vssip" start="2" end="2"/>
  <field name="vstip" start="6" end="6"/>
  <field name="vseip" start="10" end="10"/>
</flags><flags id="hgatp-fields" size="4">
  <field name="ppn" start="0" end="43"/>
  <field name="vmid" start="44" end="57"/>
  <field name="mode" start="60" end="63" type="hgatp-mode-type"/>
</flags>
)
#elif defined TARGET_RISCV64
   STR(
<enum id="sstatus-fs-type" size="8">
  <evalue name="off" value="0"/>
  <evalue name="initial" value="1"/>
  <evalue name="clean" value="2"/>
  <evalue name="dirty" value="3"/>
</enum><enum id="sstatus-xs-type" size="8">
  <evalue name="off" value="0"/>
  <evalue name="initial" value="1"/>
  <evalue name="clean" value="2"/>
  <evalue name="dirty" value="3"/>
</enum><enum id="sstatus-uxl-type" size="8">
  <evalue name="32" value="1"/>
  <evalue name="64" value="2"/>
  <evalue name="128" value="3"/>
</enum><enum id="stvec-mode-type" size="8">
  <evalue name="direct" value="0"/>
  <evalue name="vectored" value="1"/>
</enum><enum id="scause-exc-type" size="8">
  <evalue name="instruction_address_misaligned" value="0"/>
  <evalue name="instruction_access_fault" value="1"/>
  <evalue name="illegal_instruction" value="2"/>
  <evalue name="breakpoint" value="3"/>
  <evalue name="load_address_misaligned" value="4"/>
  <evalue name="load_access_fault" value="5"/>
  <evalue name="store_address_misaligned" value="6"/>
  <evalue name="store_access_fault" value="7"/>
  <evalue name="enironment_call_from_U_mode" value="8"/>
  <evalue name="enironment_call_from_S_mode" value="9"/>
  <evalue name="enironment_call_from_VS_mode" value="10"/>
  <evalue name="enironment_call_from_M_mode" value="11"/>
  <evalue name="instruction_page_fault" value="12"/>
  <evalue name="load_page_fault" value="13"/>
  <evalue name="store_page_fault" value="15"/>
  <evalue name="instruction_guest_page_fault" value="20"/>
  <evalue name="load_guest_page_fault" value="21"/>
  <evalue name="virtual_instruction" value="22"/>
  <evalue name="store_guest_page_fault" value="23"/>
</enum><enum id="satp-mode-type" size="8">
  <evalue name="bare" value="0"/>
  <evalue name="sv32" value="1"/>
  <evalue name="sv39" value="8"/>
  <evalue name="sv48" value="9"/>
  <evalue name="sv57" value="10"/>
  <evalue name="sv64" value="11"/>
</enum><enum id="hgatp-mode-type" size="8">
  <evalue name="bare" value="0"/>
  <evalue name="sv32x4" value="1"/>
  <evalue name="sv39x4" value="8"/>
  <evalue name="sv48x4" value="9"/>
  <evalue name="sv57x4" value="10"/>
</enum><flags id="sstatus-fields" size="8">
  <field name="sie" start="1" end="1"/>
  <field name="mie" start="3" end="3"/>
  <field name="spie" start="5" end="5"/>
  <field name="ube" start="6" end="6"/>
  <field name="mpie" start="7" end="7"/>
  <field name="spp" start="8" end="8"/>
  <field name="mpp" start="11" end="12"/>
  <field name="fs" start="13" end="14" type="sstatus-fs-type"/>
  <field name="xs" start="15" end="16" type="sstatus-xs-type"/>
  <field name="mprv" start="17" end="17"/>
  <field name="sum" start="18" end="18"/>
  <field name="mxr" start="19" end="19"/>
  <field name="tvm" start="20" end="20"/>
  <field name="tw" start="21" end="21"/>
  <field name="tsr" start="22" end="23"/>
  <field name="uxl" start="32" end="33" type="sstatus-uxl-type"/>
  <field name="sxl" start="34" end="35"/>
  <field name="sbe" start="36" end="36"/>
  <field name="mbe" start="37" end="37"/>
  <field name="gva" start="38" end="38"/>
  <field name="mpv" start="39" end="39"/>
  <field name="sd" start="63" end="63"/>
</flags><flags id="sie-fields" size="8">
  <field name="ssie" start="1" end="1"/>
  <field name="vssie" start="2" end="2"/>
  <field name="msie" start="3" end="3"/>
  <field name="stie" start="5" end="5"/>
  <field name="vstie" start="6" end="6"/>
  <field name="mtie" start="7" end="7"/>
  <field name="seie" start="9" end="9"/>
  <field name="vseie" start="10" end="10"/>
  <field name="meie" start="11" end="11"/>
  <field name="sgeie" start="12" end="12"/>
</flags><flags id="stvec-fields" size="8">
  <field name="mode" start="0" end="1" type="stvec-mode-type"/>
  <field name="base" start="2" end="63"/>
</flags><flags id="scounteren-fields" size="8">
  <field name="cy" start="0" end="0"/>
  <field name="tm" start="1" end="1"/>
  <field name="ir" start="2" end="2"/>
  <field name="hpm" start="3" end="31"/>
</flags><flags id="scause-fields" size="8">
  <field name="exc" start="0" end="62" type="scause-exc-type"/>
  <field name="interrupt" start="63" end="63"/>
</flags><flags id="sip-fields" size="8">
  <field name="ssip" start="1" end="1"/>
  <field name="vssip" start="2" end="2"/>
  <field name="msip" start="3" end="3"/>
  <field name="stip" start="5" end="5"/>
  <field name="vstip" start="6" end="6"/>
  <field name="mtip" start="7" end="7"/>
  <field name="seip" start="9" end="9"/>
  <field name="vseip" start="10" end="10"/>
  <field name="meip" start="11" end="11"/>
  <field name="sgeip" start="12" end="12"/>
</flags><flags id="satp-fields" size="8">
  <field name="ppn" start="0" end="43"/>
  <field name="asid" start="44" end="59"/>
  <field name="mode" start="60" end="63" type="satp-mode-type"/>
</flags><flags id="hstatus-fields" size="8">
  <field name="vsbe" start="5" end="5"/>
  <field name="gva" start="6" end="6"/>
  <field name="spv" start="7" end="7"/>
  <field name="spvp" start="8" end="8"/>
  <field name="hu" start="9" end="9"/>
  <field name="vgein" start="12" end="17"/>
  <field name="vtvm" start="20" end="20"/>
  <field name="vtsr" start="22" end="22"/>
  <field name="vsxl" start="32" end="33"/>
</flags><flags id="hedeleg-fields" size="8">
  <field name="instruction_address_misaligned" start="0" end="0"/>
  <field name="instruction_access_fault" start="1" end="1"/>
  <field name="illegal_instruction" start="2" end="2"/>
  <field name="breakpoint" start="3" end="3"/>
  <field name="load_address_misaligned" start="4" end="4"/>
  <field name="load_access_fault" start="5" end="5"/>
  <field name="store_address_misaligned" start="6" end="6"/>
  <field name="store_access_fault" start="7" end="7"/>
  <field name="enironment_call_from_U_mode" start="8" end="8"/>
  <field name="enironment_call_from_S_mode" start="9" end="9"/>
  <field name="enironment_call_from_VS_mode" start="10" end="10"/>
  <field name="enironment_call_from_M_mode" start="11" end="11"/>
  <field name="instruction_page_fault" start="12" end="12"/>
  <field name="load_page_fault" start="13" end="13"/>
  <field name="store_page_fault" start="15" end="15"/>
  <field name="instruction_guest_page_fault" start="20" end="20"/>
  <field name="load_guest_page_fault" start="21" end="21"/>
  <field name="virtual_instruction" start="22" end="22"/>
  <field name="store_guest_page_fault" start="23" end="23"/>
</flags><flags id="hie-fields" size="8">
  <field name="vssie" start="2" end="2"/>
  <field name="vstie" start="6" end="6"/>
  <field name="vseie" start="10" end="10"/>
  <field name="sgeie" start="12" end="12"/>
</flags><flags id="hip-fields" size="8">
  <field name="vssip" start="2" end="2"/>
  <field name="vstip" start="6" end="6"/>
  <field name="vseip" start="10" end="10"/>
  <field name="sgeip" start="12" end="12"/>
</flags><flags id="hvip-fields" size="8">
  <field name="vssip" start="2" end="2"/>
  <field name="vstip" start="6" end="6"/>
  <field name="vseip" start="10" end="10"/>
</flags><flags id="hgatp-fields" size="8">
  <field name="ppn" start="0" end="43"/>
  <field name="vmid" start="44" end="57"/>
  <field name="mode" start="60" end="63" type="hgatp-mode-type"/>
</flags>
)
# endif
;
