#!/usr/bin/env python3

import sys
import struct
import string


class Qcow2BitmapDirEntry:

    name = ''
    BME_FLAG_IN_USE = 1
    BME_FLAG_AUTO = 1 << 1

    uint8_t = 'B'
    uint16_t = 'H'
    uint32_t = 'I'
    uint64_t = 'Q'

    fields = [
        [uint64_t, '%#x', 'bitmap_table_offset'],
        [uint32_t, '%d',  'bitmap_table_size'],
        [uint32_t, '%d',  'flags'],
        [uint8_t,  '%d',  'type'],
        [uint8_t,  '%d',  'granularity_bits'],
        [uint16_t, '%d',  'name_size'],
        [uint32_t, '%d',  'extra_data_size']
    ]

    fmt = '>' + ''.join(field[0] for field in fields)

    def __init__(self, data):

        entry = struct.unpack(Qcow2BitmapDirEntry.fmt, data)
        self.__dict__ = dict((field[2], entry[i])
                             for i, field in enumerate(
                                 Qcow2BitmapDirEntry.fields))

        self.bitmap_table_size = self.bitmap_table_size \
            * struct.calcsize(self.uint64_t)

    def bitmap_dir_entry_size(self):
        size = struct.calcsize(self.fmt) + self.name_size + \
            self.extra_data_size
        return (size + 7) & ~7

    def dump_bitmap_dir_entry(self):
        print("%-25s" % 'Bitmap name', self.name)
        if (self.flags & self.BME_FLAG_IN_USE) != 0:
            print("%-25s" % 'flag', '"in-use"')
        if (self.flags & self.BME_FLAG_AUTO) != 0:
            print("%-25s" % 'flag', '"auto"')
        for f in Qcow2BitmapDirEntry.fields:
            value = self.__dict__[f[2]]
            value_str = f[1] % value

            print("%-25s" % f[2], value_str)
        print("")

    def dump_bitmap_table(self, fd):
        fd.seek(self.bitmap_table_offset)
        table_size = self.bitmap_table_size * struct.calcsize(self.uint64_t)
        bitmap_table = [e[0] for e in struct.iter_unpack('>Q',
                                                         fd.read(table_size))]
        BME_TABLE_ENTRY_OFFSET_MASK = 0x00fffffffffffe00
        BME_TABLE_ENTRY_FLAG_ALL_ONES = 1
        bmt_type = ['all-zeroes', 'all-ones', 'serialized']
        items = enumerate(bitmap_table)
        print("Bitmap table")
        for i, entry in items:
            offset = entry & BME_TABLE_ENTRY_OFFSET_MASK
            if offset != 0:
                index = 2
            else:
                index = entry & BME_TABLE_ENTRY_FLAG_ALL_ONES
            print("   %-4d  %s, offset %#x" % (i, bmt_type[index], offset))
        print("")


class Qcow2BitmapExt:

    uint32_t = 'I'
    uint64_t = 'Q'

    fields = [
        [uint32_t, '%d',  'nb_bitmaps'],
        [uint32_t, '%d',  'reserved32'],
        [uint64_t, '%#x', 'bitmap_directory_size'],
        [uint64_t, '%#x', 'bitmap_directory_offset']
    ]

    fmt = '>' + ''.join(field[0] for field in fields)

    def __init__(self, data):

        extension = struct.unpack(Qcow2BitmapExt.fmt, data)
        self.__dict__ = dict((field[2], extension[i])
                             for i, field in enumerate(Qcow2BitmapExt.fields))

    def dump_bitmap_ext(self):
        for f in Qcow2BitmapExt.fields:
            value = self.__dict__[f[2]]
            value_str = f[1] % value

            print("%-25s" % f[2], value_str)
        print("")

    def bitmap_directory(self, fd):
        offset = self.bitmap_directory_offset
        buf_size = struct.calcsize(Qcow2BitmapDirEntry.fmt)

        for n in range(self.nb_bitmaps):
            fd.seek(offset)
            buf = fd.read(buf_size)
            dir_entry = Qcow2BitmapDirEntry(buf)
            fd.seek(dir_entry.extra_data_size, 1)
            bitmap_name = fd.read(dir_entry.name_size)
            dir_entry.name = bitmap_name.decode('ascii')
            dir_entry.dump_bitmap_dir_entry()
            dir_entry.dump_bitmap_table(fd)
            offset += dir_entry.bitmap_dir_entry_size()


class QcowHeaderExtension:

    def __init__(self, magic, length, data):
        if length % 8 != 0:
            padding = 8 - (length % 8)
            data += b"\0" * padding

        self.magic  = magic
        self.length = length
        self.data   = data

    @classmethod
    def create(cls, magic, data):
        return QcowHeaderExtension(magic, len(data), data)

class QcowHeader:

    QCOW2_EXT_MAGIC_FEATURE_TABLE = 0x6803f857
    QCOW2_EXT_MAGIC_BITMAPS = 0x23852875
    uint32_t = 'I'
    uint64_t = 'Q'

    fields = [
        # Version 2 header fields
        [ uint32_t, '%#x',  'magic' ],
        [ uint32_t, '%d',   'version' ],
        [ uint64_t, '%#x',  'backing_file_offset' ],
        [ uint32_t, '%#x',  'backing_file_size' ],
        [ uint32_t, '%d',   'cluster_bits' ],
        [ uint64_t, '%d',   'size' ],
        [ uint32_t, '%d',   'crypt_method' ],
        [ uint32_t, '%d',   'l1_size' ],
        [ uint64_t, '%#x',  'l1_table_offset' ],
        [ uint64_t, '%#x',  'refcount_table_offset' ],
        [ uint32_t, '%d',   'refcount_table_clusters' ],
        [ uint32_t, '%d',   'nb_snapshots' ],
        [ uint64_t, '%#x',  'snapshot_offset' ],

        # Version 3 header fields
        [ uint64_t, 'mask', 'incompatible_features' ],
        [ uint64_t, 'mask', 'compatible_features' ],
        [ uint64_t, 'mask', 'autoclear_features' ],
        [ uint32_t, '%d',   'refcount_order' ],
        [ uint32_t, '%d',   'header_length' ],
    ];

    fmt = '>' + ''.join(field[0] for field in fields)

    def __init__(self, fd):

        buf_size = struct.calcsize(QcowHeader.fmt)

        fd.seek(0)
        buf = fd.read(buf_size)

        header = struct.unpack(QcowHeader.fmt, buf)
        self.__dict__ = dict((field[2], header[i])
            for i, field in enumerate(QcowHeader.fields))

        self.set_defaults()
        self.cluster_size = 1 << self.cluster_bits

        fd.seek(self.header_length)
        self.load_extensions(fd)

        if self.backing_file_offset:
            fd.seek(self.backing_file_offset)
            self.backing_file = fd.read(self.backing_file_size)
        else:
            self.backing_file = None

    def set_defaults(self):
        if self.version == 2:
            self.incompatible_features = 0
            self.compatible_features = 0
            self.autoclear_features = 0
            self.refcount_order = 4
            self.header_length = 72

    def load_extensions(self, fd):
        self.extensions = []

        if self.backing_file_offset != 0:
            end = min(self.cluster_size, self.backing_file_offset)
        else:
            end = self.cluster_size

        while fd.tell() < end:
            (magic, length) = struct.unpack('>II', fd.read(8))
            if magic == 0:
                break
            else:
                padded = (length + 7) & ~7
                data = fd.read(padded)
                self.extensions.append(QcowHeaderExtension(magic, length, data))

    def update_extensions(self, fd):

        fd.seek(self.header_length)
        extensions = self.extensions
        extensions.append(QcowHeaderExtension(0, 0, b""))
        for ex in extensions:
            buf = struct.pack('>II', ex.magic, ex.length)
            fd.write(buf)
            fd.write(ex.data)

        if self.backing_file != None:
            self.backing_file_offset = fd.tell()
            fd.write(self.backing_file)

        if fd.tell() > self.cluster_size:
            raise Exception("I think I just broke the image...")


    def update(self, fd):
        header_bytes = self.header_length

        self.update_extensions(fd)

        fd.seek(0)
        header = tuple(self.__dict__[f] for t, p, f in QcowHeader.fields)
        buf = struct.pack(QcowHeader.fmt, *header)
        buf = buf[0:header_bytes-1]
        fd.write(buf)

    def extension_name(self, magic):
       return {
            self.QCOW2_EXT_MAGIC_FEATURE_TABLE: 'Feature table',
            self.QCOW2_EXT_MAGIC_BITMAPS: 'Bitmaps',
        }.get(magic, 'Unknown')

    def dump(self):
        for f in QcowHeader.fields:
            value = self.__dict__[f[2]]
            if f[1] == 'mask':
                bits = []
                for bit in range(64):
                    if value & (1 << bit):
                        bits.append(bit)
                value_str = str(bits)
            else:
                value_str = f[1] % value

            print("%-25s" % f[2], value_str)
        print("")

    def dump_extensions(self, fd):
        for ex in self.extensions:

            print("Header extension (%s):" % self.extension_name(ex.magic))
            print("%-25s %#x" % ("magic", ex.magic))
            print("%-25s %d" % ("length", ex.length))

            data = ex.data[:ex.length]
            if all(c in string.printable.encode('ascii') for c in data):
                data = "'%s'" % data.decode('ascii')
                print("%-25s %s" % ("data", data))
            else:
                self.dump_extension_data(fd, ex)

            print("")

    def dump_extension_data(self, fd, ext):
        if ext.magic == self.QCOW2_EXT_MAGIC_BITMAPS:
            b_ext = Qcow2BitmapExt(ext.data)
            b_ext.dump_bitmap_ext()
            b_ext.bitmap_directory(fd)
        else:
            print("%-25s %s" % ("data", "<binary>"))


def cmd_dump_header(fd):
    h = QcowHeader(fd)
    h.dump()
    h.dump_extensions(fd)

def cmd_dump_header_exts(fd):
    h = QcowHeader(fd)
    h.dump_extensions(fd)

def cmd_set_header(fd, name, value):
    try:
        value = int(value, 0)
    except:
        print("'%s' is not a valid number" % value)
        sys.exit(1)

    fields = (field[2] for field in QcowHeader.fields)
    if not name in fields:
        print("'%s' is not a known header field" % name)
        sys.exit(1)

    h = QcowHeader(fd)
    h.__dict__[name] = value
    h.update(fd)

def cmd_add_header_ext(fd, magic, data):
    try:
        magic = int(magic, 0)
    except:
        print("'%s' is not a valid magic number" % magic)
        sys.exit(1)

    h = QcowHeader(fd)
    h.extensions.append(QcowHeaderExtension.create(magic, data.encode('ascii')))
    h.update(fd)

def cmd_add_header_ext_stdio(fd, magic):
    data = sys.stdin.read()
    cmd_add_header_ext(fd, magic, data)

def cmd_del_header_ext(fd, magic):
    try:
        magic = int(magic, 0)
    except:
        print("'%s' is not a valid magic number" % magic)
        sys.exit(1)

    h = QcowHeader(fd)
    found = False

    for ex in h.extensions:
        if ex.magic == magic:
            found = True
            h.extensions.remove(ex)

    if not found:
        print("No such header extension")
        return

    h.update(fd)

def cmd_set_feature_bit(fd, group, bit):
    try:
        bit = int(bit, 0)
        if bit < 0 or bit >= 64:
            raise ValueError
    except:
        print("'%s' is not a valid bit number in range [0, 64)" % bit)
        sys.exit(1)

    h = QcowHeader(fd)
    if group == 'incompatible':
        h.incompatible_features |= 1 << bit
    elif group == 'compatible':
        h.compatible_features |= 1 << bit
    elif group == 'autoclear':
        h.autoclear_features |= 1 << bit
    else:
        print("'%s' is not a valid group, try 'incompatible', 'compatible', or 'autoclear'" % group)
        sys.exit(1)

    h.update(fd)

cmds = [
    [ 'dump-header',          cmd_dump_header,          0, 'Dump image header and header extensions' ],
    [ 'dump-header-exts',     cmd_dump_header_exts,     0, 'Dump image header extensions' ],
    [ 'set-header',           cmd_set_header,           2, 'Set a field in the header'],
    [ 'add-header-ext',       cmd_add_header_ext,       2, 'Add a header extension' ],
    [ 'add-header-ext-stdio', cmd_add_header_ext_stdio, 1, 'Add a header extension, data from stdin' ],
    [ 'del-header-ext',       cmd_del_header_ext,       1, 'Delete a header extension' ],
    [ 'set-feature-bit',      cmd_set_feature_bit,      2, 'Set a feature bit'],
]

def main(filename, cmd, args):
    fd = open(filename, "r+b")
    try:
        for name, handler, num_args, desc in cmds:
            if name != cmd:
                continue
            elif len(args) != num_args:
                usage()
                return
            else:
                handler(fd, *args)
                return
        print("Unknown command '%s'" % cmd)
    finally:
        fd.close()

def usage():
    print("Usage: %s <file> <cmd> [<arg>, ...]" % sys.argv[0])
    print("")
    print("Supported commands:")
    for name, handler, num_args, desc in cmds:
        print("    %-20s - %s" % (name, desc))

if __name__ == '__main__':
    if len(sys.argv) < 3:
        usage()
        sys.exit(1)

    main(sys.argv[1], sys.argv[2], sys.argv[3:])
