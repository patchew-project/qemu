#!/usr/bin/python
# -*- coding: utf-8 -*-
"""
This file is an implementation of backup tool
"""
from argparse import ArgumentParser
import os
import errno
from socket import error as socket_error
import configparser
import sys
sys.path.append('../../scripts/qmp')
from qmp import QEMUMonitorProtocol


class BackupTool(object):
    """BackupTool Class"""
    def __init__(self, config_file='backup.ini'):
        self.config_file = config_file
        self.config = configparser.ConfigParser()
        self.config.read(self.config_file)

    def write_config(self):
        """
        Writes configuration to ini file.
        """
        with open(self.config_file, 'w') as config_file:
            self.config.write(config_file)

    def get_socket_path(self, socket_path, tcp):
        """
        Return Socket address in form of string or tuple
        """
        if tcp is False:
            return os.path.abspath(socket_path)
        return (socket_path.split(':')[0], int(socket_path.split(':')[1]))

    def __full_backup(self, guest_name):
        """
        Performs full backup of guest
        """
        if guest_name not in self.config.sections():
            print ("Cannot find specified guest")
            return
        if self.is_guest_running(guest_name, self.config[guest_name]['qmp'],
                                 self.config[guest_name]['tcp']) is False:
            return
        connection = QEMUMonitorProtocol(
                                         self.get_socket_path(
                                             self.config[guest_name]['qmp'],
                                             self.config[guest_name]['tcp']))
        connection.connect()
        cmd = {"execute": "transaction", "arguments": {"actions": []}}
        for key in self.config[guest_name]:
            if key.startswith("drive_"):
                drive = key[key.index('_')+1:]
                target = self.config[guest_name][key]
                sub_cmd = {"type": "drive-backup", "data": {"device": drive,
                                                            "target": target,
                                                            "sync": "full"}}
                cmd['arguments']['actions'].append(sub_cmd)
        print (connection.cmd_obj(cmd))

    def __drive_add(self, drive_id, guest_name, target=None):
        """
        Adds drive for backup
        """
        if target is None:
            target = os.path.abspath(drive_id) + ".img"

        if guest_name not in self.config.sections():
            print ("Cannot find specified guest")
            return

        if "drive_"+drive_id in self.config[guest_name]:
            print ("Drive already marked for backup")
            return

        if self.is_guest_running(guest_name, self.config[guest_name]['qmp'],
                                 self.config[guest_name]['tcp']) is False:
            return

        connection = QEMUMonitorProtocol(
                                         self.get_socket_path(
                                             self.config[guest_name]['qmp'],
                                             self.config[guest_name]['tcp']))
        connection.connect()
        cmd = {'execute': 'query-block'}
        returned_json = connection.cmd_obj(cmd)
        device_present = False
        for device in returned_json['return']:
            if device['device'] == drive_id:
                device_present = True
                break

        if device_present is False:
            print ("No such drive in guest")
            return

        drive_id = "drive_" + drive_id
        for id in self.config[guest_name]:
            if self.config[guest_name][id] == target:
                print ("Please choose different target")
                return
        self.config.set(guest_name, drive_id, target)
        self.write_config()
        print("Successfully Added Drive")

    def is_guest_running(self, guest_name, socket_path, tcp):
        """
        Checks whether specified guest is running or not
        """
        try:
            connection = QEMUMonitorProtocol(
                                             self.get_socket_path(
                                                  socket_path, tcp))
            connection.connect()
        except socket_error:
            if socket_error.errno != errno.ECONNREFUSED:
                print ("Connection to guest refused")
            return False
        except:
            print ("Unable to connect to guest")
            return False
        return True

    def __guest_add(self, guest_name, socket_path, tcp):
        """
        Adds a guest to the config file
        """
        if self.is_guest_running(guest_name, socket_path, tcp) is False:
            return

        if guest_name in self.config.sections():
            print ("ID already exists. Please choose a different guestname")
            return

        self.config[guest_name] = {'qmp': socket_path}
        self.config.set(guest_name, 'tcp', str(tcp))
        self.write_config()
        print("Successfully Added Guest")

    def __guest_remove(self, guest_name):
        """
        Removes a guest from config file
        """
        if guest_name not in self.config.sections():
            print("Guest Not present")
            return
        self.config.remove_section(guest_name)
        print("Guest successfully deleted")

    def guest_remove_wrapper(self, args):
        """
        Wrapper for __guest_remove method.
        """
        guest_name = args.guest
        self.__guest_remove(guest_name)
        self.write_config()

    def list(self, args):
        """
        Prints guests present in Config file
        """
        for guest_name in self.config.sections():
            print(guest_name)

    def guest_add_wrapper(self, args):
        """
        Wrapper for __quest_add method
        """
        if args.tcp is False:
            self.__guest_add(args.guest, args.qmp, False)
        else:
            self.__guest_add(args.guest, args.qmp, True)

    def drive_add_wrapper(self, args):
        """
        Wrapper for __drive_add method
        """
        self.__drive_add(args.id, args.guest, args.target)

    def fullbackup_wrapper(self, args):
        """
        Wrapper for __full_backup method
        """
        self.__full_backup(args.guest)


def main():
    backup_tool = BackupTool()
    parser = ArgumentParser()
    subparsers = parser.add_subparsers(title='Subcommands',
                                       description='Valid Subcommands',
                                       help='Subcommand help')
    guest_parser = subparsers.add_parser('guest', help='Adds or \
                                                   removes and lists guest(s)')
    guest_subparsers = guest_parser.add_subparsers(title='Guest Subparser')
    guest_list_parser = guest_subparsers.add_parser('list',
                                                    help='Lists all guests')
    guest_list_parser.set_defaults(func=backup_tool.list)

    guest_add_parser = guest_subparsers.add_parser('add', help='Adds a guest')
    guest_add_parser.add_argument('--guest', action='store', type=str,
                                  help='Name of the guest')
    guest_add_parser.add_argument('--qmp', action='store', type=str,
                                  help='Path of socket')
    guest_add_parser.add_argument('--tcp', nargs='?', type=bool,
                                  default=False,
                                  help='Specify if socket is tcp')
    guest_add_parser.set_defaults(func=backup_tool.guest_add_wrapper)

    guest_remove_parser = guest_subparsers.add_parser('remove',
                                                      help='removes a guest')
    guest_remove_parser.add_argument('--guest', action='store', type=str,
                                     help='Name of the guest')
    guest_remove_parser.set_defaults(func=backup_tool.guest_remove_wrapper)

    drive_parser = subparsers.add_parser('drive',
                                         help='Adds drive(s) for backup')
    drive_subparsers = drive_parser.add_subparsers(title='Add subparser',
                                                   description='Drive \
                                                                subparser')
    drive_add_parser = drive_subparsers.add_parser('add',
                                                   help='Adds new \
                                                         drive for backup')
    drive_add_parser.add_argument('--guest', action='store',
                                  type=str, help='Name of the guest')
    drive_add_parser.add_argument('--id', action='store',
                                  type=str, help='Drive ID')
    drive_add_parser.add_argument('--target', nargs='?',
                                  default=None, help='Destination path')
    drive_add_parser.set_defaults(func=backup_tool.drive_add_wrapper)

    backup_parser = subparsers.add_parser('backup', help='Creates backup')
    backup_parser.add_argument('--guest', action='store',
                               type=str, help='Name of the guest')
    backup_parser.set_defaults(func=backup_tool.fullbackup_wrapper)

    args = parser.parse_args()
    args.func(args)

if __name__ == '__main__':
    main()
