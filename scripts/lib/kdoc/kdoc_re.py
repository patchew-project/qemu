#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2025: Mauro Carvalho Chehab <mchehab@kernel.org>.

"""
Regular expression ancillary classes.

Those help caching regular expressions and do matching for kernel-doc.
"""

import re

# Local cache for regular expressions
re_cache = {}


class KernRe:
    """
    Helper class to simplify regex declaration and usage.

    It calls re.compile for a given pattern. It also allows adding
    regular expressions and define sub at class init time.

    Regular expressions can be cached via an argument, helping to speedup
    searches.
    """

    def _add_regex(self, string, flags):
        """
        Adds a new regex or reuses it from the cache.
        """
        self.regex = re_cache.get(string, None)
        if not self.regex:
            self.regex = re.compile(string, flags=flags)
            if self.cache:
                re_cache[string] = self.regex

    def __init__(self, string, cache=True, flags=0):
        """
        Compile a regular expression and initialize internal vars.
        """

        self.cache = cache
        self.last_match = None

        self._add_regex(string, flags)

    def __str__(self):
        """
        Return the regular expression pattern.
        """
        return self.regex.pattern

    def __repr__(self):
        """
        Returns a displayable version of the class init.
        """

        flag_map = {
            re.IGNORECASE: "re.I",
            re.MULTILINE: "re.M",
            re.DOTALL: "re.S",
            re.VERBOSE: "re.X",
        }

        flags = []
        for flag, name in flag_map.items():
            if self.regex.flags & flag:
                flags.append(name)

        flags_name = " | ".join(flags)

        if flags_name:
            return f'KernRe("{self.regex.pattern}", {flags_name})'
        else:
            return f'KernRe("{self.regex.pattern}")'

    def __add__(self, other):
        """
        Allows adding two regular expressions into one.
        """

        return KernRe(str(self) + str(other), cache=self.cache or other.cache,
                  flags=self.regex.flags | other.regex.flags)

    def match(self, string):
        """
        Handles a re.match storing its results.
        """

        self.last_match = self.regex.match(string)
        return self.last_match

    def search(self, string):
        """
        Handles a re.search storing its results.
        """

        self.last_match = self.regex.search(string)
        return self.last_match

    def finditer(self,  string):
        """
        Alias to re.finditer.
        """

        return self.regex.finditer(string)

    def findall(self, string):
        """
        Alias to re.findall.
        """

        return self.regex.findall(string)

    def split(self, string):
        """
        Alias to re.split.
        """

        return self.regex.split(string)

    def sub(self, sub, string, count=0):
        """
        Alias to re.sub.
        """

        return self.regex.sub(sub, string, count=count)

    def group(self, num):
        """
        Returns the group results of the last match.
        """

        return self.last_match.group(num)

    def groups(self):
        """
        Returns the group results of the last match
        """

        return self.last_match.groups()

#: Nested delimited pairs (brackets and parenthesis)
DELIMITER_PAIRS = {
    '{': '}',
    '(': ')',
    '[': ']',
}

#: compiled delimiters
RE_DELIM = KernRe(r'[\{\}\[\]\(\)]')


class NestedMatch:
    """
    Finding nested delimiters is hard with regular expressions. It is
    even harder on Python with its normal re module, as there are several
    advanced regular expressions that are missing.

    This is the case of this pattern::

            '\\bSTRUCT_GROUP(\\(((?:(?>[^)(]+)|(?1))*)\\))[^;]*;'

    which is used to properly match open/close parentheses of the
    string search STRUCT_GROUP(),

    Add a class that counts pairs of delimiters, using it to match and
    replace nested expressions.

    The original approach was suggested by:

        https://stackoverflow.com/questions/5454322/python-how-to-match-nested-parentheses-with-regex

    Although I re-implemented it to make it more generic and match 3 types
    of delimiters. The logic checks if delimiters are paired. If not, it
    will ignore the search string.
    """

    def __init__(self, regex):
        self.regex = KernRe(regex)

    def _search(self, line):
        """
        Finds paired blocks for a regex that ends with a delimiter.

        The suggestion of using finditer to match pairs came from:
        https://stackoverflow.com/questions/5454322/python-how-to-match-nested-parentheses-with-regex
        but I ended using a different implementation to align all three types
        of delimiters and seek for an initial regular expression.

        The algorithm seeks for open/close paired delimiters and places them
        into a stack, yielding a start/stop position of each match when the
        stack is zeroed.

        The algorithm should work fine for properly paired lines, but will
        silently ignore end delimiters that precede a start delimiter.
        This should be OK for kernel-doc parser, as unaligned delimiters
        would cause compilation errors. So, we don't need to raise exceptions
        to cover such issues.
        """

        stack = []
        start = 0
        offset = 0
        pos = 0

        for match_re in self.regex.finditer(line):
            start = match_re.start()
            offset = match_re.end()
            string_char = None
            escape = False

            d = line[offset - 1]
            if d not in DELIMITER_PAIRS:
                continue

            end = DELIMITER_PAIRS[d]
            stack.append(end)

            for match in RE_DELIM.finditer(line[offset:]):
                pos = match.start() + offset

                d = line[pos]

                if escape:
                    escape = False
                    continue

                if string_char:
                    if d == '\\':
                        escape = True
                    elif d == string_char:
                        string_char = None

                    continue

                if d in ('"', "'"):
                    string_char = d
                    continue

                if d in DELIMITER_PAIRS:
                    end = DELIMITER_PAIRS[d]

                    stack.append(end)
                    continue

                # Does the end delimiter match what is expected?
                if stack and d == stack[-1]:
                    stack.pop()

                    if not stack:
                        yield start, offset, pos + 1
                        break

        # When /* private */ is used, it may end the end delimiterq
        if stack:
            stack.pop()
            yield start, offset, len(line) + 1

    def search(self, line):
        """
        This is similar to re.search:

        It matches a regex that it is followed by a delimiter,
        returning occurrences only if all delimiters are paired.
        """

        for t in self._search(line):

            yield line[t[0]:t[2]]

    @staticmethod
    def _split_args(all_args, delim=","):
        """
        Helper method to split comma-separated function arguments
        or struct elements, if delim is set to ";".

        It returns a list of arguments that can be used later on by
        the sub() method.
        """
        args = [all_args]
        stack = []
        arg_start = 0
        string_char = None
        escape = False

        for idx, d in enumerate(all_args):
            if escape:
                escape = False
                continue

            if string_char:
                if d == '\\':
                    escape = True
                elif d == string_char:
                    string_char = None

                continue

            if d in ('"', "'"):
                string_char = d
                continue

            if d in DELIMITER_PAIRS:
                end = DELIMITER_PAIRS[d]

                stack.append(end)
                continue

            if stack and d == stack[-1]:
                stack.pop()
                continue

            if d == delim and not stack:
                args.append(all_args[arg_start:idx].strip())
                arg_start = idx + 1

        # Add the last argument (if any)
        last = all_args[arg_start:].strip()
        if last:
            args.append(last)

        return args

    def sub(self, sub, line, count=0):
        """
        This is similar to re.sub:

        It matches a regex that it is followed by a delimiter,
        replacing occurrences only if all delimiters are paired.

        if r'\0' is used, it works on a similar way of using re.group(0):
        it places the entire args of the matched paired data, with the
        delimiter stripped.

        If count is different than zero, it will replace at most count
        items.
        """
        out = ""

        cur_pos = 0
        n = 0

        for start, end, pos in self._search(line):
            out += line[cur_pos:start]

            # Value, ignoring start/end delimiters
            value = line[end:pos - 1]

            # replace arguments
            new_sub = sub
            if "\\" in sub:
                args = self._split_args(value)

                new_sub = re.sub(r'\\(\d+)',
                                 lambda m: args[int(m.group(1))], new_sub)

            out += new_sub

            cur_pos = pos
            n += 1

            if count and count >= n:
                break

        # Append the remaining string
        l = len(line)
        out += line[cur_pos:l]

        return out

    def __repr__(self):
        """
        Returns a displayable version of the class init.
        """

        return f'NestedMatch("{self.regex.regex.pattern}")'


class CFunction(NestedMatch):
    r"""
    Variant of NestedMatch.

    It overrides the init method to ensure that the regular expression will
    start with a ``\b`` and end with a C function delimiter (open parenthesis).
    """
    def __init__(self, regex):
        self.regex = KernRe(r"\b" + regex + r"\s*\(")
