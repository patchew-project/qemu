# A simple FAT16 driver that is used to test the `vvfat` driver in QEMU.
#
# Copyright (C) 2024 Amjad Alsharafi <amjadsharafi10@gmail.com>
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

from typing import List

SECTOR_SIZE = 512
DIRENTRY_SIZE = 32


class MBR:
    def __init__(self, data: bytes):
        assert len(data) == 512
        self.partition_table = []
        for i in range(4):
            partition = data[446 + i * 16 : 446 + (i + 1) * 16]
            self.partition_table.append(
                {
                    "status": partition[0],
                    "start_head": partition[1],
                    "start_sector": partition[2] & 0x3F,
                    "start_cylinder": ((partition[2] & 0xC0) << 2) | partition[3],
                    "type": partition[4],
                    "end_head": partition[5],
                    "end_sector": partition[6] & 0x3F,
                    "end_cylinder": ((partition[6] & 0xC0) << 2) | partition[7],
                    "start_lba": int.from_bytes(partition[8:12], "little"),
                    "size": int.from_bytes(partition[12:16], "little"),
                }
            )

    def __str__(self):
        return "\n".join(
            [f"{i}: {partition}" for i, partition in enumerate(self.partition_table)]
        )


class FatBootSector:
    def __init__(self, data: bytes):
        assert len(data) == 512
        self.bytes_per_sector = int.from_bytes(data[11:13], "little")
        self.sectors_per_cluster = data[13]
        self.reserved_sectors = int.from_bytes(data[14:16], "little")
        self.fat_count = data[16]
        self.root_entries = int.from_bytes(data[17:19], "little")
        self.media_descriptor = data[21]
        self.fat_size = int.from_bytes(data[22:24], "little")
        self.sectors_per_fat = int.from_bytes(data[22:24], "little")
        self.sectors_per_track = int.from_bytes(data[24:26], "little")
        self.heads = int.from_bytes(data[26:28], "little")
        self.hidden_sectors = int.from_bytes(data[28:32], "little")
        self.total_sectors = int.from_bytes(data[32:36], "little")
        self.drive_number = data[36]
        self.volume_id = int.from_bytes(data[39:43], "little")
        self.volume_label = data[43:54].decode("ascii").strip()
        self.fs_type = data[54:62].decode("ascii").strip()

    def root_dir_start(self):
        """
        Calculate the start sector of the root directory.
        """
        return self.reserved_sectors + self.fat_count * self.sectors_per_fat

    def root_dir_size(self):
        """
        Calculate the size of the root directory in sectors.
        """
        return (
            self.root_entries * DIRENTRY_SIZE + self.bytes_per_sector - 1
        ) // self.bytes_per_sector

    def data_sector_start(self):
        """
        Calculate the start sector of the data region.
        """
        return self.root_dir_start() + self.root_dir_size()

    def first_sector_of_cluster(self, cluster: int):
        """
        Calculate the first sector of the given cluster.
        """
        return self.data_sector_start() + (cluster - 2) * self.sectors_per_cluster

    def cluster_bytes(self):
        """
        Calculate the number of bytes in a cluster.
        """
        return self.bytes_per_sector * self.sectors_per_cluster

    def __str__(self):
        return (
            f"Bytes per sector: {self.bytes_per_sector}\n"
            f"Sectors per cluster: {self.sectors_per_cluster}\n"
            f"Reserved sectors: {self.reserved_sectors}\n"
            f"FAT count: {self.fat_count}\n"
            f"Root entries: {self.root_entries}\n"
            f"Total sectors: {self.total_sectors}\n"
            f"Media descriptor: {self.media_descriptor}\n"
            f"Sectors per FAT: {self.sectors_per_fat}\n"
            f"Sectors per track: {self.sectors_per_track}\n"
            f"Heads: {self.heads}\n"
            f"Hidden sectors: {self.hidden_sectors}\n"
            f"Drive number: {self.drive_number}\n"
            f"Volume ID: {self.volume_id}\n"
            f"Volume label: {self.volume_label}\n"
            f"FS type: {self.fs_type}\n"
        )


class FatDirectoryEntry:
    def __init__(self, data: bytes, sector: int, offset: int):
        self.name = data[0:8].decode("ascii").strip()
        self.ext = data[8:11].decode("ascii").strip()
        self.attributes = data[11]
        self.reserved = data[12]
        self.create_time_tenth = data[13]
        self.create_time = int.from_bytes(data[14:16], "little")
        self.create_date = int.from_bytes(data[16:18], "little")
        self.last_access_date = int.from_bytes(data[18:20], "little")
        high_cluster = int.from_bytes(data[20:22], "little")
        self.last_mod_time = int.from_bytes(data[22:24], "little")
        self.last_mod_date = int.from_bytes(data[24:26], "little")
        low_cluster = int.from_bytes(data[26:28], "little")
        self.cluster = (high_cluster << 16) | low_cluster
        self.size_bytes = int.from_bytes(data[28:32], "little")

        # extra (to help write back to disk)
        self.sector = sector
        self.offset = offset

    def as_bytes(self) -> bytes:
        return (
            self.name.ljust(8, " ").encode("ascii")
            + self.ext.ljust(3, " ").encode("ascii")
            + self.attributes.to_bytes(1, "little")
            + self.reserved.to_bytes(1, "little")
            + self.create_time_tenth.to_bytes(1, "little")
            + self.create_time.to_bytes(2, "little")
            + self.create_date.to_bytes(2, "little")
            + self.last_access_date.to_bytes(2, "little")
            + (self.cluster >> 16).to_bytes(2, "little")
            + self.last_mod_time.to_bytes(2, "little")
            + self.last_mod_date.to_bytes(2, "little")
            + (self.cluster & 0xFFFF).to_bytes(2, "little")
            + self.size_bytes.to_bytes(4, "little")
        )

    def whole_name(self):
        if self.ext:
            return f"{self.name}.{self.ext}"
        else:
            return self.name

    def __str__(self):
        return (
            f"Name: {self.name}\n"
            f"Ext: {self.ext}\n"
            f"Attributes: {self.attributes}\n"
            f"Reserved: {self.reserved}\n"
            f"Create time tenth: {self.create_time_tenth}\n"
            f"Create time: {self.create_time}\n"
            f"Create date: {self.create_date}\n"
            f"Last access date: {self.last_access_date}\n"
            f"Last mod time: {self.last_mod_time}\n"
            f"Last mod date: {self.last_mod_date}\n"
            f"Cluster: {self.cluster}\n"
            f"Size: {self.size_bytes}\n"
        )

    def __repr__(self):
        # convert to dict
        return str(vars(self))


class Fat16:
    def __init__(
        self,
        start_sector: int,
        size: int,
        sector_reader: callable,
        sector_writer: callable,
    ):
        self.start_sector = start_sector
        self.size_in_sectors = size
        self.sector_reader = sector_reader
        self.sector_writer = sector_writer

        self.boot_sector = FatBootSector(self.sector_reader(start_sector))

        fat_size_in_sectors = self.boot_sector.fat_size * self.boot_sector.fat_count
        self.fats = self.read_sectors(
            self.boot_sector.reserved_sectors, fat_size_in_sectors
        )
        self.fats_dirty_sectors = set()

    def read_sectors(self, start_sector: int, num_sectors: int) -> bytes:
        return self.sector_reader(start_sector + self.start_sector, num_sectors)

    def write_sectors(self, start_sector: int, data: bytes):
        return self.sector_writer(start_sector + self.start_sector, data)

    def directory_from_bytes(
        self, data: bytes, start_sector: int
    ) -> List[FatDirectoryEntry]:
        """
        Convert `bytes` into a list of `FatDirectoryEntry` objects.
        Will ignore long file names.
        Will stop when it encounters a 0x00 byte.
        """

        entries = []
        for i in range(0, len(data), DIRENTRY_SIZE):
            entry = data[i : i + DIRENTRY_SIZE]

            current_sector = start_sector + (i // SECTOR_SIZE)
            current_offset = i % SECTOR_SIZE

            if entry[0] == 0:
                break
            elif entry[0] == 0xE5:
                # Deleted file
                continue

            if entry[11] & 0xF == 0xF:
                # Long file name
                continue

            entries.append(FatDirectoryEntry(entry, current_sector, current_offset))
        return entries

    def read_root_directory(self) -> List[FatDirectoryEntry]:
        root_dir = self.read_sectors(
            self.boot_sector.root_dir_start(), self.boot_sector.root_dir_size()
        )
        return self.directory_from_bytes(root_dir, self.boot_sector.root_dir_start())

    def read_fat_entry(self, cluster: int) -> int:
        """
        Read the FAT entry for the given cluster.
        """
        fat_offset = cluster * 2  # FAT16
        return int.from_bytes(self.fats[fat_offset : fat_offset + 2], "little")

    def write_fat_entry(self, cluster: int, value: int):
        """
        Write the FAT entry for the given cluster.
        """
        fat_offset = cluster * 2
        self.fats = (
            self.fats[:fat_offset]
            + value.to_bytes(2, "little")
            + self.fats[fat_offset + 2 :]
        )
        self.fats_dirty_sectors.add(fat_offset // SECTOR_SIZE)
    
    def flush_fats(self):
        """
        Write the FATs back to the disk.
        """
        for sector in self.fats_dirty_sectors:
            data = self.fats[sector * SECTOR_SIZE : (sector + 1) * SECTOR_SIZE]
            sector = self.boot_sector.reserved_sectors + sector
            self.write_sectors(sector, data)
        self.fats_dirty_sectors = set()

    def next_cluster(self, cluster: int) -> int | None:
        """
        Get the next cluster in the chain.
        If its `None`, then its the last cluster.
        The function will crash if the next cluster is `FREE` (unexpected) or invalid entry.
        """
        fat_entry = self.read_fat_entry(cluster)
        if fat_entry == 0:
            raise Exception("Unexpected: FREE cluster")
        elif fat_entry == 1:
            raise Exception("Unexpected: RESERVED cluster")
        elif fat_entry >= 0xFFF8:
            return None
        elif fat_entry >= 0xFFF7:
            raise Exception("Invalid FAT entry")
        else:
            return fat_entry
    
    def next_free_cluster(self) -> int:
        """
        Find the next free cluster.
        """
        # simple linear search
        for i in range(2, 0xFFFF):
            if self.read_fat_entry(i) == 0:
                return i
        raise Exception("No free clusters")

    def read_cluster(self, cluster: int) -> bytes:
        """
        Read the cluster at the given cluster.
        """
        return self.read_sectors(
            self.boot_sector.first_sector_of_cluster(cluster),
            self.boot_sector.sectors_per_cluster,
        )

    def write_cluster(self, cluster: int, data: bytes):
        """
        Write the cluster at the given cluster.
        """
        assert len(data) == self.boot_sector.cluster_bytes()
        return self.write_sectors(
            self.boot_sector.first_sector_of_cluster(cluster),
            data,
        )

    def read_directory(self, cluster: int) -> List[FatDirectoryEntry]:
        """
        Read the directory at the given cluster.
        """
        entries = []
        while cluster is not None:
            data = self.read_cluster(cluster)
            entries.extend(
                self.directory_from_bytes(
                    data, self.boot_sector.first_sector_of_cluster(cluster)
                )
            )
            cluster = self.next_cluster(cluster)
        return entries

    def update_direntry(self, entry: FatDirectoryEntry):
        """
        Write the directory entry back to the disk.
        """
        sector = self.read_sectors(entry.sector, 1)
        sector = (
            sector[: entry.offset]
            + entry.as_bytes()
            + sector[entry.offset + DIRENTRY_SIZE :]
        )
        self.write_sectors(entry.sector, sector)

    def find_direntry(self, path: str) -> FatDirectoryEntry | None:
        """
        Find the directory entry for the given path.
        """
        assert path[0] == "/", "Path must start with /"

        path = path[1:]  # remove the leading /
        parts = path.split("/")
        directory = self.read_root_directory()

        current_entry = None

        for i, part in enumerate(parts):
            is_last = i == len(parts) - 1

            for entry in directory:
                if entry.whole_name() == part:
                    current_entry = entry
                    break
            if current_entry is None:
                return None

            if is_last:
                return current_entry
            else:
                if current_entry.attributes & 0x10 == 0:
                    raise Exception(f"{current_entry.whole_name()} is not a directory")
                else:
                    directory = self.read_directory(current_entry.cluster)

    def read_file(self, entry: FatDirectoryEntry) -> bytes:
        """
        Read the content of the file at the given path.
        """
        if entry is None:
            return None
        if entry.attributes & 0x10 != 0:
            raise Exception(f"{entry.whole_name()} is a directory")

        data = b""
        cluster = entry.cluster
        while cluster is not None and len(data) <= entry.size_bytes:
            data += self.read_cluster(cluster)
            cluster = self.next_cluster(cluster)
        return data[: entry.size_bytes]

    def truncate_file(self, entry: FatDirectoryEntry, new_size: int):
        """
        Truncate the file at the given path to the new size.
        """
        if entry is None:
            return Exception("entry is None")
        if entry.attributes & 0x10 != 0:
            raise Exception(f"{entry.whole_name()} is a directory")

        def clusters_from_size(size: int):
            return (size + self.boot_sector.cluster_bytes() - 1) // self.boot_sector.cluster_bytes()

        
        # First, allocate new FATs if we need to
        required_clusters = clusters_from_size(new_size)
        current_clusters = clusters_from_size(entry.size_bytes)

        affected_clusters = set()

        # Keep at least one cluster, easier to manage this way
        if required_clusters == 0:
            required_clusters = 1
        if current_clusters == 0:
            current_clusters = 1

        if required_clusters > current_clusters:
            # Allocate new clusters
            cluster = entry.cluster
            to_add = required_clusters
            for _ in range(current_clusters - 1):
                to_add -= 1
                cluster = self.next_cluster(cluster)
            assert required_clusters > 0, "No new clusters to allocate"
            assert cluster is not None, "Cluster is None"
            assert self.next_cluster(cluster) is None, "Cluster is not the last cluster"

            # Allocate new clusters
            for _ in range(to_add - 1):
                new_cluster = self.next_free_cluster()
                self.write_fat_entry(cluster, new_cluster)
                self.write_fat_entry(new_cluster, 0xFFFF)
                cluster = new_cluster
            
        elif required_clusters < current_clusters:
            # Truncate the file
            cluster = entry.cluster
            for _ in range(required_clusters - 1):
                cluster = self.next_cluster(cluster)
            assert cluster is not None, "Cluster is None"

            next_cluster = self.next_cluster(cluster)
            # mark last as EOF
            self.write_fat_entry(cluster, 0xFFFF)
            # free the rest
            while next_cluster is not None:
                cluster = next_cluster
                next_cluster = self.next_cluster(next_cluster)
                self.write_fat_entry(cluster, 0)

        self.flush_fats()

        # verify number of clusters
        cluster = entry.cluster
        count = 0
        while cluster is not None:
            count += 1
            affected_clusters.add(cluster)
            cluster = self.next_cluster(cluster)
        assert count == required_clusters, f"Expected {required_clusters} clusters, got {count}"

        # update the size
        entry.size_bytes = new_size
        self.update_direntry(entry)

        # trigger every affected cluster
        for cluster in affected_clusters:
            first_sector = self.boot_sector.first_sector_of_cluster(cluster)
            first_sector_data = self.read_sectors(first_sector, 1)
            self.write_sectors(first_sector, first_sector_data)

    def write_file(self, entry: FatDirectoryEntry, data: bytes):
        """
        Write the content of the file at the given path.
        """
        if entry is None:
            return Exception("entry is None")
        if entry.attributes & 0x10 != 0:
            raise Exception(f"{entry.whole_name()} is a directory")

        data_len = len(data)

        self.truncate_file(entry, data_len)

        cluster = entry.cluster
        while cluster is not None:
            data_to_write = data[: self.boot_sector.cluster_bytes()]
            last_data = False
            if len(data_to_write) < self.boot_sector.cluster_bytes():
                last_data = True
                old_data = self.read_cluster(cluster)
                data_to_write += old_data[len(data_to_write) :]

            self.write_cluster(cluster, data_to_write)
            data = data[self.boot_sector.cluster_bytes() :]
            if len(data) == 0:
                break
            cluster = self.next_cluster(cluster)

        assert len(data) == 0, "Data was not written completely, clusters missing"
