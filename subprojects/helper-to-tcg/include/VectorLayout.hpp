//
//  Copyright(c) 2026 rev.ng Labs Srl. All Rights Reserved.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, see <http://www.gnu.org/licenses/>.
//

#pragma once

#include <llvm/ADT/ArrayRef.h>
#include <stddef.h>
#include <stdint.h>

// Vector Layout
//
// Vector layouts among QEMU guests is quite diverse, this header aims to be
// simple but still capture a large subset of targets.
//
// Guest vectors are assumed to be divided into host-endian blocks of a
// configurable size (>= 64-bit and power-of-two), with block 0 always being at
// the lowest address.  Blocks in turn are divided into 64-bit columns which are
// used when interfacing with QEMUs `gvec` API.
//
// Below is a 32-byte <32 x i8> guest vector consisting of 16-byte blocks,
// 8-byte columns, and 1-byte lanes for each element in the vector.  Elements
// count from 1-32 in hex to indicate their position in memory.  Note this
// example is for a little-endian host with `LeastSignificant` lane preference
// meaning lane 0 is at the least significant bytes of each block.
//
//   Guest vector <32 x i8> {1, 2, 3, ..., 32}
//   +---------------------------------+---------------------------------+
//   |              BLOCK 0            |              BLOCK 1            |
//   +----------------+----------------+----------------+----------------+
//   |    COLUMN 0    |    COLUMN 1    |    COLUMN 2    |    COLUMN 3    |
//   |0102030405060708|090A0B0C0D0E0F10|1112131415161718|191A1B1C1D1E1F20|
//
//   LOW ADDRESS                                              HIGH ADDRESS
//
// On a big-endian host, the vector would instead be expressed in memory as
//
//   Guest vector <32 x i8> {1, 2, 3, ..., 32}
//   +---------------------------------+---------------------------------+
//   |              BLOCK 0            |              BLOCK 1            |
//   +----------------+----------------+----------------+----------------+
//   |    COLUMN 1    |    COLUMN 0    |    COLUMN 3    |    COLUMN 2    |
//   |100F0E0D0C0B0A09|0807060504030201|201F1E1D1C1B1A19|1817161514131211|
//
//   LOW ADDRESS                                              HIGH ADDRESS
//
// with the column order having shifted to make sure the host-endian blocks
// retain the same value.  Blocks are also allowed to be equal or greater in
// size that the guest vector, at which point the entire memory view of the
// vector would reverse when going from a little to big-endian host.
//
// The lane preference may also be changed to `MostSignificant` which for the
// little-endian example places lane 0 and the highest address of block 0.
// Note, that `MostSignificant` on little-endian and `LeastSignificant` on
// big-endian hosts are equivalent in their memory representation, but will
// differ in how they are emitted in C:
//
//   // MostSignificant, little endian
//   uint64_t vec[] = {0x90A0B0C0D0E0F10, 0x102030405060708, ...};
//
//   // LeastSignificant, big endian
//   uint64_t vec[] = {0x100F0E0D0C0B0A09, 0x807060504030201, ...};
//

enum LanePreference {
    LeastSignificant,
    MostSignificant,
};

struct VectorLayout {
    bool HostBigEndian;
    LanePreference Lane0;
    size_t BlockBytes;

    inline size_t index(bool Reverse, size_t Count, size_t I, size_t J) const {
        if (Reverse) {
            return Count * I + (J ^ (Count - 1));
        } else {
            return Count * I + J;
        }
    }

    // Return the index into a linear array of lane L in column C
    inline size_t indexLane(size_t LanesPerColumn, size_t C, size_t L) const {
        const bool Reverse = (Lane0 == MostSignificant);
        return index(Reverse, LanesPerColumn, C, L);
    }

    // Return the index into a linear array of
    inline size_t indexBlock(size_t BlocksPerVec, size_t I) const {
        const bool Reverse = (HostBigEndian ^ (Lane0 == MostSignificant));
        return index(Reverse, BlocksPerVec, 0, I);
    }

    //inline uint64_t column(llvm::ArrayRef<uint64_t> Lanes) {
    //    uint64_t Column = 0;
    //    const size_t LanesPerColumn = 64 / ElementSize;
    //    for (size_t L = 0; L < LanesPerColumn; ++L) {
    //        const size_t Index = VL.indexLane(LanesPerColumn, C, L);
    //        Column |= Ints[Index] << (ElementSize * L);
    //    }
    //}
};
