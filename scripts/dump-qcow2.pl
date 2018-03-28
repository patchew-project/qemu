#!/usr/bin/env perl
# Copyright (C) 2018 Igalia, S.L.
#
# Authors:
#  Alberto Garcia <berto@igalia.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

use strict;
use warnings;
use Fcntl qw(SEEK_SET SEEK_CUR);
use POSIX qw(ceil);
use Getopt::Long;

sub showUsage {
    print "Usage: dump-qcow2.pl [options] <qcow2-file>\n";
    print "\nOptions:\n";
    print "\t--host           Show host cluster types\n";
    print "\t--guest          Show guest cluster mappings\n";
    print "\t--snapshot=<id>  Use this snapshot's L1 table for the mappings\n";
    exit 0;
}

# Command-line parameters
my $printHost;
my $printGuest;
my $snapshot = '';
GetOptions('guest' => \$printGuest,
           'host' => \$printHost,
           'snapshot=s' => \$snapshot) or showUsage();

# Open the qcow2 image
@ARGV == 1 or showUsage();
my $file = $ARGV[0];
-e $file or die "File '$file' not found\n";
open(FILE, $file) or die "Cannot open '$file'\n";

# Some global variables
my $refcount_order = 4;
my $file_size = -s $file;
my $header_size = 72;
my $incompat_bits;
my $compat_bits;
my $autoclear_bits;
my @l2_tables;
my @clusters = ( "QCOW2 Header" ); # Contents of host clusters
my @crypt_methods = qw(no AES LUKS);

# Read all common header fields
die "Not a qcow2 file\n" if (read_uint32() != 0x514649fb);
my $qcow2_version   = read_uint32();
my $backing_offset  = read_uint64();
my $backing_size    = read_uint32();
my $cluster_bits    = read_uint32();
my $virtual_size    = read_uint64();
my $crypt_method    = read_uint32();
my $l1_size         = read_uint32();
my $l1_offset       = read_uint64();
my $refcount_offset = read_uint64();
my $refcount_size   = read_uint32();
my $n_snapshots     = read_uint32();
my $snapshot_offset = read_uint64();

if ($qcow2_version != 2 && $qcow2_version != 3) {
    die "Unknown QCOW2 version: $qcow2_version\n";
}

# Read v3-specific fields
if ($qcow2_version == 3) {
    $incompat_bits  = read_uint64();
    $compat_bits    = read_uint64();
    $autoclear_bits = read_uint64();
    $refcount_order = read_uint32();
    $header_size    = read_uint32();
}

# Values calculated from the header fields
my $cluster_size     = 1 << $cluster_bits;
my $refcount_bits    = 1 << $refcount_order;
my $n_clusters       = ceil($file_size / $cluster_size);
my $l1_size_clusters = ceil($l1_size * 8 / $cluster_size);
my $l1_size_expected = ceil($virtual_size * 8 / $cluster_size / $cluster_size);

foreach my $i (0..$l1_size_clusters - 1) {
    $clusters[ncluster($l1_offset) + $i] = "L1 table $i [active]";
}
foreach my $i (0..$refcount_size-1) {
    $clusters[ncluster($refcount_offset) + $i] = "Refcount table $i";
}
if ($snapshot_offset > 0) {
    $clusters[ncluster($snapshot_offset)] = "Snapshot table";
}

# Print a summary of the header
print "QCOW2 version $qcow2_version\n";
print "Header size: $header_size bytes\n";
print "File size: ".prettyBytes($file_size)."\n";
print "Virtual size: ".prettyBytes($virtual_size)."\n";
print "Cluster size: ".prettyBytes($cluster_size)." ($n_clusters clusters)\n";
if ($backing_offset != 0) {
    sysseek(FILE, $backing_offset, SEEK_SET);
    my $backing_name = read_str($backing_size);
    printf("Backing file offset: 0x%u (%u bytes): %s\n",
           $backing_offset, $backing_size, $backing_name);
}
print "Number of snapshots: $n_snapshots\n";
printf("Refcount table: %u cluster(s) (%u entries) ".
       "at 0x%x (#%u)\n", $refcount_size,
       $refcount_size * $cluster_size / 8,
       $refcount_offset, ncluster($refcount_offset));
printf("Refcount entry size: %u bits (%u entries per block)\n",
       $refcount_bits, $cluster_size * 8 / $refcount_bits);
printf("L1 table: %u entries at 0x%x (#%u)\n",
       $l1_size, $l1_offset, ncluster($l1_offset));
if ($l1_size != $l1_size_expected) {
    printf("Expected L1 entries based on the virtual size: %u\n",
           $l1_size_expected);
}
printf("Encrypted: %s\n", $crypt_methods[$crypt_method] || "unknown method");

# Header extensions and additional sections
if ($qcow2_version == 3) {
    print "Incompatible bits:";
    print " none" if ($incompat_bits == 0);
    print " dirty" if ($incompat_bits & 1);
    print " corrupted" if ($incompat_bits & 2);
    print " unknown" if ($incompat_bits >> 2);
    print "\n";

    print "Compatible bits:";
    print " none" if ($compat_bits == 0);
    print " lazy_refcounts" if ($compat_bits & 1);
    print " unknown" if ($compat_bits >> 1);
    print "\n";

    print "Autoclear bits:";
    print " none" if ($autoclear_bits == 0);
    print " bitmaps_extension" if ($autoclear_bits & 1);
    print " unknown" if ($autoclear_bits >> 1);
    print "\n";

    my ($i, $hdr_type, $hdr_len) = (0);
    sysseek(FILE, $header_size, SEEK_SET);
    do {
        $hdr_type = read_uint32();
        $hdr_len = (read_uint32() + 7) & ~7; # Round to the next multiple of 8
        if ($hdr_type == 0) {
            # No more extensions
        } elsif ($hdr_type == 0xE2792ACA) {
            print "Header extension $i: backing file format name\n";
        } elsif ($hdr_type == 0x6803f857) {
            print "Header extension $i: feature table name\n";
            for (my $ft_num = 0; $hdr_len >= 48; $hdr_len -= 48) {
                my @ft_types = ("incompatible", "compatible", "autoclear");
                my $ft_type = read_uint8();
                my $ft_bit = read_uint8();
                my $ft_name = read_str(46);
                printf("    Feature %d: %s (%s, bit %d)\n",
                       $ft_num++, $ft_name, $ft_types[$ft_type], $ft_bit);
            }
        } elsif ($hdr_type == 0x23852875) {
            print "Header extension $i: bitmaps extension\n";
        } elsif ($hdr_type == 0x0537be77) {
            print "Header extension $i: full disk encryption\n";
            my $fde_offset = read_uint64();
            my $fde_length = read_uint64();
            $hdr_len -= 16;
            my $fde_cluster = ncluster($fde_offset);
            my $fde_nclusters = ceil($fde_length / $cluster_size);
            foreach my $i (0..$fde_nclusters-1) {
                $clusters[$fde_cluster + $i] = "Full disk encryption header $i";
            }
            printf("    Offset 0x%x (#%u), length %u bytes (%u clusters)\n",
                   $fde_offset, $fde_cluster, $fde_length, $fde_nclusters);
        } else {
            printf("Header extension $i: unknown (0x%x)\n", $hdr_type);
        }
        sysseek(FILE, $hdr_len, SEEK_CUR);
        $i++;
    } while ($hdr_type != 0);
}

if ($incompat_bits >> 2) {
    die "Incompatible bits found, aborting\n";
}

# Read the snapshot table
my %snap_l1_offsets = read_snapshot_table();
if ($snapshot ne '' && !defined($snap_l1_offsets{$snapshot})) {
    die "Snapshot $snapshot not found\n";
}

read_refcount_table();

# Read and parse the active L1/L2 tables
my @l1_table = read_l1_table($l1_offset);
read_l2_tables("active", \@l1_table, $snapshot eq "");

# Read all L1/L2 tables from all snapshots
foreach my $id (keys(%snap_l1_offsets)) {
    my $off = $snap_l1_offsets{$id};
    @l1_table = read_l1_table($off);
    read_l2_tables("snapshot $id", \@l1_table, $snapshot eq $id);
}

close(FILE);

printHostClusters() if $printHost;
printGuestClusters() if $printGuest;

exit 0;

# Subroutines

sub read_l1_table {
    my $offset = shift;
    my @table;
    sysseek(FILE, $offset, SEEK_SET);
    foreach my $i (0..$l1_size-1) {
        $table[$i] = read_uint64();
    }
    return @table;
}

sub read_l2_tables {
    my $name = shift;
    my $l1_table = shift;
    my $selected_l1_table = shift;
    foreach my $i (0..$#{$l1_table}) {
        my $l2_offset = ${$l1_table}[$i] & ((1<<56) - (1<<9));
        if ($l2_offset != 0) {
            my $l2_table =
                read_one_l2_table($i, $l2_offset, $selected_l1_table);
            if ($selected_l1_table) {
                $l2_tables[$i] = $l2_table;
            }
            $clusters[ncluster($l2_offset)] = "L2 table $i [$name]";
        }
    }
}

sub read_one_l2_table {
    my $table_num = shift;
    my $l2_offset = shift;
    my $selected_l1_table = shift;
    my $num_l2_entries = $cluster_size / 8;
    my @l2_table;
    sysseek(FILE, $l2_offset, SEEK_SET);
    foreach my $j (0..$num_l2_entries-1) {
        my $entry = read_uint64();
        my $compressed = ($entry >> 62) & 1;
        if ($compressed) {
            my $csize_shift = 62 - ($cluster_bits - 8);
            my $csize_mask = (1 << ($cluster_bits - 8)) - 1;
            my $csize = (($entry >> $csize_shift) & $csize_mask) + 1;
            my $coffset = $entry & ((1 << $csize_shift) - 1);
            my $cluster1 = ncluster($coffset);
            my $cluster2 = ncluster($coffset + ($csize * 512) - 1);
            foreach my $k ($cluster1..$cluster2) {
                if (!$clusters[$k]) {
                    $clusters[$k] = "Compressed cluster";
                }
            }
            if ($cluster1 == $cluster2) {
                $l2_table[$j] =
                    sprintf("0x%x (#%u) (compressed, %d sectors)",
                            $coffset, $cluster1, $csize);
            } else {
                $l2_table[$j] =
                    sprintf("0x%x (#%u - #%u) (compressed, %d sectors)",
                            $coffset, $cluster1, $cluster2, $csize);
            }
        } else {
            my $guest_off = ($table_num << ($cluster_bits * 2 - 3)) +
                            ($j << $cluster_bits);
            my $host_off = $entry & ((1<<56) - (1<<9));
            my $host_cluster = ncluster($host_off);
            my $all_zeros = $entry & 1;
            if ($all_zeros) {
                if ($host_off != 0) {
                    $l2_table[$j] = sprintf("0x%x (#%u) (all zeros)",
                                            $host_off, ncluster($host_off));
                    if ($selected_l1_table) {
                        $clusters[$host_cluster] =
                            sprintf("All zeros [guest 0x%x (#%u)]",
                                    $guest_off, ncluster($guest_off));
                    } elsif (!$clusters[$host_cluster]) {
                        $clusters[$host_cluster] = "All zeros";
                    }
                } else {
                    $l2_table[$j] = "All zeros";
                }
            } else {
                if ($host_off != 0) {
                    $l2_table[$j] = sprintf("0x%x (#%u)",
                                            $host_off, ncluster($host_off));
                    if ($selected_l1_table) {
                        $clusters[$host_cluster] =
                            sprintf("Data cluster [guest 0x%x (#%u)]",
                                    $guest_off, ncluster($guest_off));
                    } elsif (!$clusters[$host_cluster]) {
                        $clusters[$host_cluster] = "Data cluster";
                    }
                }
            }
        }
    }
    return \@l2_table;
}

sub read_refcount_table {
    my $n_entries = $refcount_size * $cluster_size / 8;
    sysseek(FILE, $refcount_offset, SEEK_SET);
    foreach my $i (0..$n_entries-1) {
        my $entry = read_uint64() & ~511;
        if ($entry != 0) {
            $clusters[ncluster($entry)] = "Refcount block $i";
        }
    }
}

sub read_snapshot_table {
    my %l1_offsets;
    sysseek(FILE, $snapshot_offset, SEEK_SET);
    for (my $i = 0; $i < $n_snapshots; $i++) {
        my $snap_l1_offset = read_uint64();
        my $snap_l1_size = read_uint32();
        my $snap_l1_size_clusters = ceil($snap_l1_size * 8 / $cluster_size);
        my $snap_id_len = read_uint16();
        my $snap_name_len = read_uint16();
        sysseek(FILE, 20, SEEK_CUR);
        my $snap_extra_len = read_uint32();
        sysseek(FILE, $snap_extra_len, SEEK_CUR);
        my $snap_id = read_str($snap_id_len);
        my $snap_name = read_str($snap_name_len);

        my $snap_var_len = $snap_id_len + $snap_name_len + $snap_extra_len;
        if ($snap_var_len & 7) {
            sysseek(FILE, 8 - ($snap_var_len & 7), SEEK_CUR);
        }

        $l1_offsets{$snap_id} = $snap_l1_offset;

        foreach my $i (0..$snap_l1_size_clusters - 1) {
            $clusters[ncluster($snap_l1_offset) + $i] =
                "L1 table $i [snapshot $snap_id]";
        }
        print "\nSnapshot #$snap_id ($snap_name)\n";
        printf("L1 table: %u entries at 0x%x (#%u)\n",
               $snap_l1_size, $snap_l1_offset, ncluster($snap_l1_offset));
    }
    return %l1_offsets;
}

sub read_uint8 {
    my $data;
    sysread(FILE, $data, 1);
    return unpack('C', $data);
}

sub read_uint16 {
    my $data;
    sysread(FILE, $data, 2);
    return unpack('S>', $data);
}

sub read_uint32 {
    my $data;
    sysread(FILE, $data, 4);
    return unpack('L>', $data);
}

sub read_uint64 {
    my $data;
    sysread(FILE, $data, 8);
    return unpack('Q>', $data);
}

sub read_str {
    my $maxlen = shift;
    my $data;
    sysread(FILE, $data, $maxlen);
    if (index($data,chr(0)) != -1) {
        return substr($data, 0, index($data,chr(0))); # Strip trailing NULLs
    } else {
        return $data;
    }
}

sub prettyBytes {
    my $size = $_[0];
    foreach ('B','KiB','MiB','GiB','TiB','PiB','EiB') {
        return sprintf("%.2f ",$size)."$_" if $size < 1024;
        $size /= 1024;
    }
}

sub printHostClusters {
    print "\nHost clusters\n";
    foreach my $i (0..$n_clusters-1) {
        if ($clusters[$i]) {
            printf "0x%x (#%u): $clusters[$i]\n", $i * $cluster_size, $i;
        }
    }
}

sub printGuestClusters {
    print "\nGuest address -> Host address\n";
    for (my $i = 0; $i < $virtual_size; $i += $cluster_size) {
        my $l1_index = $i >> ($cluster_bits * 2 - 3);
        my $l2_index = ncluster($i) & ((1 << ($cluster_bits - 3)) - 1);
        my $l2_table = $l2_tables[$l1_index];
        my $mapping = ${$l2_table}[$l2_index];
        if ($mapping) {
            printf("0x%x (#%u): %s\n", $i, ncluster($i), $mapping);
        }
    }
}

# Return cluster number from its absolute offset
sub ncluster {
    my $offset = shift;
    return $offset >> $cluster_bits;
}
