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
 * Copyright (C) 2011 Google, Inc.
 */

#ifndef MM_BEARER_H
#define MM_BEARER_H

#include <glib.h>
#include <glib-object.h>

#include <libmm-common.h>

#include "mm-base-modem.h"

#define MM_TYPE_BEARER            (mm_bearer_get_type ())
#define MM_BEARER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BEARER, MMBearer))
#define MM_BEARER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BEARER, MMBearerClass))
#define MM_IS_BEARER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BEARER))
#define MM_IS_BEARER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BEARER))
#define MM_BEARER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BEARER, MMBearerClass))

typedef struct _MMBearer MMBearer;
typedef struct _MMBearerClass MMBearerClass;
typedef struct _MMBearerPrivate MMBearerPrivate;

#define MM_BEARER_PATH       "bearer-path"
#define MM_BEARER_CONNECTION "bearer-connection"
#define MM_BEARER_MODEM      "bearer-modem"
#define MM_BEARER_STATUS     "bearer-status"

typedef enum { /*< underscore_name=mm_bearer_status >*/
    MM_BEARER_STATUS_DISCONNECTED,
    MM_BEARER_STATUS_DISCONNECTING,
    MM_BEARER_STATUS_CONNECTING,
    MM_BEARER_STATUS_CONNECTED,
} MMBearerStatus;

struct _MMBearer {
    MmGdbusBearerSkeleton parent;
    MMBearerPrivate *priv;
};

struct _MMBearerClass {
    MmGdbusBearerSkeletonClass parent;

    /* Connect this bearer */
    void (* connect) (MMBearer *bearer,
                      GCancellable *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer user_data);
    gboolean (* connect_finish) (MMBearer *bearer,
                                 GAsyncResult *res,
                                 MMPort **data,
                                 MMBearerIpConfig **ipv4_config,
                                 MMBearerIpConfig **ipv6_config,
                                 GError **error);

    /* Disconnect this bearer */
    void (* disconnect) (MMBearer *bearer,
                         GAsyncReadyCallback callback,
                         gpointer user_data);
    gboolean (* disconnect_finish) (MMBearer *bearer,
                                    GAsyncResult *res,
                                    GError **error);

    /* Report disconnection */
    void (* report_disconnection) (MMBearer *bearer);

    /* Check if the bearer has the exact same properties */
    gboolean (* cmp_properties) (MMBearer *self,
                                 MMBearerProperties *properties);

    /* Builder of the properties to be exposed in DBus. Bearers should expose only
     * the input properties they actually ended up using */
    MMBearerProperties * (* expose_properties) (MMBearer *self);
};

GType mm_bearer_get_type (void);

void         mm_bearer_export   (MMBearer *self);
const gchar *mm_bearer_get_path (MMBearer *bearer);

void mm_bearer_expose_properties (MMBearer *bearer,
                                  MMBearerProperties *properties);

MMBearerStatus mm_bearer_get_status (MMBearer *bearer);

void     mm_bearer_connect        (MMBearer *self,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
gboolean mm_bearer_connect_finish (MMBearer *self,
                                   GAsyncResult *res,
                                   GError **error);

void     mm_bearer_disconnect        (MMBearer *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
gboolean mm_bearer_disconnect_finish (MMBearer *self,
                                      GAsyncResult *res,
                                      GError **error);

void mm_bearer_disconnect_force (MMBearer *self);

void mm_bearer_report_disconnection (MMBearer *self);

gboolean mm_bearer_cmp_properties (MMBearer *self,
                                   MMBearerProperties *properties);

#endif /* MM_BEARER_H */
