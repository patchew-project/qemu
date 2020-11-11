/*
 *  Common header file to be used by cpu and disassembler.
 *  Copyright (C) 2017 Free Software Foundation, Inc.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GAS or GDB; see the file COPYING3. If not, write to
 *  the Free Software Foundation, 51 Franklin Street - Fifth Floor, Boston,
 *  MA 02110-1301, USA.
 */

#ifndef ARC_COMMON_H
#define ARC_COMMON_H


/* CPU combi. */
#define ARC_OPCODE_ARCALL  (ARC_OPCODE_ARC600 | ARC_OPCODE_ARC700       \
                            | ARC_OPCODE_ARCv2EM | ARC_OPCODE_ARCv2HS)
#define ARC_OPCODE_ARCFPX  (ARC_OPCODE_ARC700 | ARC_OPCODE_ARCv2EM)
#define ARC_OPCODE_ARCV1   (ARC_OPCODE_ARC700 | ARC_OPCODE_ARC600)
#define ARC_OPCODE_ARCV2   (ARC_OPCODE_ARCv2EM | ARC_OPCODE_ARCv2HS)
#define ARC_OPCODE_ARCMPY6E  (ARC_OPCODE_ARC700 | ARC_OPCODE_ARCV2)


enum arc_cpu_family {
  ARC_OPCODE_NONE    = 0,
  ARC_OPCODE_DEFAULT = 1 << 0,
  ARC_OPCODE_ARC600  = 1 << 1,
  ARC_OPCODE_ARC700  = 1 << 2,
  ARC_OPCODE_ARCv2EM = 1 << 3,
  ARC_OPCODE_ARCv2HS = 1 << 4
};

typedef struct {
    uint32_t value;
    uint32_t type;
} operand_t;

typedef struct {
    uint32_t class;
    uint32_t limm;
    uint8_t len;
    bool limm_p;
    operand_t operands[3];
    uint8_t n_ops;
    uint8_t cc;
    uint8_t aa;
    uint8_t zz;
    bool d;
    bool f;
    bool di;
    bool x;
} insn_t;

#endif
