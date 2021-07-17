import asyncio
from contextlib import contextmanager
import os
import socket
from tempfile import TemporaryDirectory

import avocado

from qemu.aqmp import ConnectError, Runstate
from qemu.aqmp.protocol import StateError
from qemu.aqmp.util import asyncio_run, create_task

# An Avocado bug prevents us from defining this testing class in-line here:
from null_proto import NullProtocol


def run_as_task(coro, allow_cancellation=False):
    # This helper runs a given coroutine as a task, wrapping it in a
    # try...except that allows it to be cancelled gracefully.
    async def _runner():
        try:
            await coro
        except asyncio.CancelledError:
            if allow_cancellation:
                return
            raise
    return create_task(_runner())


@contextmanager
def jammed_socket():
    # This method opens up a random TCP port on localhost, then jams it.
    socks = []

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(('127.0.0.1', 0))
        sock.listen(1)
        address = sock.getsockname()

        socks.append(sock)

        # I don't *fully* understand why, but it takes *two* un-accepted
        # connections to start jamming the socket.
        for _ in range(2):
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(address)
            socks.append(sock)

        yield address

    finally:
        for sock in socks:
            sock.close()


class Smoke(avocado.Test):

    def setUp(self):
        self.proto = NullProtocol()

    def test__repr__(self):
        self.assertEqual(
            repr(self.proto),
            "<NullProtocol runstate=IDLE>"
        )

    def testRunstate(self):
        self.assertEqual(
            self.proto.runstate,
            Runstate.IDLE
        )

    def testDefaultName(self):
        self.assertEqual(
            self.proto.name,
            None
        )

    def testLogger(self):
        self.assertEqual(
            self.proto.logger.name,
            'qemu.aqmp.protocol'
        )

    def testName(self):
        self.proto = NullProtocol('Steve')

        self.assertEqual(
            self.proto.name,
            'Steve'
        )

        self.assertEqual(
            self.proto.logger.name,
            'qemu.aqmp.protocol.Steve'
        )

        self.assertEqual(
            repr(self.proto),
            "<NullProtocol name='Steve' runstate=IDLE>"
        )


class TestBase(avocado.Test):

    def setUp(self):
        self.proto = NullProtocol(type(self).__name__)
        self.assertEqual(self.proto.runstate, Runstate.IDLE)
        self.runstate_watcher = None

    def tearDown(self):
        self.assertEqual(self.proto.runstate, Runstate.IDLE)

    async def _asyncSetUp(self):
        pass

    async def _asyncTearDown(self):
        if self.runstate_watcher:
            await self.runstate_watcher

    def _asyncRunner(self, test_coroutine):
        async def coroutine():
            await self._asyncSetUp()
            await test_coroutine
            await self._asyncTearDown()

        asyncio_run(coroutine(), debug=True)

    # Definitions

    # The states we expect a "bad" connect/accept attempt to transition through
    BAD_CONNECTION_STATES = (
        Runstate.CONNECTING,
        Runstate.DISCONNECTING,
        Runstate.IDLE,
    )

    # The states we expect a "good" session to transition through
    GOOD_CONNECTION_STATES = (
        Runstate.CONNECTING,
        Runstate.RUNNING,
        Runstate.DISCONNECTING,
        Runstate.IDLE,
    )

    # Helpers

    async def _watch_runstates(self, *states):
        # This launches a task alongside most tests below to confirm that the
        # sequence of runstate changes is exactly as anticipated.

        async def _watcher():
            for state in states:
                new_state = await self.proto.runstate_changed()
                self.assertEqual(
                    new_state,
                    state,
                    msg=f"Expected state '{state.name}'",
                )

        self.runstate_watcher = create_task(_watcher())
        # Kick the loop and force the task to block on the event.
        await asyncio.sleep(0)


class State(TestBase):

    async def testSuperfluousDisconnect_(self):
        await self._watch_runstates(
            Runstate.DISCONNECTING,
            Runstate.IDLE,
        )
        await self.proto.disconnect()

    def testSuperfluousDisconnect(self):
        self._asyncRunner(self.testSuperfluousDisconnect_())


class Connect(TestBase):

    async def _bad_connection(self, family: str):
        assert family in ('INET', 'UNIX')

        if family == 'INET':
            await self.proto.connect(('127.0.0.1', 0))
        elif family == 'UNIX':
            await self.proto.connect('/dev/null')

    async def _hanging_connection(self):
        with jammed_socket() as addr:
            await self.proto.connect(addr)

    async def _bad_connection_test(self, family: str):
        await self._watch_runstates(*self.BAD_CONNECTION_STATES)

        with self.assertRaises(ConnectError) as context:
            await self._bad_connection(family)

        self.assertIsInstance(context.exception.exc, OSError)
        self.assertEqual(
            context.exception.error_message,
            "Failed to establish connection"
        )

    def testBadINET(self):
        self._asyncRunner(self._bad_connection_test('INET'))
        # self.assertIsInstance(err.exc, ConnectionRefusedError)

    def testBadUNIX(self):
        self._asyncRunner(self._bad_connection_test('UNIX'))
        # self.assertIsInstance(err.exc, ConnectionRefusedError)

    async def testCancellation_(self):
        # Note that accept() cannot be cancelled outright, as it isn't a task.
        # However, we can wrap it in a task and cancel *that*.
        await self._watch_runstates(*self.BAD_CONNECTION_STATES)
        task = run_as_task(self._hanging_connection(), allow_cancellation=True)

        state = await self.proto.runstate_changed()
        self.assertEqual(state, Runstate.CONNECTING)

        # This is insider baseball, but the connection attempt has
        # yielded *just* before the actual connection attempt, so kick
        # the loop to make sure it's truly wedged.
        await asyncio.sleep(0)

        task.cancel()
        await task

    def testCancellation(self):
        self._asyncRunner(self.testCancellation_())

    async def testTimeout_(self):
        await self._watch_runstates(*self.BAD_CONNECTION_STATES)
        task = run_as_task(self._hanging_connection())

        # More insider baseball: to improve the speed of this test while
        # guaranteeing that the connection even gets a chance to start,
        # verify that the connection hangs *first*, then await the
        # result of the task with a nearly-zero timeout.

        state = await self.proto.runstate_changed()
        self.assertEqual(state, Runstate.CONNECTING)
        await asyncio.sleep(0)

        with self.assertRaises(asyncio.TimeoutError):
            await asyncio.wait_for(task, timeout=0)

    def testTimeout(self):
        self._asyncRunner(self.testTimeout_())

    async def testRequire_(self):
        await self._watch_runstates(*self.BAD_CONNECTION_STATES)
        task = run_as_task(self._hanging_connection(), allow_cancellation=True)

        state = await self.proto.runstate_changed()
        self.assertEqual(state, Runstate.CONNECTING)

        with self.assertRaises(StateError) as context:
            await self._bad_connection('UNIX')

        self.assertEqual(
            context.exception.error_message,
            "NullProtocol is currently connecting."
        )
        self.assertEqual(context.exception.state, Runstate.CONNECTING)
        self.assertEqual(context.exception.required, Runstate.IDLE)

        task.cancel()
        await task

    def testRequire(self):
        self._asyncRunner(self.testRequire_())

    async def testImplicitRunstateInit_(self):
        # This tests what happens if we do not wait on the
        # runstate until AFTER we connect, i.e., connect()/accept()
        # themselves initialize the runstate event. All of the above
        # tests force the initialization by waiting on the runstate
        # *first*.
        task = run_as_task(self._hanging_connection(), allow_cancellation=True)

        # Kick the loop to coerce the state change
        await asyncio.sleep(0)
        assert self.proto.runstate == Runstate.CONNECTING

        # We already missed the transition to CONNECTING
        await self._watch_runstates(Runstate.DISCONNECTING, Runstate.IDLE)

        task.cancel()
        await task

    def testImplicitRunstateInit(self):
        self._asyncRunner(self.testImplicitRunstateInit_())


class Accept(Connect):

    async def _bad_connection(self, family: str):
        assert family in ('INET', 'UNIX')

        if family == 'INET':
            await self.proto.accept(('example.com', 1))
        elif family == 'UNIX':
            await self.proto.accept('/dev/null')

    async def _hanging_connection(self):
        with TemporaryDirectory(suffix='.aqmp') as tmpdir:
            sock = os.path.join(tmpdir, type(self.proto).__name__ + ".sock")
            await self.proto.accept(sock)


class FakeSession(TestBase):

    def setUp(self):
        super().setUp()
        self.proto.fake_session = True

    async def _asyncSetUp(self):
        await super()._asyncSetUp()
        await self._watch_runstates(*self.GOOD_CONNECTION_STATES)

    async def _asyncTearDown(self):
        await self.proto.disconnect()
        await super()._asyncTearDown()

    ####

    async def testFakeConnect_(self):
        await self.proto.connect('/not/a/real/path')
        self.assertEqual(self.proto.runstate, Runstate.RUNNING)

    def testFakeConnect(self):
        """Test the full state lifecycle (via connect) with a no-op session."""
        self._asyncRunner(self.testFakeConnect_())

    async def testFakeAccept_(self):
        await self.proto.accept('/not/a/real/path')
        self.assertEqual(self.proto.runstate, Runstate.RUNNING)

    def testFakeAccept(self):
        """Test the full state lifecycle (via accept) with a no-op session."""
        self._asyncRunner(self.testFakeAccept_())

    async def testFakeRecv_(self):
        await self.proto.accept('/not/a/real/path')

        logname = self.proto.logger.name
        with self.assertLogs(logname, level='DEBUG') as context:
            self.proto.trigger_input.set()
            self.proto.trigger_input.clear()
            await asyncio.sleep(0)  # Kick reader.

        self.assertEqual(
            context.output,
            [f"DEBUG:{logname}:<-- None"],
        )

    def testFakeRecv(self):
        """Test receiving a fake/null message."""
        self._asyncRunner(self.testFakeRecv_())

    async def testFakeSend_(self):
        await self.proto.accept('/not/a/real/path')

        logname = self.proto.logger.name
        with self.assertLogs(logname, level='DEBUG') as context:
            # Cheat: Send a Null message to nobody.
            await self.proto.send_msg()
            # Kick writer; awaiting on a queue.put isn't sufficient to yield.
            await asyncio.sleep(0)

        self.assertEqual(
            context.output,
            [f"DEBUG:{logname}:--> None"],
        )

    def testFakeSend(self):
        """Test sending a fake/null message."""
        self._asyncRunner(self.testFakeSend_())

    async def _prod_session_api(
            self,
            current_state: Runstate,
            error_message: str,
            accept: bool = True
    ):
        with self.assertRaises(StateError) as context:
            if accept:
                await self.proto.accept('/not/a/real/path')
            else:
                await self.proto.connect('/not/a/real/path')

        self.assertEqual(context.exception.error_message, error_message)
        self.assertEqual(context.exception.state, current_state)
        self.assertEqual(context.exception.required, Runstate.IDLE)

    async def testAcceptRequireRunning_(self):
        await self.proto.accept('/not/a/real/path')

        await self._prod_session_api(
            Runstate.RUNNING,
            "NullProtocol is already connected and running.",
            accept=True,
        )

    def testAcceptRequireRunning(self):
        """Test that accept() cannot be called when Runstate=RUNNING"""
        self._asyncRunner(self.testAcceptRequireRunning_())

    async def testConnectRequireRunning_(self):
        await self.proto.accept('/not/a/real/path')

        await self._prod_session_api(
            Runstate.RUNNING,
            "NullProtocol is already connected and running.",
            accept=False,
        )

    def testConnectRequireRunning(self):
        """Test that connect() cannot be called when Runstate=RUNNING"""
        self._asyncRunner(self.testConnectRequireRunning_())

    async def testAcceptRequireDisconnecting_(self):
        await self.proto.accept('/not/a/real/path')

        # Cheat: force a disconnect.
        await self.proto.simulate_disconnect()

        await self._prod_session_api(
            Runstate.DISCONNECTING,
            ("NullProtocol is disconnecting."
             " Call disconnect() to return to IDLE state."),
            accept=True,
        )

    def testAcceptRequireDisconnecting(self):
        """Test that accept() cannot be called when Runstate=DISCONNECTING"""
        self._asyncRunner(self.testAcceptRequireDisconnecting_())

    async def testConnectRequireDisconnecting_(self):
        await self.proto.accept('/not/a/real/path')

        # Cheat: force a disconnect.
        await self.proto.simulate_disconnect()

        await self._prod_session_api(
            Runstate.DISCONNECTING,
            ("NullProtocol is disconnecting."
             " Call disconnect() to return to IDLE state."),
            accept=False,
        )

    def testConnectRequireDisconnecting(self):
        """Test that connect() cannot be called when Runstate=DISCONNECTING"""
        self._asyncRunner(self.testConnectRequireDisconnecting_())
