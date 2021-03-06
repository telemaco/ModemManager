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
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef MM_IFACE_MODEM_MESSAGING_H
#define MM_IFACE_MODEM_MESSAGING_H

#include <glib-object.h>
#include <gio/gio.h>

#include "mm-at-serial-port.h"
#include "mm-sms-part.h"
#include "mm-sms.h"

#define MM_TYPE_IFACE_MODEM_MESSAGING               (mm_iface_modem_messaging_get_type ())
#define MM_IFACE_MODEM_MESSAGING(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_IFACE_MODEM_MESSAGING, MMIfaceModemMessaging))
#define MM_IS_IFACE_MODEM_MESSAGING(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_IFACE_MODEM_MESSAGING))
#define MM_IFACE_MODEM_MESSAGING_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_IFACE_MODEM_MESSAGING, MMIfaceModemMessaging))

#define MM_IFACE_MODEM_MESSAGING_DBUS_SKELETON    "iface-modem-messaging-dbus-skeleton"
#define MM_IFACE_MODEM_MESSAGING_SMS_LIST         "iface-modem-messaging-sms-list"
#define MM_IFACE_MODEM_MESSAGING_SMS_PDU_MODE     "iface-modem-messaging-sms-pdu-mode"
#define MM_IFACE_MODEM_MESSAGING_SMS_MEM1_STORAGE "iface-modem-messaging-sms-mem1-storage"
#define MM_IFACE_MODEM_MESSAGING_SMS_MEM2_STORAGE "iface-modem-messaging-sms-mem2-storage"
#define MM_IFACE_MODEM_MESSAGING_SMS_MEM3_STORAGE "iface-modem-messaging-sms-mem3-storage"

typedef struct _MMIfaceModemMessaging MMIfaceModemMessaging;

struct _MMIfaceModemMessaging {
    GTypeInterface g_iface;

    /* Check for Messaging support (async) */
    void (* check_support) (MMIfaceModemMessaging *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data);
    gboolean (*check_support_finish) (MMIfaceModemMessaging *self,
                                      GAsyncResult *res,
                                      GError **error);

    /* Load supported storages for...
     *  mem1: listing/reading/deleting
     *  mem2: writing/sending
     *  mem3: receiving
     */
    void (* load_supported_storages) (MMIfaceModemMessaging *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
    gboolean (*load_supported_storages_finish) (MMIfaceModemMessaging *self,
                                                GAsyncResult *res,
                                                GArray **mem1,
                                                GArray **mem2,
                                                GArray **mem3,
                                                GError **error);

    /* Set preferred storages (async) */
    void (* set_preferred_storages) (MMIfaceModemMessaging *self,
                                     MMSmsStorage mem1,
                                     MMSmsStorage mem2,
                                     MMSmsStorage mem3,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
    gboolean (*set_preferred_storages_finish) (MMIfaceModemMessaging *self,
                                               GAsyncResult *res,
                                               GError **error);

    /* Setup SMS format (async) */
    void (* setup_sms_format) (MMIfaceModemMessaging *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data);
    gboolean (*setup_sms_format_finish) (MMIfaceModemMessaging *self,
                                         GAsyncResult *res,
                                         GError **error);

    /* Asynchronous setting up unsolicited SMS reception events */
    void (*setup_unsolicited_events) (MMIfaceModemMessaging *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
    gboolean (*setup_unsolicited_events_finish) (MMIfaceModemMessaging *self,
                                                 GAsyncResult *res,
                                                 GError **error);

    /* Asynchronous cleaning up of unsolicited SMS reception events */
    void (*cleanup_unsolicited_events) (MMIfaceModemMessaging *self,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
    gboolean (*cleanup_unsolicited_events_finish) (MMIfaceModemMessaging *self,
                                                   GAsyncResult *res,
                                                   GError **error);

    /* Asynchronous enabling unsolicited SMS reception events */
    void (* enable_unsolicited_events) (MMIfaceModemMessaging *self,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
    gboolean (* enable_unsolicited_events_finish) (MMIfaceModemMessaging *self,
                                                   GAsyncResult *res,
                                                   GError **error);

    /* Asynchronous disabling unsolicited SMS reception events */
    void (* disable_unsolicited_events) (MMIfaceModemMessaging *self,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data);
    gboolean (* disable_unsolicited_events_finish) (MMIfaceModemMessaging *self,
                                                    GAsyncResult *res,
                                                    GError **error);

    /* Load initial SMS parts (async).
     * Found parts need to be reported with take_part() */
    void (* load_initial_sms_parts) (MMIfaceModemMessaging *self,
                                     MMSmsStorage storage,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
    gboolean (*load_initial_sms_parts_finish) (MMIfaceModemMessaging *self,
                                               GAsyncResult *res,
                                               GError **error);

    /* Create SMS objects */
    MMSms * (* create_sms) (MMBaseModem *self);
};

GType mm_iface_modem_messaging_get_type (void);

/* Initialize Messaging interface (async) */
void     mm_iface_modem_messaging_initialize        (MMIfaceModemMessaging *self,
                                                     GCancellable *cancellable,
                                                     GAsyncReadyCallback callback,
                                                     gpointer user_data);
gboolean mm_iface_modem_messaging_initialize_finish (MMIfaceModemMessaging *self,
                                                     GAsyncResult *res,
                                                     GError **error);

/* Enable Messaging interface (async) */
void     mm_iface_modem_messaging_enable        (MMIfaceModemMessaging *self,
                                                 GCancellable *cancellable,
                                                 GAsyncReadyCallback callback,
                                                 gpointer user_data);
gboolean mm_iface_modem_messaging_enable_finish (MMIfaceModemMessaging *self,
                                                 GAsyncResult *res,
                                                 GError **error);

/* Disable Messaging interface (async) */
void     mm_iface_modem_messaging_disable        (MMIfaceModemMessaging *self,
                                                  GAsyncReadyCallback callback,
                                                  gpointer user_data);
gboolean mm_iface_modem_messaging_disable_finish (MMIfaceModemMessaging *self,
                                                  GAsyncResult *res,
                                                  GError **error);

/* Shutdown Messaging interface */
void mm_iface_modem_messaging_shutdown (MMIfaceModemMessaging *self);

/* Bind properties for simple GetStatus() */
void mm_iface_modem_messaging_bind_simple_status (MMIfaceModemMessaging *self,
                                                  MMSimpleStatus *status);

/* Report new SMS part */
gboolean mm_iface_modem_messaging_take_part (MMIfaceModemMessaging *self,
                                             MMSmsPart *sms_part,
                                             MMSmsState state,
                                             MMSmsStorage storage);

/* Set preferred storages */
void mm_iface_modem_messaging_set_preferred_storages (MMIfaceModemMessaging *self,
                                                      MMSmsStorage mem1,
                                                      MMSmsStorage mem2,
                                                      MMSmsStorage mem3,
                                                      GAsyncReadyCallback callback,
                                                      gpointer user_data);
gboolean mm_iface_modem_messaging_set_preferred_storages_finish (MMIfaceModemMessaging *self,
                                                                 GAsyncResult *res,
                                                                 GError **error);

#endif /* MM_IFACE_MODEM_MESSAGING_H */
