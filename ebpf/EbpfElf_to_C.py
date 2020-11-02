#!/usr/bin/python3
# pip install pyelftools

import sys
import argparse

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
from elftools.elf.sections import Section
from elftools.elf.sections import SymbolTableSection

def process_file(filename, prog_name):
    print('Processing file:', filename)
    with open(filename, 'rb') as f:
        with open("%s.h" % prog_name, 'w') as w:

            elffile = ELFFile(f)

            symtab = elffile.get_section_by_name(".symtab")
            if not isinstance(symtab, SymbolTableSection):
                print('  The file has no %s section' % ".symtab")
                return -1

            prog_sec = elffile.get_section_by_name(prog_name);
            if not isinstance(prog_sec, Section):
                print('  The file has no %s section' % prog_name)
                return -1

            w.write('#ifndef %s\n' % prog_name.upper())
            w.write('#define %s\n\n' % prog_name.upper())

            w.write("struct bpf_insn ins%s[] = {\n" % prog_name)
            insns = [prog_sec.data()[i:i + 8] for i in range(0, prog_sec.data_size, 8)]
            for x in insns:
                w.write( \
                    '    {0x%02x, 0x%02x, 0x%02x, 0x%02x%02x, 0x%02x%02x%02x%02x},\n' \
                    % (x[0], x[1] & 0x0f, (x[1] >> 4) & 0x0f, \
                       x[3], x[2], x[7], x[6], x[5], x[4]))
            w.write('};\n\n')

            reladyn_name = '.rel' + prog_name
            reladyn = elffile.get_section_by_name(reladyn_name)

            if isinstance(reladyn, RelocationSection):
                w.write('struct fixup_mapfd_t rel%s[] = {\n' % prog_name)
                for reloc in reladyn.iter_relocations():
                    w.write('    {"%s", %i},\n' \
                        % (symtab.get_symbol(reloc['r_info_sym']).name, \
                           (reloc['r_offset']/8)))
                w.write('};\n\n')
            else:
               print('  The file has no %s section' % reladyn_name)

            w.write('#endif /* %s */\n' % prog_name.upper())

    return 0

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Convert eBPF ELF to C header. '
                    'Section name will be used in C namings.')
    parser.add_argument('--file', '-f', nargs=1, required=True,
                        help='eBPF ELF file')
    parser.add_argument('--section', '-s', nargs=1, required=True,
                        help='section in ELF with eBPF program.')
    args = parser.parse_args()
    sys.exit(process_file(args.file[0], args.section[0]))
