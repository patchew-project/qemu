#!/usr/bin/env python3
#
# Pretty-printer for simple trace backend binary trace files
#
# Copyright IBM, Corp. 2010
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#
# For help see docs/devel/tracing.rst

import sys
import struct
import inspect
from tracetool import read_events, Event
from tracetool.backend.simple import is_string

header_event_id = 0xffffffffffffffff # trace/simple.c::HEADER_EVENT_ID
header_magic    = 0xf2b177cb0aa429b4 # trace/simple.c::HEADER_MAGIC
dropped_event_id = 0xfffffffffffffffe # trace/simple.c::DROPPED_EVENT_ID

record_type_mapping = 0 # trace/simple.c::TRACE_RECORD_TYPE_MAPPING
record_type_event = 1 # trace/simple.c::TRACE_RECORD_TYPE_EVENT

log_header_fmt = '=QQQ' # trace/simple.c::TraceLogHeader
rec_header_fmt = '=QQII' # trace/simple.c::TraceRecord
rec_header_fmt_len = struct.calcsize(rec_header_fmt)

class SimpleException(Exception):
    pass

def read_header(fobj, hfmt):
    '''Read a trace record header'''
    hlen = struct.calcsize(hfmt)
    hdr = fobj.read(hlen)
    if len(hdr) != hlen:
        raise SimpleException('Error reading header. Wrong filetype provided?')
    return struct.unpack(hfmt, hdr)

def get_mapping(fobj):
    (event_id, ) = struct.unpack('=Q', fobj.read(8))
    (len, ) = struct.unpack('=L', fobj.read(4))
    name = fobj.read(len).decode()

    return (event_id, name)

def read_record(fobj):
    """Deserialize a trace record from a file into a tuple (event_num, timestamp, pid, args)."""
    event_id, timestamp_ns, record_length, record_pid = read_header(fobj, rec_header_fmt)
    args_payload = fobj.read(record_length - rec_header_fmt_len)
    return (event_id, timestamp_ns, record_pid, args_payload)

def read_trace_header(fobj):
    """Read and verify trace file header"""
    _header_event_id, _header_magic, log_version = read_header(fobj, log_header_fmt)
    if _header_event_id != header_event_id:
        raise ValueError(f'Not a valid trace file, header id {_header_event_id} != {header_event_id}')
    if _header_magic != header_magic:
        raise ValueError(f'Not a valid trace file, header magic {_header_magic} != {header_magic}')

    if log_version not in [0, 2, 3, 4]:
        raise ValueError(f'Unknown version {log_version} of tracelog format!')
    if log_version != 4:
        raise ValueError(f'Log format {log_version} not supported with this QEMU release!')

def read_trace_records(events, fobj, read_header):
    """Deserialize trace records from a file, yielding record tuples (event, event_num, timestamp, pid, arg1, ..., arg6).

    Args:
        event_mapping (str -> Event): events dict, indexed by name
        fobj (file): input file
        read_header (bool): whether headers were read from fobj

    """
    frameinfo = inspect.getframeinfo(inspect.currentframe())
    dropped_event = Event.build("Dropped_Event(uint64_t num_events_dropped)",
                                frameinfo.lineno + 1, frameinfo.filename)

    event_mapping = {e.name: e for e in events}
    event_mapping["dropped"] = dropped_event
    event_id_to_name = {dropped_event_id: "dropped"}

    # If there is no header assume event ID mapping matches events list
    if not read_header:
        for event_id, event in enumerate(events):
            event_id_to_name[event_id] = event.name

    while t := fobj.read(8):
        (rectype, ) = struct.unpack('=Q', t)
        if rectype == record_type_mapping:
            event_id, event_name = get_mapping(fobj)
            event_id_to_name[event_id] = event_name
        else:
            event_id, timestamp_ns, pid, args_payload = read_record(fobj)
            event_name = event_id_to_name[event_id]

            try:
                event = event_mapping[event_name]
            except KeyError as e:
                raise SimpleException(
                    f'{e} event is logged but is not declared in the trace events'
                    'file, try using trace-events-all instead.'
                )

            offset = 0
            args = []
            for type, _ in event.args:
                if is_string(type):
                    (len,) = struct.unpack_from('=L', args_payload, offset=offset)
                    offset += 4
                    s = args_payload[offset:offset+len]
                    offset += len
                    args.append(s)
                else:
                    (value,) = struct.unpack_from('=Q', args_payload, offset=offset)
                    offset += 8
                    args.append(value)

            yield (event_mapping[event_name], event_name, timestamp_ns, pid) + tuple(args)

class Analyzer:
    """A trace file analyzer which processes trace records.

    An analyzer can be passed to run() or process().  The __enter__() method is
    invoked when opening the analyzer using the `with` statement, then each trace
    record is processed, and finally the __exit__() method is invoked.

    If a method matching a trace event name exists, it is invoked to process
    that trace record.  Otherwise the catchall() method is invoked.

    The methods are called with a set of keyword-arguments. These can be ignored
    using `**kwargs` or defined like any keyword-argument.

    The following keyword-arguments are available:
        event: Event object of current trace
        event_id: The id of the event in the current trace file
        timestamp_ns: The timestamp in nanoseconds of the trace
        pid: The process id recorded for the given trace

    Example:
    The following method handles the runstate_set(int new_state) trace event::

      def runstate_set(self, new_state, **kwargs):
          ...

    The method can also explicitly take a timestamp keyword-argument with the
    trace event arguments::

      def runstate_set(self, new_state, *, timestamp, **kwargs):
          ...

    Timestamps have the uint64_t type and are in nanoseconds.

    The pid can be included in addition to the timestamp and is useful when
    dealing with traces from multiple processes::

      def runstate_set(self, new_state, *, timestamp, pid, **kwargs):
          ...
    """

    def __enter__(self):
        """Called at the start of the trace."""
        return self

    def catchall(self, *rec_args, event, timestamp_ns, pid, event_id):
        """Called if no specific method for processing a trace event has been found."""
        pass

    def __exit__(self, _type, value, traceback):
        """Called at the end of the trace."""
        pass

    def __call__(self):
        """Fix for legacy use without context manager.
        We call the provided object in `process` regardless of it being the object-type or instance.
        With this function, it will work in both cases."""
        return self

def process(events_fobj, log_fobj, analyzer_class, read_header=True):
    """Invoke an analyzer on each event in a log.

    Args:
        events_fobj (file): file-object to read event data from
        log_fobj (file): file-object to read log data from
        analyzer_class (Analyzer): the Analyzer to interpret the event data
        read_header (bool, optional): Whether to read header data from the log data. Defaults to True.
    """
    if read_header:
        read_trace_header(log_fobj)

    events = read_events(events_fobj, events_fobj.name)

    with analyzer_class() as analyzer:
        for event, event_id, timestamp_ns, record_pid, *rec_args in read_trace_records(events, log_fobj, read_header):
            fn = getattr(analyzer, event.name, analyzer.catchall)
            fn(*rec_args, event=event, event_id=event_id, timestamp_ns=timestamp_ns, pid=record_pid)


def run(analyzer):
    """Execute an analyzer on a trace file given on the command-line.

    This function is useful as a driver for simple analysis scripts.  More
    advanced scripts will want to call process() instead."""

    try:
        # NOTE: See built-in `argparse` module for a more robust cli interface
        *no_header, trace_event_path, trace_file_path = sys.argv[1:]
        assert no_header == [] or no_header == ['--no-header'], 'Invalid no-header argument'
    except (AssertionError, ValueError):
        raise SimpleException(f'usage: {sys.argv[0]} [--no-header] <trace-events> <trace-file>\n')

    with open(trace_event_path, 'r') as events_fobj, open(trace_file_path, 'rb') as log_fobj:
        process(events_fobj, log_fobj, analyzer, read_header=not no_header)

if __name__ == '__main__':
    class Formatter(Analyzer):
        def __init__(self):
            self.last_timestamp_ns = None

        def catchall(self, *rec_args, event, timestamp_ns, pid, event_id):
            if self.last_timestamp_ns is None:
                self.last_timestamp_ns = timestamp_ns
            delta_ns = timestamp_ns - self.last_timestamp_ns
            self.last_timestamp_ns = timestamp_ns

            fields = [
                f'{name}={r}' if is_string(type) else f'{name}=0x{r:x}'
                for r, (type, name) in zip(rec_args, event.args)
            ]
            print(f'{event.name} {delta_ns / 1000:0.3f} {pid=} ' + ' '.join(fields))

    try:
        run(Formatter())
    except SimpleException as e:
        sys.stderr.write(e + "\n")
        sys.exit(1)
