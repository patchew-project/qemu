# Bitmap-related helper utilities
#
# Copyright (c) 2020 John Snow for Red Hat, Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# owner=jsnow@redhat.com

from iotests import log

GRANULARITY = 64 * 1024


class Pattern:
    def __init__(self, byte, offset, size=GRANULARITY):
        self.byte = byte
        self.offset = offset
        self.size = size

    def bits(self, granularity):
        lower = self.offset // granularity
        upper = (self.offset + self.size - 1) // granularity
        return set(range(lower, upper + 1))


class PatternGroup:
    """Grouping of Pattern objects. Initialize with an iterable of Patterns."""
    def __init__(self, patterns):
        self.patterns = patterns

    def bits(self, granularity):
        """Calculate the unique bits dirtied by this pattern grouping"""
        res = set()
        for pattern in self.patterns:
            res |= pattern.bits(granularity)
        return res


GROUPS = [
    PatternGroup([
        # Batch 0: 4 clusters
        Pattern('0x49', 0x0000000),
        Pattern('0x6c', 0x0100000),   # 1M
        Pattern('0x6f', 0x2000000),   # 32M
        Pattern('0x76', 0x3ff0000)]), # 64M - 64K
    PatternGroup([
        # Batch 1: 6 clusters (3 new)
        Pattern('0x65', 0x0000000),   # Full overwrite
        Pattern('0x77', 0x00f8000),   # Partial-left (1M-32K)
        Pattern('0x72', 0x2008000),   # Partial-right (32M+32K)
        Pattern('0x69', 0x3fe0000)]), # Adjacent-left (64M - 128K)
    PatternGroup([
        # Batch 2: 7 clusters (3 new)
        Pattern('0x74', 0x0010000),   # Adjacent-right
        Pattern('0x69', 0x00e8000),   # Partial-left  (1M-96K)
        Pattern('0x6e', 0x2018000),   # Partial-right (32M+96K)
        Pattern('0x67', 0x3fe0000,
                2*GRANULARITY)]),     # Overwrite [(64M-128K)-64M)
    PatternGroup([
        # Batch 3: 8 clusters (5 new)
        # Carefully chosen such that nothing re-dirties the one cluster
        # that copies out successfully before failure in Group #1.
        Pattern('0xaa', 0x0010000,
                3*GRANULARITY),       # Overwrite and 2x Adjacent-right
        Pattern('0xbb', 0x00d8000),   # Partial-left (1M-160K)
        Pattern('0xcc', 0x2028000),   # Partial-right (32M+160K)
        Pattern('0xdd', 0x3fc0000)]), # New; leaving a gap to the right
]


class EmulatedBitmap:
    def __init__(self, granularity=GRANULARITY):
        self._bits = set()
        self.granularity = granularity

    def dirty_bits(self, bits):
        self._bits |= set(bits)

    def dirty_group(self, n):
        self.dirty_bits(GROUPS[n].bits(self.granularity))

    def clear(self):
        self._bits = set()

    def clear_bits(self, bits):
        self._bits -= set(bits)

    def clear_bit(self, bit):
        self.clear_bits({bit})

    def clear_group(self, n):
        self.clear_bits(GROUPS[n].bits(self.granularity))

    @property
    def first_bit(self):
        return sorted(self.bits)[0]

    @property
    def bits(self):
        return self._bits

    @property
    def count(self):
        return len(self.bits)

    def compare(self, qmp_bitmap):
        """
        Print a nice human-readable message checking that a bitmap as reported
        by the QMP interface has as many bits set as we expect it to.
        """

        name = qmp_bitmap.get('name', '(anonymous)')
        log("= Checking Bitmap {:s} =".format(name))

        want = self.count
        have = qmp_bitmap['count'] // qmp_bitmap['granularity']

        log("expecting {:d} dirty sectors; have {:d}. {:s}".format(
            want, have, "OK!" if want == have else "ERROR!"))
        log('')
