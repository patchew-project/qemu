import asyncio

from qemu.aqmp.protocol import AsyncProtocol


class NullProtocol(AsyncProtocol[None]):
    """
    NullProtocol is a test mockup of an AsyncProtocol implementation.

    It adds a fake_session instance variable that enables a code path
    that bypasses the actual connection logic, but still allows the
    reader/writers to start.

    Because the message type is defined as None, an asyncio.Event named
    'trigger_input' is created that prohibits the reader from
    incessantly being able to yield None; this input can be poked to
    simulate an incoming message.

    For testing symmetry with do_recv, an interface is added to "send" a
    Null message.

    For testing purposes, a "simulate_disconnection" method is also
    added which allows us to trigger a bottom half disconnect without
    injecting any real errors into the reader/writer loops; in essence
    it performs exactly half of what disconnect() normally does.
    """
    def __init__(self, name=None):
        self.fake_session = False
        self.trigger_input: asyncio.Event
        super().__init__(name)

    async def _establish_session(self):
        self.trigger_input = asyncio.Event()
        await super()._establish_session()

    async def _do_accept(self, address, ssl=None):
        if not self.fake_session:
            await super()._do_accept(address, ssl)

    async def _do_connect(self, address, ssl=None):
        if not self.fake_session:
            await super()._do_connect(address, ssl)

    async def _do_recv(self) -> None:
        await self.trigger_input.wait()
        self.trigger_input.clear()

    def _do_send(self, msg: None) -> None:
        pass

    async def send_msg(self) -> None:
        await self._outgoing.put(None)

    async def simulate_disconnect(self) -> None:
        # Simulates a bottom half disconnect, e.g. schedules a
        # disconnection but does not wait for it to complete. This is
        # used to put the loop into the DISCONNECTING state without
        # fully quiescing it back to IDLE; this is normally something
        # you cannot coax AsyncProtocol to do on purpose, but it will be
        # similar to what happens with an unhandled Exception in the
        # reader/writer.
        #
        # Under normal circumstances, the library design requires you to
        # await on disconnect(), which awaits the disconnect task and
        # returns bottom half errors as a pre-condition to allowing the
        # loop to return back to IDLE.
        self._schedule_disconnect()
