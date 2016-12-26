/* -*- C -*-
 *
 * Guest-side management of hypertrace.
 *
 * Copyright (C) 2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 */

extern uint64_t _qemu_hypertrace_channel_max_clients;
extern uint64_t _qemu_hypertrace_channel_num_args;
extern uint64_t *_qemu_hypertrace_channel_data;
extern uint64_t *_qemu_hypertrace_channel_control;

static inline uint64_t qemu_hypertrace_max_clients(void)
{
    return _qemu_hypertrace_channel_max_clients;
}

static inline uint64_t qemu_hypertrace_num_args(void)
{
    return _qemu_hypertrace_channel_num_args;
}

static inline uint64_t *qemu_hypertrace_data(uint64_t client)
{
    size_t args_size = qemu_hypertrace_num_args() * sizeof(uint64_t);
    return &_qemu_hypertrace_channel_data[client * args_size];
}

static inline void qemu_hypertrace(uint64_t client, uint64_t arg1)
{
    uint64_t *ctrlp = _qemu_hypertrace_channel_control;
    ctrlp[client] = arg1;
}
