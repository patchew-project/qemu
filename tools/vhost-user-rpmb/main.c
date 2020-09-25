/*
 * VIRTIO RPMB Emulation via vhost-user
 *
 * Copyright (c) 2020 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <glib.h>
#include <stdio.h>

static gchar *socket_path;
static gint socket_fd;
static gboolean print_cap;

static GOptionEntry options[] =
{
    { "socket-path", 0, 0, G_OPTION_ARG_FILENAME, &socket_path, "Location of vhost-user Unix domain socket, incompatible with --fd", "PATH" },
    { "fd", 0, 0, G_OPTION_ARG_INT, &socket_fd, "Specify the file-descriptor of the backend, incompatible with --socket-path", "FD" },
    { "print-capabilities", 0, 0, G_OPTION_ARG_NONE, &print_cap, "Output to stdout the backend capabilities in JSON format and exit", NULL},
    { NULL }
};

/* Print vhost-user.json backend program capabilities */
static void print_capabilities(void)
{
    printf("{\n");
    printf("  \"type\": \"block\"\n");
    printf("}\n");
}

int main (int argc, char *argv[])
{
    GError *error = NULL;
    GOptionContext *context;

    context = g_option_context_new ("vhost-user-rpmb - vhost-user emulation of RPBM device");
    g_option_context_add_main_entries (context, options, "vhost-user-rpmb");
    if (!g_option_context_parse (context, &argc, &argv, &error))
    {
        g_print ("option parsing failed: %s\n", error->message);
        exit (1);
    }

    if (print_cap) {
        print_capabilities();
        exit(0);
    }

}
