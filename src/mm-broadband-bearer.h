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
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 *
 * Copyright (C) 2011 - 2012 Google, Inc.
 */

#ifndef MM_BROADBAND_BEARER_H
#define MM_BROADBAND_BEARER_H

#include <glib.h>
#include <glib-object.h>

#include <libmm-common.h>

#include "mm-bearer.h"
#include "mm-broadband-modem.h"

#define MM_TYPE_BROADBAND_BEARER            (mm_broadband_bearer_get_type ())
#define MM_BROADBAND_BEARER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_BEARER, MMBroadbandBearer))
#define MM_BROADBAND_BEARER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_BEARER, MMBroadbandBearerClass))
#define MM_IS_BROADBAND_BEARER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_BEARER))
#define MM_IS_BROADBAND_BEARER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_BEARER))
#define MM_BROADBAND_BEARER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_BEARER, MMBroadbandBearerClass))

#define MM_BROADBAND_BEARER_3GPP_APN         "broadband-bearer-3gpp-apn"
#define MM_BROADBAND_BEARER_CDMA_NUMBER      "broadband-bearer-cdma-number"
#define MM_BROADBAND_BEARER_CDMA_RM_PROTOCOL "broadband-bearer-cdma-rm-protocol"
#define MM_BROADBAND_BEARER_IP_TYPE          "broadband-bearer-ip-type"
#define MM_BROADBAND_BEARER_ALLOW_ROAMING    "broadband-bearer-allow-roaming"

typedef struct _MMBroadbandBearer MMBroadbandBearer;
typedef struct _MMBroadbandBearerClass MMBroadbandBearerClass;
typedef struct _MMBroadbandBearerPrivate MMBroadbandBearerPrivate;

struct _MMBroadbandBearer {
    MMBearer parent;
    MMBroadbandBearerPrivate *priv;
};

struct _MMBroadbandBearerClass {
    MMBearerClass parent;

    /* Full 3GPP connection sequence */
    void     (* connect_3gpp)        (MMBroadbandBearer *self,
                                      MMBroadbandModem *modem,
                                      MMAtSerialPort *primary,
                                      MMAtSerialPort *secondary,
                                      MMPort *data,
                                      GCancellable *cancellable,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
    gboolean (* connect_3gpp_finish) (MMBroadbandBearer *self,
                                      GAsyncResult *res,
                                      MMBearerIpConfig **ipv4_config,
                                      MMBearerIpConfig **ipv6_config,
                                      GError **error);

    /* Dialing sub-part of 3GPP connection */
    void     (* dial_3gpp)        (MMBroadbandBearer *self,
                                   MMBaseModem *modem,
                                   MMAtSerialPort *primary,
                                   guint cid,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
    gboolean (* dial_3gpp_finish) (MMBroadbandBearer *self,
                                   GAsyncResult *res,
                                   GError **error);

    /* IP config retrieval sub-part of 3GPP connection.
     * Only really required when using net port + static IP address. */
    void     (* get_ip_config_3gpp) (MMBroadbandBearer *self,
                                     MMBroadbandModem *modem,
                                     MMAtSerialPort *primary,
                                     MMAtSerialPort *secondary,
                                     MMPort *data,
                                     guint cid,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
    gboolean (* get_ip_config_3gpp_finish) (MMBroadbandBearer *self,
                                            GAsyncResult *res,
                                            MMBearerIpConfig **ipv4_config,
                                            MMBearerIpConfig **ipv6_config,
                                            GError **error);

    /* Full 3GPP disconnection sequence */
    void     (* disconnect_3gpp)        (MMBroadbandBearer *self,
                                         MMBroadbandModem *modem,
                                         MMAtSerialPort *primary,
                                         MMAtSerialPort *secondary,
                                         MMPort *data,
                                         guint cid,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data);
    gboolean (* disconnect_3gpp_finish) (MMBroadbandBearer *self,
                                         GAsyncResult *res,
                                         GError **error);

    /* Full CDMA connection sequence */
    void     (* connect_cdma)        (MMBroadbandBearer *self,
                                      MMBroadbandModem *modem,
                                      MMAtSerialPort *primary,
                                      MMAtSerialPort *secondary,
                                      MMPort *data,
                                      GCancellable *cancellable,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
    gboolean (* connect_cdma_finish) (MMBroadbandBearer *self,
                                      GAsyncResult *res,
                                      MMBearerIpConfig **ipv4_config,
                                      MMBearerIpConfig **ipv6_config,
                                      GError **error);

    /* Full CDMA disconnection sequence */
    void     (* disconnect_cdma)        (MMBroadbandBearer *self,
                                         MMBroadbandModem *modem,
                                         MMAtSerialPort *primary,
                                         MMAtSerialPort *secondary,
                                         MMPort *data,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data);
    gboolean (* disconnect_cdma_finish) (MMBroadbandBearer *self,
                                         GAsyncResult *res,
                                         GError **error);
};

GType mm_broadband_bearer_get_type (void);

/* Default 3GPP bearer creation implementation */
void mm_broadband_bearer_new (MMBroadbandModem *modem,
                              MMBearerProperties *properties,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data);
MMBearer *mm_broadband_bearer_new_finish (GAsyncResult *res,
                                          GError **error);

const gchar *mm_broadband_bearer_get_3gpp_apn         (MMBroadbandBearer *self);
guint        mm_broadband_bearer_get_cdma_rm_protocol (MMBroadbandBearer *self);
const gchar *mm_broadband_bearer_get_ip_type          (MMBroadbandBearer *self);
gboolean     mm_broadband_bearer_get_allow_roaming    (MMBroadbandBearer *self);

guint        mm_broadband_bearer_get_3gpp_cid         (MMBroadbandBearer *self);

#endif /* MM_BROADBAND_BEARER_H */
