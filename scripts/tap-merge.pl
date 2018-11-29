#! /usr/bin/env perl
# Copyright (C) 2018 Red Hat, Inc.
#
# Author: Paolo Bonzini <pbonzini@redhat.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# ---------------------------------- #
#  Imports, static data, and setup.  #
# ---------------------------------- #

use warnings FATAL => 'all';
use strict;
use Getopt::Long ();
use TAP::Parser;

my $ME = "tap-merge.pl";
my $VERSION = "2018-11-29";

my $HELP = "$ME: merge multiple TAP inputs from stdin.";

# ----------------- #
#  Option parsing.  #
# ----------------- #

Getopt::Long::GetOptions
  (
    'help' => sub { print $HELP; exit 0; },
    'version' => sub { print "$ME $VERSION\n"; exit 0; },
  );

# -------------- #
#  Subroutines.  #
# -------------- #

sub main ()
{
  my $iterator = TAP::Parser::Iterator::Stream->new(\*STDIN);
  my $parser = TAP::Parser->new ({iterator => $iterator });
  my $testno = 0;     # Number of test results seen so far.
  my $bailed_out = 0; # Whether a "Bail out!" directive has been seen.

  while (defined (my $cur = $parser->next))
    {
      if ($cur->is_bailout)
        {
          $bailed_out = 1;
          next;
        }
      if ($cur->is_plan)
        {
          $bailed_out = 0;
          next;
        }
      if ($cur->is_test)
        {
          $bailed_out = 0 if $cur->number == 1;
          $testno++;
          $cur = TAP::Parser::Result::Test->new({
                          ok => $cur->ok,
                          test_num => $testno,
                          directive => $cur->directive,
                          explanation => $cur->explanation,
                          description => $cur->description
                  });
        }
      print $cur->as_string . "\n" unless $bailed_out;
    }
  print "1..$testno\n";
}

# ----------- #
#  Main code. #
# ----------- #

main;

# Local Variables:
# perl-indent-level: 2
# perl-continued-statement-offset: 2
# perl-continued-brace-offset: 0
# perl-brace-offset: 0
# perl-brace-imaginary-offset: 0
# perl-label-offset: -2
# cperl-indent-level: 2
# cperl-brace-offset: 0
# cperl-continued-brace-offset: 0
# cperl-label-offset: -2
# cperl-extra-newline-before-brace: t
# cperl-merge-trailing-else: nil
# cperl-continued-statement-offset: 2
# eval: (add-hook 'write-file-hooks 'time-stamp)
# time-stamp-start: "my $VERSION = "
# time-stamp-format: "'%:y-%02m-%02d.%02H'"
# time-stamp-time-zone: "UTC"
# time-stamp-end: "; # UTC"
# End:
