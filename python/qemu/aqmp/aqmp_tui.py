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
from logging import Handler
import signal

import urwid
import urwid_readline

from .error import MultiException
from .protocol import ConnectError
from .qmp_protocol import QMP, ExecInterruptedError, ExecuteError
from .util import create_task, pretty_traceback


UPDATE_MSG = 'UPDATE_MSG'

# Using root logger to enable all loggers under qemu and asyncio
LOGGER = logging.getLogger()

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
                                ('weight', 20, self.editor_widget)])
        super().__init__(self.body)
        urwid.connect_signal(self.master, UPDATE_MSG, self.cb_add_to_history)

    def cb_add_to_history(self, msg, level=None):
        formatted = []
        if level:
            msg = f'[{level}]: {msg}'
            formatted.append(msg)
        else:
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
        urwid.register_signal(type(self), UPDATE_MSG)
        self.window = Window(self)
        self.address = address
        self.aloop = None
        self.loop = None
        super().__init__()

    def add_to_history(self, msg, level=None):
        urwid.emit_signal(self, UPDATE_MSG, msg, level)

    def _cb_outbound(self, msg):
        LOGGER.debug('Request: %s', str(msg))
        self.add_to_history('<-- ' + str(msg))
        return msg

    def _cb_inbound(self, msg):
        LOGGER.debug('Response: %s', str(msg))
        self.add_to_history('--> ' + str(msg))
        return msg

    async def wait_for_events(self):
        async for event in self.events:
            self.handle_event(event)

    async def _send_to_server(self, msg):
        # TODO: Handle more validation errors (eg: ValueError)
        try:
            await self._raw(bytes(msg, 'utf-8'))
        except ExecuteError:
            LOGGER.info('Error response from server for msg: %s', msg)
        except ExecInterruptedError:
            LOGGER.info('Error server disconnected before reply')
            # FIXME: Handle this better
            # Show the disconnected message in the history window
            urwid.emit_signal(self, UPDATE_MSG,
                              '{"error": "Server disconnected before reply"}')
            self.window.footer.set_text("Server disconnected")
        except Exception as err:
            LOGGER.error('Exception from _send_to_server: %s', str(err))
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
        try:
            await self.disconnect()
            LOGGER.debug('Disconnect finished. Exiting app')
        except MultiException as err:
            LOGGER.info('Multiple exception on disconnect: %s', str(err))
            # Let the app crash after providing a proper stack trace
            raise err
        raise urwid.ExitMainLoop()

    def handle_event(self, event):
        # FIXME: Consider all states present in qapi/run-state.json
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
            LOGGER.debug('Cannot connect to server %s', str(err))
            self.window.footer.set_text('Server shutdown')

    def run(self, debug=False):
        self.screen.set_terminal_properties(256)

        self.aloop = asyncio.get_event_loop()
        self.aloop.set_debug(debug)

        # Gracefully handle SIGTERM and SIGINT signals
        cancel_signals = [signal.SIGTERM, signal.SIGINT]
        for sig in cancel_signals:
            self.aloop.add_signal_handler(sig, self.kill_app)

        event_loop = urwid.AsyncioEventLoop(loop=self.aloop)
        self.loop = urwid.MainLoop(self.window,
                                   unhandled_input=self.unhandled_input,
                                   handle_mouse=True,
                                   event_loop=event_loop)

        create_task(self.wait_for_events(), self.aloop)
        create_task(self.connect_server(), self.aloop)
        try:
            self.loop.run()
        except Exception as err:
            LOGGER.error('%s\n%s\n', str(err), pretty_traceback())
            raise err


class TUILogHandler(Handler):
    def __init__(self, tui):
        super().__init__()
        self.tui = tui

    def emit(self, record):
        level = record.levelname
        msg = record.getMessage()
        self.tui.add_to_history(msg, level)


def parse_address(address):
    """
    This snippet was taken from qemu.qmp.__init__.
    pylint complaints about duplicate code so it has been
    temprorily disabled. This should be fixed once qmp is
    replaced by aqmp in the future.
    """
    components = address.split(':')
    if len(components) == 2:
        try:
            port = int(components[1])
        except ValueError:
            raise ValueError(f'Bad Port value in {address}') from None
        return (components[0], port)
    return address


def main():
    parser = argparse.ArgumentParser(description='AQMP TUI')
    parser.add_argument('qmp_server', help='Address of the QMP server'
                        '< UNIX socket path | TCP addr:port >')
    parser.add_argument('--log-file', help='The Log file name')
    parser.add_argument('--log-level', help='Log level <debug|info|error>',
                        default='debug')
    parser.add_argument('--debug', action='store_true',
                        help='Enable debug mode for asyncio loop'
                        'Generates lot of output, makes TUI unusable when'
                        'logs are logged in the TUI itself.'
                        'Use only when logging to a file')
    args = parser.parse_args()

    try:
        address = parse_address(args.qmp_server)
    except ValueError as err:
        parser.error(err)

    app = App(address)

    if args.log_file:
        LOGGER.addHandler(logging.FileHandler(args.log_file))
    else:
        LOGGER.addHandler(TUILogHandler(app))

    log_levels = {'debug': logging.DEBUG,
                  'info': logging.INFO,
                  'error': logging.ERROR}

    if args.log_level not in log_levels:
        parser.error('Invalid log level')
    LOGGER.setLevel(log_levels[args.log_level])

    app.run(args.debug)


if __name__ == '__main__':
    main()  # type: ignore
