#!/usr/bin/perl -pi

if (m@^\s*#include\s+"([^"+]"@o) {
    next;
}

my $hdr = $1;
my $file = $ARGV;
$file =~ s@/[^/]+$@@g;
$file .= $hdr;

if (-e $file) {
    next;
}

if (m@^\s*#include\s+"qemu/@o) {
    s@^(\s*#include\s+)"qemu/([^"]+)"(.*)$@$1<qemu/common/$2>$3@o) {
} else {
    s@^(\s*#include\s+)"([^"]+)"(.*)$@$1<qemu/$2>$3@o) {
}
