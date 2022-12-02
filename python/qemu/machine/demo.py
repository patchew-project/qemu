"""
This module is a quick demonstration to show how to asynchronously
watch terminal output using Python asyncio, and how it might relate to
improving QEMUMachine.

This demo does NOT include console output nor a QMP monitor, but the
techniques being applied here might be applied to guest console
interactions.
"""

import asyncio
import io
import locale
import logging
import os
from pathlib import Path
import resource
import sys
from typing import (
    Any,
    BinaryIO,
    List,
    Optional,
    Union,
)


class StreamWatcher:
    """
    StreamWatcher is a bit of a quick hack that consumes data (as
    bytes) from an asyncio.StreamReader and relays it to several other
    sources concurrently.

    Conceptually, it's kind of like "tee": data from one pipe is
    forwarded to several.

    A single instance of this class watches either stdout or
    stderr. (A user could combine these streams and then a single
    instance of this class could watch both.)

    The data stream being watched is forwarded to three destinations:

    (1) BytesIO (In-memory buffer)
        --------------------------

        The data is buffered directly into a BytesIO object. This isn't
        used to do anything further, but a caller could get the entire
        stream (so far) at any time. This mimics the "iolog" property of
        QEMUMachine, which we have used in the past to show the console
        output on various error conditions or to assert that certain
        patterns have occurred in iotests.

        This can grow without bound, which might be a bad idea. It seems
        convenient to have on by default, but for more serious uses, you
        may want to actually disable this. Maybe it'd be useful to be
        able to configure which sources you want active by default.

    (2) External logfile
        ----------------

        Data is written byte-for-byte into an external logfile. In this
        demo, StreamWatcher does not own the file object so that two
        StreamWatchers can share the same logfile -- so that a
        stderr-watcher and a stdout-watcher can log to the same file.

        This destination adds the "logfile" and "flush" parameters to
        the initializer; flush=True can be used for the stderr-watcher
        if desired to flush the file to disk without waiting for a
        newline.

        This might be a bit extraneous since we already have an
        in-memory log, but I added it here purely because if all else
        fails -- if we don't ever print out the in-memory buffer and we
        don't enable logging -- we can likely rely on a good
        old-fashioned solid bohr-model file.

    (3) Python Logging Interface
        ------------------------

        Data is manually re-buffered and when a newline is encountered,
        the buffer is flushed into the Python logging subsystem. This
        may mean that if terminal output is not terminated with a
        newline, we may hang onto it in the buffer. When EOF is
        encountered, any remaining information in the buffer is flushed
        with a newline marker inserted to indicate that a newline was
        not actually seen. (This is how FiSH seems to handle it, and I
        like it.)

        This destination adds two more parameters: logger and level. The
        logger is the Logger instance to log to, while the level
        determines which logging level to use for messages in this
        stream. In this demo, I use "INFO" for stdout and "WARNING" for
        stderr. If the Python logging subsystem is not configured, the
        default behavior is to hide "INFO" messages but to show
        "WARNING" messages. This might be the most useful behavior for
        helping to surface potential errors, but it's possible it will
        be a pain for certain kinds of iotesting.
    """
    # pylint: disable=too-few-public-methods
    def __init__(
            self,
            pipe: asyncio.StreamReader,
            logger: logging.Logger,
            level: int,
            logfile: BinaryIO,
            flush: bool = False):
        self.pipe = pipe
        self.logfile = logfile
        self.logger = logger
        self.level = level
        self.flush = flush

        self.data = io.BytesIO()
        self.buffer = bytearray()

        # We need an encoding for whatever we're watching.
        # For console output, assume it's whatever our locale says.
        # If this guess is wrong, go ahead and change it, pal.
        _, encoding = locale.getlocale()
        self.encoding = encoding or 'UTF-8'

    async def run(self) -> None:
        """
        Run forever, waiting for new data.

        When the stream hits EOF, return.
        """
        self.logger.debug("StreamWatcher starting")
        pagesize = resource.getpagesize()
        while True:
            data = await self.pipe.read(pagesize)
            await self._handle_data(data)
            if not data:
                break
        self.logger.debug("StreamWatcher exiting")

    async def _handle_data(self, data: bytes) -> None:
        # Destination A: Internal line-based buffer
        await self._buffer_data(data)

        # Destination B: Internal byte-based log
        self.data.write(data)

        # Destination C: External logfile
        self.logfile.write(data)
        if self.flush:
            self.logfile.flush()

    async def _buffer_data(self, data: bytes) -> None:
        self.buffer.extend(data)
        if not self.buffer:
            return

        lines = self.buffer.split(b'\n')
        if lines[-1]:  # trailing line was not (yet) terminated
            self.buffer = lines[-1]
        else:
            self.buffer.clear()

        for line in lines[:-1]:
            await self._handle_line(
                line.decode(self.encoding, errors='replace'))

        if data == b'' and self.buffer:
            # EOL; flush the remainder of the buffer.
            await self._handle_line(
                self.buffer.decode(self.encoding, errors='replace') + '⏎')

    async def _handle_line(self, line: str) -> None:
        self.logger.log(self.level, line)


class ExecManager:
    """
    Simple demo for executing a child process while gathering its output.
    """
    logger = logging.getLogger(__name__)

    def __init__(self) -> None:
        self.process: Optional[asyncio.subprocess.Process] = None
        self.log: Optional[BinaryIO] = None
        self.stdout: Optional[StreamWatcher] = None
        self.stderr: Optional[StreamWatcher] = None
        self._tasks: List[asyncio.Task[Any]] = []

    async def launch(self, binary: Union[str, Path], *args: str) -> None:
        """Launch the executable, but don't wait for it."""
        self.logger.debug("launching '%s'", binary)
        self.logger.debug("%s", ' '.join((str(binary),) + args))
        self.process = await asyncio.create_subprocess_exec(
            str(binary),
            *args,
            stdin=asyncio.subprocess.DEVNULL,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        # Type hints for mypy
        assert self.process.stdout is not None
        assert self.process.stderr is not None

        self.log = open("qemu.log", "wb")
        self.stdout = StreamWatcher(
            self.process.stdout,
            self.logger.getChild('stdout'),
            logging.INFO,
            self.log)
        self.stderr = StreamWatcher(
            self.process.stderr,
            self.logger.getChild('stderr'),
            logging.WARNING,
            self.log,
            flush=True)
        self._tasks.append(asyncio.create_task(self.stdout.run()))
        self._tasks.append(asyncio.create_task(self.stderr.run()))

    async def wait(self) -> None:
        """Wait for the process and all watchers to finish."""
        if self.process is None:
            raise Exception("Nothing's running, pal!")
        self.logger.debug("bundling reader tasks and process waiter ...")
        task = asyncio.gather(
            *self._tasks,
            self.process.wait(),
            return_exceptions=True,
        )
        # return_exceptions=True means that if any coroutine raises an
        # exception, all other coroutines will be cancelled and waited on.
        # Without this, the other coroutines continue to run after first
        # exception.
        self.logger.debug("waiting on bundled task ...")
        await task
        self.logger.debug("bundled task done.")
        if self.log is not None:
            self.logger.debug("closing logfile")
            self.log.close()
            if self.process.returncode == 0:
                self.logger.debug("No errors detected; deleting qemu.log")
                os.unlink("qemu.log")
            else:
                self.logger.debug(
                    "Process returned non-zero returncode, keeping qemu.log")
            self.log = None


async def main(binary: str, *args: str) -> int:
    """Run a subprocess, print out some stuff, have a good time."""
    logging.basicConfig(level=logging.DEBUG)
    proc = ExecManager()
    await proc.launch(binary, *args)
    assert proc.stdout is not None
    assert proc.stderr is not None
    logging.debug("process launched; waiting on termination")
    await proc.wait()
    logging.debug("process terminated.")

    stdout = proc.stdout.data.getvalue()
    if stdout:
        print("========== stdout ==========")
        print(stdout.decode(proc.stdout.encoding), end='')
        print("=" * 80)

    stderr = proc.stderr.data.getvalue()
    if stderr:
        print("========== stderr ==========")
        print(stderr.decode(proc.stderr.encoding), end='')
        print("=" * 80)

    assert proc.process is not None
    assert proc.process.returncode is not None
    print(f"process returncode was {proc.process.returncode}")
    print("OK, seeya!")
    return proc.process.returncode


if __name__ == '__main__':
    sys.exit(asyncio.run(main(*sys.argv[1:])))
