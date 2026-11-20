#!/usr/bin/env python3
#
# get_maintainer.py: Print selected MAINTAINERS information
#
# usage: scripts/get_maintainer.py [OPTIONS] <patch>
#        scripts/get_maintainer.py [OPTIONS] -f <file>
#
# (c) 2025, Alex Bennée <alex.bennee@linaro.org>
#           based on get_maintainers.pl
#
# SPDX-License-Identifier: GPL-2.0-or-later

import sys
from argparse import Action, ArgumentParser, ArgumentTypeError, BooleanOptionalAction
from dataclasses import dataclass
from os import path
from pathlib import Path
from enum import StrEnum, auto
from fnmatch import translate as fn_translate
from re import compile as re_compile
from re import sub as re_sub
from re import IGNORECASE
from regex import compile as prec_compile
from git import Repo
from collections import Counter, defaultdict
from csv import writer as csv_writer
from datetime import datetime, timezone

#
# Subsystem MAINTAINER entries
#
# The MAINTAINERS file is an unstructured text file where the
# important information is in lines that follow the form:
#
# X: some data
#
# where X is a documented tag and the data is variously an email,
# path, regex or link. Other lines should be ignored except the
# preceding non-blank or underlined line which represents the name of
# the "subsystem" or general area of the project.
#
# A blank line denominates the end of a section.
#

tag_re = re_compile(r"^([A-Z]):")


class UnhandledTag(Exception):
    "Exception for unhandled tags"


class BadStatus(Exception):
    "Exception for unknown status"


class Status(StrEnum):
    "Maintenance status"

    UNKNOWN = auto()
    SUPPORTED = 'Supported'
    MAINTAINED = 'Maintained'
    ODD_FIXES = 'Odd Fixes'
    ORPHAN = 'Orphan'
    OBSOLETE = 'Obsolete'

    @classmethod
    def _missing_(cls, value):
        # _missing_ is only invoked by the enum machinery if 'value'
        # does not match any existing enum member's value. So, if we
        # reach this point, 'value' is inherently invalid for this
        # enum.
        raise BadStatus(f"'{value}' is not a valid maintenance status.")


#
# Helpers for GitLab handles
#


def load_gitlab_handles(src):
    """
    Load GitLab handle to name mappings from .gitlab-map-auto and
    .gitlab-map-manual. Returns a dictionary mapping name to handle.
    """
    name_to_handle = {}
    for mapfile in ['.gitlab-map-auto', '.gitlab-map-manual']:
        p = path.join(src, mapfile)
        if path.exists(p):
            with open(p, 'r', encoding='utf-8') as f:
                for line in f:
                    if not line.startswith('#'):
                        parts = line.rstrip('\r\n').split('\t', 1)
                        if len(parts) == 2:
                            name_to_handle[parts[1]] = parts[0]
    return name_to_handle


def map_gitlab_handle(name, gitlab_map):
    """
    Lookup GitLab handle for a real name, stripping quotes.
    """
    if not gitlab_map or not name:
        return None
    clean_name = name.replace('"', '')
    return gitlab_map.get(clean_name)


person_re = re_compile(r"^(?P<name>[^<]+?)\s*<(?P<email>[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,})>$")


class BadPerson(Exception):
    "Exception for un-parsable person"


class Person:
    "Class representing a maintainer or reviewer and their details"

    def __init__(self, info, gitlab_map=None):
        match = person_re.search(info)

        if match is None:
            raise BadPerson(f"Failed to parse {info}")

        self.name = match.group('name')
        self.email = match.group('email')
        if gitlab_map is not None:
            self.handle = map_gitlab_handle(self.name, gitlab_map)
        else:
            self.handle = None

    def __str__(self):
        return f"{self.name} <{self.email}>"

    def format_role(self, role: str) -> str:
        if self.handle:
            return f"{role}, gitlab:@{self.handle}"
        return role


class MaintainerSection:
    "Class representing a section of MAINTAINERS"

    def _expand(self, pattern):
        if pattern.endswith("/"):
            pattern = f"{pattern}*"
        return re_compile(fn_translate(pattern))

    def __init__(self, section, entries, gitlab_map=None):
        self.section = section
        self.status = Status.UNKNOWN
        self.maintainers = []
        self.reviewers = []
        self.files = []
        self.files_exclude = []
        self.trees = []
        self.lists = []
        self.web = []
        self.keywords = []

        for e in entries:
            (tag, data) = e.split(": ", 2)

            if tag == "M":
                person = Person(data, gitlab_map=gitlab_map)
                self.maintainers.append(person)
            elif tag == "R":
                person = Person(data, gitlab_map=gitlab_map)
                self.reviewers.append(person)
            elif tag == "S":
                self.status = Status(data)
            elif tag == "L":
                self.lists.append(data)
            elif tag == 'F':
                pat = self._expand(data)
                self.files.append(pat)
            elif tag == 'W':
                self.web.append(data)
            elif tag == 'K':
                match = prec_compile(data)
                self.keywords.append(match)
            elif tag == 'T':
                self.trees.append(data)
            elif tag == 'X':
                pat = self._expand(data)
                self.files_exclude.append(pat)
            else:
                raise UnhandledTag(f"'{tag}' is not understood.")

    def __str__(self) -> str:
        entries = []

        for m in self.maintainers:
            role = m.format_role(f"maintainer: {self.section}")
            entries.append(f"{m} ({role})")

        for r in self.reviewers:
            role = r.format_role(f"reviewer: {self.section}")
            entries.append(f"{r} ({role})")

        for l in self.lists:
            entries.append(f"{l} (open list: {self.section})")

        return "\n".join(entries)

    def is_file_covered(self, filename):
        "Is filename covered by this maintainer section"

        fn = str(filename)

        for fx in self.files_exclude:
            if fx.match(fn):
                return False

        for f in self.files:
            if f.match(fn):
                return True

        return False

    def patch_contains_keyword(self, patch_lines):
        "Does patch contain keyword covered by this maintainer section"

        for l in patch_lines:
            for k in self.keywords:
                if k.findall(l):
                    return True

        return False


def read_maintainers(src, gitlab_map=None):
    """
    Read the MAINTAINERS file, return a list of MaintainerSection objects.
    """
    mfile = path.join(src, 'MAINTAINERS')
    entries = []

    section = None
    fields = []

    with open(mfile, 'r', encoding='utf-8') as f:
        for line in f:
            if not line.strip():  # Blank line found, potential end of a section
                if section and fields:
                    new_section = MaintainerSection(section, fields, gitlab_map)
                    entries.append(new_section)
                # reset for next section
                section = None
                fields = []
            elif tag_re.match(line):
                fields.append(line.strip())
            else:
                if line.startswith("-") or line.startswith("="):
                    continue

                section = line.strip()

        if section and fields:
            new_section = MaintainerSection(section, fields, gitlab_map)
            entries.append(new_section)

    return entries

#
# Helper functions for dealing with patch files
#


patchfile_re = re_compile(r"\+\+\+\s+(\S+)")


def process_patch_file(patchfile):
    """
    Read a patchfile and return the message as a string and a list
    of files which are modified by the patch.
    """
    file_list = []
    msg = []

    with open(patchfile, 'r', encoding='utf-8') as f:
        for line in f:
            msg.append(line)
            m = patchfile_re.match(line)
            if m:
                # strip leading [ab]/
                stripped = re_sub(r'^[^/]*/', '', m.group(1))
                as_path = Path(path.abspath(stripped))
                file_list.append(as_path)

    return (msg, file_list)

#
# Helpers for querying git
#


@dataclass
class GitOptions:
    repo: Repo
    singers: bool
    since: str
    min_sig: int
    max_maint: int
    min_percent: int
    gitlab_map: dict = None


def rank_signers(git_opts, all_signers, total_commits):
    """
    Counts signer occurrences and returns a list of (Person, count, percent).
    """
    if total_commits == 0:
        return []

    # Count by email to handle duplicates/mailmap issues
    counts = Counter(s.email for s in all_signers)

    # Keep a map of email -> Person object for the most recent name used
    email_to_person = {p.email: p for p in all_signers}

    ranked_results = []

    # Sort by count descending, then take the top N
    for email, count in counts.most_common(git_opts.max_maint):
        percent = min(100.0, (count / total_commits) * 100)
        if percent >= git_opts.min_percent:
            person = email_to_person[email]
            ranked_results.append((person, count, percent))

    return ranked_results


# regex to extract name/email from *-by: tags
sig_line_re = re_compile(r"^\s*[\w-]+-by:\s*(?P<person_info>.*)", IGNORECASE)


def extract_signers(commit_message, gitlab_map=None):
    """
    Return a list of Persons found in commit.
    """
    signers = []
    for line in commit_message.splitlines():
        match = sig_line_re.match(line)
        if match:
            try:
                p = Person(match.group('person_info'), gitlab_map=gitlab_map)
                signers.append(p)
            except BadPerson:
                continue
    return signers


def extract_from_git(git_opts, src_file):
    """
    Extract 'maintainers' from examining the git history of a file.
    Return an array of Person/role tuples.
    """
    repo = git_opts.repo

    # use the porcelain to fetch the log
    hashes = repo.git.log('--follow', f"--since={git_opts.since}",
                          "--format=%H", '--', src_file).splitlines()

    if len(hashes) <= 0:
        return []

    commits = [repo.commit(h) for h in hashes]

    all_signers = []

    for c in commits:
        all_signers.extend(extract_signers(f"{c.message}",
                                           git_opts.gitlab_map))

    ranked = rank_signers(git_opts, all_signers, len(commits))
    results = []

    for person, count, percent in ranked:
        role = f"commit_signer: {count}/{len(commits)}={percent:.0f}%"
        results.append((person, role, count, percent, len(commits)))

    return results

#
# Helper functions for dealing with the source path
#


def is_qemu_src_root(directory):
    """
    Checks if the given path appears to be the root of a QEMU source tree,
    based on the presence of key files and directories.
    """
    if not path.isdir(directory):
        return False

    required_files = ['COPYING', 'MAINTAINERS', 'Makefile', 'VERSION']
    required_dirs = ['docs', 'linux-user', 'system']

    for f in required_files:
        if not path.isfile(path.join(directory, f)):
            return False
    for d in required_dirs:
        if not path.isdir(path.join(directory, d)):
            return False
    return True


def find_src_root():
    """
    Walks up the directory tree from the script's location
    to find the QEMU source root.
    Returns the absolute path of the root directory if found, or None.
    """
    script_dir = path.dirname(path.abspath(__file__)) # FIXME, breaks python-send-buffer
    current_dir = script_dir

    while True:
        if is_qemu_src_root(current_dir):
            return current_dir

        # Move up to the parent directory
        parent_dir = path.dirname(current_dir)

        # If we reached the filesystem root and haven't found it, stop
        if parent_dir == current_dir:
            break

        current_dir = parent_dir

    return None

#
# Helpers for maintainer statistics
#
# We assume the current state of MAINTAINERS as parsed from the
# working tree is the authoritative source for who is currently a
# maintainer, as well as their canonical name spelling and primary
# email address. Historical commits or older MAINTAINERS revisions
# with alternate name transliterations or older email addresses are
# mapped back onto these current canonical maintainer names.
#


def get_maintainer_since(repo, maintainers, email_to_maint):
    """
    Scan git history of MAINTAINERS to determine the earliest date
    each maintainer was added as a maintainer (M:) or reviewer (R:).
    Returns (maintainer_since, reviewer_since) dictionaries.
    """
    maintainer_since = {}
    reviewer_since = {}
    current_date = None

    tag_re = re_compile(r'^\+\s*([MR]):\s*([^<]+?)\s*<([^>]+)>')

    log_output = repo.git.log('-p', '--format=COMMIT %as', '--', 'MAINTAINERS')
    for line in log_output.splitlines():
        if line.startswith('COMMIT '):
            current_date = line[7:17]
        elif line.startswith('+') and not line.startswith('+++'):
            m = tag_re.match(line)
            if m:
                tag, name, email = m.group(1), m.group(2), m.group(3)
                target = (name if name in maintainers
                          else email_to_maint.get(email))
                if target:
                    since_dict = (maintainer_since if tag == 'M'
                                  else reviewer_since)
                    if (target not in since_dict or
                            current_date < since_dict[target]):
                        since_dict[target] = current_date
    return maintainer_since, reviewer_since


def get_contributor_dates(repo, maintainers, email_to_maint):
    """
    Scan git history for author and committer dates, as well as review/ack
    trailers, to determine the first and last contribution dates for
    maintainers.

    Trailers are dated by the author date, as are MAINTAINERS entries in
    get_maintainer_since(), so a patch adding its own maintainer lines
    doesn't appear to predate the maintainer's involvement.

    Returns (first_contrib, last_contrib) dictionaries mapping maintainer
    canonical names to ISO date strings.
    """
    trailer_keys = [
        'Signed-off-by',
        'Co-developed-by',
        'Reviewed-by',
        'Tested-by',
        'Acked-by',
    ]
    trailer_args = ','.join(f'key={k}' for k in trailer_keys)
    log_format = (
        '%as|%aN|%aE|%cs|%cN|%cE|'
        f'%(trailers:{trailer_args},valueonly,unfold,separator=|)'
    )

    first_contrib = {}
    last_contrib = {}

    def record_date(name, email, date):
        target = name if name in maintainers else email_to_maint.get(email)
        if target:
            first_contrib[target] = min(first_contrib.get(target, date), date)
            last_contrib[target] = max(last_contrib.get(target, date), date)

    for line in repo.git.log(f'--format={log_format}').splitlines():
        if not line:
            continue
        parts = line.split('|')
        a_date, a_name, a_email, c_date, c_name, c_email = parts[:6]

        record_date(a_name, a_email, a_date)
        record_date(c_name, c_email, c_date)

        for trailer in parts[6:]:
            m = person_re.match(trailer.strip())
            if m:
                record_date(m.group('name'), m.group('email'), a_date)

    return first_contrib, last_contrib


def dump_maintainers_csv(src, maint_sections):
    """
    Dump a CSV summary of maintainer statistics to stdout.
    """
    maintainers = defaultdict(lambda: {'subsystems': 0, 'emails': set()})
    for s in maint_sections:
        if s.status not in (Status.ORPHAN, Status.OBSOLETE):
            for m in s.maintainers:
                maintainers[m.name]['subsystems'] += 1
                maintainers[m.name]['emails'].add(m.email)

    email_to_maint = {}
    for name, data in maintainers.items():
        for email in data['emails']:
            email_to_maint[email] = name

    name_to_handle = load_gitlab_handles(src)
    repo = Repo(src)

    maintainer_since, reviewer_since = get_maintainer_since(
        repo, maintainers, email_to_maint
    )
    first_contrib, last_contrib = get_contributor_dates(
        repo, maintainers, email_to_maint
    )

    writer = csv_writer(sys.stdout)
    headers = [
        "Maintainer Name",
        "gitlab handle",
        "Subsystems Maintained",
        "Maintainer Since",
        "Reviewer Since",
        "Contributor Since",
        "Date of last contribution",
    ]
    writer.writerow(headers)
    for name in sorted(maintainers.keys()):
        writer.writerow([
            name,
            name_to_handle.get(name, ""),
            maintainers[name]["subsystems"],
            maintainer_since.get(name, ""),
            reviewer_since.get(name, ""),
            first_contrib.get(name, ""),
            last_contrib.get(name, ""),
        ])


def list_absent_maintainers(src, maint_sections, since_str):
    """
    Pretty print a list of maintainers and reviewers whose last
    contribution was prior to since_str, or who have never contributed.
    """
    people = defaultdict(lambda: {'subsystems': [], 'emails': set()})
    for s in maint_sections:
        if s.status not in (Status.ORPHAN, Status.OBSOLETE):
            for p in s.maintainers + s.reviewers:
                if s.section not in people[p.name]['subsystems']:
                    people[p.name]['subsystems'].append(s.section)
                people[p.name]['emails'].add(p.email)

    email_to_maint = {}
    for name, data in people.items():
        for email in data['emails']:
            email_to_maint[email] = name

    repo = Repo(src)
    try:
        out = repo.git.rev_parse(f'--since={since_str}')
        ts = int(out.split('=')[1])
        cutoff_date = datetime.fromtimestamp(
            ts, tz=timezone.utc
        ).strftime('%Y-%m-%d')
    except Exception as e:
        print(f"Error: failed to parse date '{since_str}': {e}",
              file=sys.stderr)
        sys.exit(1)

    _first, last_contrib = get_contributor_dates(
        repo, people, email_to_maint
    )

    rows = []
    for name, data in people.items():
        last = last_contrib.get(name)
        if last is None:
            rows.append((name, 'Never', ', '.join(sorted(data['emails'])),
                         ', '.join(data['subsystems'])))
        elif last < cutoff_date:
            rows.append((name, last, ', '.join(sorted(data['emails'])),
                         ', '.join(data['subsystems'])))

    # Sort most stale first (Never first, then earliest date, then by name)
    rows.sort(key=lambda x: (1 if x[1] != 'Never' else 0, x[1], x[0]))

    if not rows:
        print(f"No maintainers absent since {cutoff_date} ({since_str}).")
        return

    max_name = max([len(r[0]) for r in rows] + [len('Maintainer')])
    max_date = max([len(r[1]) for r in rows] + [len('Last Seen')])
    max_email = max([len(r[2]) for r in rows] + [len('Email(s)')])

    header = (f"{'Maintainer':<{max_name}}  "
              f"{'Last Seen':<{max_date}}  "
              f"{'Email(s)':<{max_email}}  "
              f"Subsystems")
    sep_len = max_name + 2 + max_date + 2 + max_email + 2 + len('Subsystems')
    separator = '-' * sep_len

    print(header)
    print(separator)
    for r in rows:
        print(f"{r[0]:<{max_name}}  "
              f"{r[1]:<{max_date}}  "
              f"{r[2]:<{max_email}}  "
              f"{r[3]}")

#
# Argument validation
#


def valid_file_path(arg):
    """
    Checks if arg exists and is a regular file or raises ArgumentTypeError.
    """
    if not path.exists(arg):
        raise ArgumentTypeError(f"File '{arg}' does not exist.")
    if not path.isfile(arg) and not path.isdir(arg):
        raise ArgumentTypeError(f"Path '{arg}' is not a regular file.")
    return Path(path.abspath(arg))


def valid_src_root(arg):
    """
    Checks if arg is a valid QEMU source root or raise ArgumentTypeError.
    """
    abs_path = path.abspath(arg)
    if not is_qemu_src_root(abs_path):
        raise ArgumentTypeError(f"Path '{arg}' is not a valid QEMU source tree")
    return Path(abs_path)


def parse_args():
    """
    Create and parse the arguments.
    """
    parser = ArgumentParser(description='Extract maintainer information. ')

    # We can either specify patches or an individual file
    group = parser.add_mutually_exclusive_group()
    group.add_argument('patch', nargs='*', type=valid_file_path,
                       help='path to patch file')
    group.add_argument('-f', '--file', type=valid_file_path, nargs = "+",
                       help='path to source file(s)')

    # Validate MAINTAINERS
    parser.add_argument('--validate',
                        action=BooleanOptionalAction,
                        default=None,
                        help="Just validate MAINTAINERS file")

    # List sections matching regex
    parser.add_argument('--list-maintained-by', metavar='REGEX',
                        help='List sections where maintainer matches REGEX')
    parser.add_argument('--list-reviewed-by', metavar='REGEX',
                        help='List sections where reviewer matches REGEX')

    # List maintainers
    parser.add_argument('--list-maintainers',
                        action=BooleanOptionalAction,
                        default=False,
                        help="Dump maintainers summary as CSV")
    parser.add_argument(
        '--list-absent-maintainers-since', metavar='DATE',
        help="List maintainers with no contributions since DATE")

    # We need to know or be told where the root of the source tree is
    src = find_src_root()

    parser.add_argument('--src', type=valid_src_root, default=src,
                        help=f'Root of QEMU source tree{" (default: " + src + ")" if src else ""}')

    # Git Options
    parser.add_argument('--git', action=BooleanOptionalAction,
                        default=False,
                        help="Include recent git *-by: signers (default: don't)")
    parser.add_argument('--nogit', dest='git', action='store_false',
                        help="Disable git signers (alias for --no-git)")
    parser.add_argument('--git-since', default="1-year-ago",
                        help='git history to use when falling back (default: 1-year-ago)')
    parser.add_argument('--git-fallback',
                        action=BooleanOptionalAction, default=True,
                        help='use git when no exact MAINTAINERS pattern (default: fallback)')
    parser.add_argument('--nogit-fallback', dest='git_fallback',
                        action='store_false',
                        help="Do not fallback to git (alias)")
    parser.add_argument('--git-min-signatures', default=1,
                        help='number of signatures required (default: 1)')
    parser.add_argument('--git-max-maintainers', default=5,
                        help='maximum number of git derived maintainers to add (default: 5)')
    parser.add_argument('--git-min-percent', default=5,
                        help='minimum percentage of commits to tagged as a maintainer (default: 5)')

    # Roles and Statistics options
    parser.add_argument('--roles', action=BooleanOptionalAction,
                        default=True,
                        help="Show roles (default: roles on)")
    parser.add_argument('--noroles', dest='roles', action='store_false',
                        help="Do not show roles (alias for --no-roles)")
    parser.add_argument('--rolestats', action=BooleanOptionalAction,
                        default=True,
                        help="Show statistics in git roles (default: stats on)")
    parser.add_argument('--norolestats', dest='rolestats', action='store_false',
                        help="Do not show stats in git roles (alias)")

    # Output options
    class SetFileSummary(Action):
        def __call__(self, parser, namespace, values, option_string=None):
            setattr(namespace, self.dest, True)
            # don't fallback to git
            setattr(namespace, "git_fallback", False)

    parser.add_argument('--file-summary', action=SetFileSummary, nargs=0,
                        default=False,
                        help="Summarise file maintenance status")

    return parser.parse_args()


def main():

    args = parse_args()

    try:
        # We need the gitlab map before we read the MAINTAINERS file
        gitlab_map = load_gitlab_handles(args.src)
        maint_sections = read_maintainers(args.src, gitlab_map)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(-1)

    if args.validate:
        writer = csv_writer(sys.stdout)
        writer.writerow(["Section", "Status",
                         "Num Maintainers", "Num Reviewers", "Num Lists",
                         "Num Matches", "Num Keywords", "Num Trees"])
        for ms in maint_sections:
            writer.writerow([ms.section, str(ms.status),
                             len(ms.maintainers), len(ms.reviewers), len(ms.lists),
                             len(ms.files), len(ms.keywords), len(ms.trees)])
        sys.exit(0)

    if args.list_maintainers:
        dump_maintainers_csv(args.src, maint_sections)
        sys.exit(0)

    if args.list_absent_maintainers_since:
        list_absent_maintainers(args.src, maint_sections,
                                args.list_absent_maintainers_since)
        sys.exit(0)

    # Build array of Path objects representing the files
    if args.list_maintained_by or args.list_reviewed_by:
        if args.list_maintained_by:
            pattern = re_compile(args.list_maintained_by, IGNORECASE)
            for m in maint_sections:
                if any(pattern.search(str(p)) for p in m.maintainers):
                    print(m.section)
        if args.list_reviewed_by:
            pattern = re_compile(args.list_reviewed_by, IGNORECASE)
            for m in maint_sections:
                if any(pattern.search(str(p)) for p in m.reviewers):
                    print(m.section)
        sys.exit(0)

    # Build array of Path objects representing the files
    files = []

    if args.file:
        files.extend(args.file)

    # unique set of maintainer sections
    maintained: set[MaintainerSection] = set()

    if args.patch:
        for p in args.patch:
            (msg, pfiles) = process_patch_file(p)
            files.extend(pfiles)
            kmaint = [ms for ms in maint_sections if ms.patch_contains_keyword(msg)]
            for m in kmaint:
                maintained.add(m)

    writer = csv_writer(sys.stdout)
    if args.file_summary and files:
        writer.writerow(["File", "Subsystem", "Status",
                         "Num Maintainers", "Num Reviewers", "Num Lists"])

    for f in files:
        rf = f.relative_to(args.src)
        fmaint = [ms for ms in maint_sections if ms.is_file_covered(rf)]
        for m in fmaint:
            maintained.add(m)

        if args.file_summary:
            valid_ms = [ms for ms in fmaint if ms.status != Status.UNKNOWN]
            if valid_ms:
                for ms in valid_ms:
                    writer.writerow([rf, ms.section, str(ms.status),
                                    len(ms.maintainers),
                                    len(ms.reviewers),len(ms.lists)])
            else:
                writer.writerow([rf, "unmaintained", Status.UNKNOWN,
                                 0, 0, 0])
        else:
            for m in fmaint:
                maintained.add(m)

    recipients = {}
    if not args.file_summary:
        for rm in maintained:
            if args.roles:
                print(f"{rm}")
            else:
                for p in rm.maintainers + rm.reviewers:
                    recipients.setdefault(p.email, p)
                for l in rm.lists:
                    recipients.setdefault(l, l)

    # Git fallback
    if args.git or (args.git_fallback and len(maintained) == 0):
        repo = Repo(args.src)
        git_opts = GitOptions(repo=repo, singers=args.git,
                              since=args.git_since,
                              min_sig=args.git_min_signatures,
                              max_maint=args.git_max_maintainers,
                              min_percent=args.git_min_percent,
                              gitlab_map=gitlab_map)

        for f in files:
            gmaint = extract_from_git(git_opts, f)

            for (person, role, count, percent, total) in gmaint:
                if not args.roles:
                    recipients.setdefault(person.email, person)
                else:
                    if not args.rolestats:
                        role = "commit_signer"
                    print(f"{person} ({person.format_role(role)})")

    if not args.roles and not args.file_summary:
        for r in recipients.values():
            print(f"{r}")


if __name__ == '__main__':
    main()
