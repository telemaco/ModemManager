/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 */

#include <config.h>
#include <signal.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include <gio/gio.h>

#include "ModemManager.h"

#include "mm-manager.h"
#include "mm-log.h"
#include "mm-context.h"

#if !defined(MM_DIST_VERSION)
# define MM_DIST_VERSION VERSION
#endif

static GMainLoop *loop;
static MMManager *manager;

static void
mm_signal_handler (int signo)
{
    if (signo == SIGUSR1)
        mm_log_usr1 ();
	else if (signo == SIGINT || signo == SIGTERM) {
		mm_info ("Caught signal %d, shutting down...", signo);
        if (loop)
            g_main_loop_quit (loop);
        else
            _exit (0);
    }
}

static void
setup_signals (void)
{
    struct sigaction action;
    sigset_t mask;

    sigemptyset (&mask);
    action.sa_handler = mm_signal_handler;
    action.sa_mask = mask;
    action.sa_flags = 0;
    sigaction (SIGUSR1, &action, NULL);
    sigaction (SIGTERM, &action, NULL);
    sigaction (SIGINT, &action, NULL);
}

static void
name_acquired_cb (GDBusConnection *connection,
                  const gchar *name,
                  gpointer user_data)
{
    GError *error = NULL;

    mm_dbg ("Service name '%s' was acquired", name);

    /* Create Manager object */
    g_assert (!manager);
    manager = mm_manager_new (connection, &error);
    if (!manager) {
        mm_warn ("Could not create manager: %s", error->message);
        g_error_free (error);
        g_main_loop_quit (loop);
        return;
    }

    /* Launch scan for devices */
    mm_manager_start (manager);
}

static void
name_lost_cb (GDBusConnection *connection,
              const gchar *name,
              gpointer user_data)
{
    /* Note that we're not allowing replacement, so once the name acquired, the
     * process won't lose it. */
    mm_warn ("Could not acquire the '%s' service name", name);
    g_main_loop_quit (loop);
}

int
main (int argc, char *argv[])
{
    GDBusConnection *bus;
    GError *err = NULL;
    guint name_id;

    g_type_init ();

    /* Setup application context */
    mm_context_init (argc, argv);

    if (!mm_log_setup (mm_context_get_log_level (),
                       mm_context_get_log_file (),
                       mm_context_get_timestamps (),
                       mm_context_get_relative_timestamps (),
                       mm_context_get_debug (),
                       &err)) {
        g_warning ("Failed to set up logging: %s", err->message);
        g_error_free (err);
        exit (1);
    }

    setup_signals ();

    mm_info ("ModemManager (version " MM_DIST_VERSION ") starting...");

    /* Get system bus */
    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &err);
    if (!bus) {
        g_warning ("Could not get the system bus. Make sure "
                   "the message bus daemon is running! Message: %s",
                   err->message);
        g_error_free (err);
        return -1;
    }

    /* Acquire name, don't allow replacement */
    name_id = g_bus_own_name_on_connection (bus,
                                            MM_DBUS_SERVICE,
                                            G_BUS_NAME_OWNER_FLAGS_NONE,
                                            name_acquired_cb,
                                            name_lost_cb,
                                            NULL,
                                            NULL);

    /* Go into the main loop */
    loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (loop);

    if (manager) {
        mm_manager_shutdown (manager);

        /* Wait for all modems to be removed */
        while (mm_manager_num_modems (manager)) {
            GMainContext *ctx = g_main_loop_get_context (loop);

            g_main_context_iteration (ctx, FALSE);
            g_usleep (50);
        }

        g_object_unref (manager);
    }

    g_bus_unown_name (name_id);
    g_object_unref (bus);

    mm_log_shutdown ();

    return 0;
}
