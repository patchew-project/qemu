/*
 * QEMU CPU I/O instructions
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CPU_IO_H
#define CPU_IO_H

void cpu_outb(uint32_t addr, uint8_t val);
void cpu_outw(uint32_t addr, uint16_t val);
void cpu_outl(uint32_t addr, uint32_t val);
uint8_t cpu_inb(uint32_t addr);
uint16_t cpu_inw(uint32_t addr);
uint32_t cpu_inl(uint32_t addr);

#endif /* CPU_IO_H */
