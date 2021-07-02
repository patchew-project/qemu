# Copyright (c) 2021
#
# Authors:
#  Niteesh Babu G S <niteesh.gs@gmail.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import argparse
import asyncio
import logging
import signal

from pygments import lexers
from pygments import token as Token
import urwid
import urwid_readline

from .protocol import ConnectError
from .qmp_protocol import QMP, ExecInterruptedError, ExecuteError
from .util import create_task, pretty_traceback


UPDATE_MSG = 'UPDATE_MSG'

palette = [
    (Token.Punctuation, '', '', '', 'h15,bold', 'g7'),
    (Token.Text, '', '', '', '', 'g7'),
    (Token.Name.Tag, '', '', '', 'bold,#f88', 'g7'),
    (Token.Literal.Number.Integer, '', '', '', '#fa0', 'g7'),
    (Token.Literal.String.Double, '', '', '', '#6f6', 'g7'),
    (Token.Keyword.Constant, '', '', '', '#6af', 'g7'),
    ('background', '', 'black', '', '', 'g7'),
]


class StatusBar(urwid.Text):
    """
    A simple Text widget that currently only shows connection status.
    """
    def __init__(self, text=''):
        super().__init__(text, align='right')


class Editor(urwid_readline.ReadlineEdit):
    """
    Support urwid_readline features along with
    history support which lacks in urwid_readline
    """
    def __init__(self, master):
        super().__init__(caption='> ', multiline=True)
        self.master = master
        self.history = []
        self.last_index = -1
        self.show_history = False

    def keypress(self, size, key):
        # TODO: Add some logic for down key and clean up logic if possible.
        # Returning None means the key has been handled by this widget
        # which otherwise is propogated to the parent widget to be
        # handled
        msg = self.get_edit_text()
        if key == 'up' and not msg:
            # Show the history when 'up arrow' is pressed with no input text.
            # NOTE: The show_history logic is necessary because in 'multiline'
            # mode (which we use) 'up arrow' is used to move between lines.
            self.show_history = True
            last_msg = self.history[self.last_index] if self.history else ''
            self.set_edit_text(last_msg)
            self.edit_pos = len(last_msg)
            self.last_index += 1
        elif key == 'up' and self.show_history:
            if self.last_index < len(self.history):
                self.set_edit_text(self.history[self.last_index])
                self.edit_pos = len(self.history[self.last_index])
                self.last_index += 1
        elif key == 'meta enter':
            # When using multiline, enter inserts a new line into the editor
            # send the input to the server on alt + enter
            self.master.cb_send_to_server(msg)
            self.history.insert(0, msg)
            self.set_edit_text('')
            self.last_index = 0
            self.show_history = False
        else:
            self.show_history = False
            self.last_index = 0
            return super().keypress(size, key)
        return None


class EditorWidget(urwid.Filler):
    """
    Wraps CustomEdit
    """
    def __init__(self, master):
        super().__init__(Editor(master), valign='top')


class HistoryBox(urwid.ListBox):
    """
    Shows all the QMP message transmitted/received
    """
    def __init__(self, master):
        self.master = master
        self.history = urwid.SimpleFocusListWalker([])
        super().__init__(self.history)

    def add_to_history(self, history):
        self.history.append(urwid.Text(history))
        if self.history:
            self.history.set_focus(len(self.history) - 1)


class HistoryWindow(urwid.Frame):
    """
    Composes the HistoryBox and EditorWidget
    """
    def __init__(self, master):
        self.master = master
        self.editor = EditorWidget(master)
        self.editor_widget = urwid.LineBox(self.editor)
        self.history = HistoryBox(master)
        self.body = urwid.Pile([('weight', 80, self.history),
                                ('weight', 10, self.editor_widget)])
        super().__init__(self.body)
        urwid.connect_signal(self.master, UPDATE_MSG, self.cb_add_to_history)

    def cb_add_to_history(self, msg):
        formatted = []
        lexer = lexers.JsonLexer()  # pylint: disable=no-member
        for token in lexer.get_tokens(msg):
            formatted.append(token)
        self.history.add_to_history(formatted)


class Window(urwid.Frame):
    """
    This is going to be the main window that is going to compose other
    windows. In this stage it is unnecesssary but will be necessary in
    future when we will have multiple windows and want to the switch between
    them and display overlays
    """
    def __init__(self, master):
        self.master = master
        footer = StatusBar()
        body = HistoryWindow(master)
        super().__init__(body, footer=footer)


class App(QMP):
    def __init__(self, address):
        urwid.register_signal(self.__class__, UPDATE_MSG)
        self.window = Window(self)
        self.address = address
        self.aloop = asyncio.get_event_loop()
        self.loop = None
        self.screen = urwid.raw_display.Screen()
        super().__init__()

        # Gracefully handle SIGTERM and SIGINT signals
        cancel_signals = [signal.SIGTERM, signal.SIGINT]
        for sig in cancel_signals:
            self.aloop.add_signal_handler(sig, self.kill_app)

    def _cb_outbound(self, msg):
        urwid.emit_signal(self, UPDATE_MSG, "<-- " + str(msg))
        return msg

    def _cb_inbound(self, msg):
        urwid.emit_signal(self, UPDATE_MSG, "--> " + str(msg))
        return msg

    async def wait_for_events(self):
        async for event in self.events:
            self.handle_event(event)

    async def _send_to_server(self, msg):
        # TODO: Handle more validation errors (eg: ValueError)
        try:
            response = await self._raw(bytes(msg, 'utf-8'))
            logging.info('Response: %s %s', response, type(response))
        except ExecuteError:
            logging.info('Error response from server for msg: %s', msg)
        except ExecInterruptedError:
            logging.info('Error server disconnected before reply')
            # FIXME: Handle this better
            # Show the disconnected message in the history window
            urwid.emit_signal(self, UPDATE_MSG,
                              '{"error": "Server disconnected before reply"}')
            self.window.footer.set_text("Server disconnected")
        except Exception as err:
            logging.info('Exception from _send_to_server: %s', str(err))
            raise err

    def cb_send_to_server(self, msg):
        create_task(self._send_to_server(msg))

    def unhandled_input(self, key):
        if key == 'esc':
            self.kill_app()

    def kill_app(self):
        # TODO: Work on the disconnect logic
        create_task(self._kill_app())

    async def _kill_app(self):
        # It is ok to call disconnect even in disconnect state
        await self.disconnect()
        logging.info('disconnect finished, Exiting app')
        raise urwid.ExitMainLoop()

    def handle_event(self, event):
        if event['event'] == 'SHUTDOWN':
            self.window.footer.set_text('Server shutdown')

    async def connect_server(self):
        try:
            await self.connect(self.address)
            self.window.footer.set_text("Connected to {:s}".format(
                f"{self.address[0]}:{self.address[1]}"
                if isinstance(self.address, tuple)
                else self.address
            ))
        except ConnectError as err:
            logging.debug('Cannot connect to server %s', str(err))
            self.window.footer.set_text('Server shutdown')

    def run(self):
        self.screen.set_terminal_properties(256)

        self.aloop.set_debug(True)
        event_loop = urwid.AsyncioEventLoop(loop=self.aloop)
        self.loop = urwid.MainLoop(urwid.AttrMap(self.window, 'background'),
                                   unhandled_input=self.unhandled_input,
                                   screen=self.screen,
                                   palette=palette,
                                   handle_mouse=True,
                                   event_loop=event_loop)

        create_task(self.wait_for_events(), self.aloop)
        create_task(self.connect_server(), self.aloop)
        try:
            self.loop.run()
        except Exception as err:
            logging.error('%s\n%s\n', str(err), pretty_traceback())
            raise err


def main():
    parser = argparse.ArgumentParser(description='AQMP TUI')
    parser.add_argument('-a', '--address', metavar='IP:PORT', required=True,
                        help='Address of the QMP server', dest='address')
    parser.add_argument('--log', help='Address of the QMP server',
                        dest='log_file')
    args = parser.parse_args()

    logging.basicConfig(filename=args.log_file, level=logging.DEBUG)

    address = args.address.split(':')
    address[1] = int(address[1])

    App(tuple(address)).run()


if __name__ == '__main__':
    main()  # type: ignore
