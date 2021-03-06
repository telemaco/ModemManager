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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_PORT_PROBE_H
#define MM_PORT_PROBE_H

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gudev/gudev.h>

#include "mm-port-probe-at.h"
#include "mm-at-serial-port.h"

#define MM_TYPE_PORT_PROBE            (mm_port_probe_get_type ())
#define MM_PORT_PROBE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PORT_PROBE, MMPortProbe))
#define MM_PORT_PROBE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PORT_PROBE, MMPortProbeClass))
#define MM_IS_PORT_PROBE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PORT_PROBE))
#define MM_IS_PLUBIN_PROBE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PORT_PROBE))
#define MM_PORT_PROBE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PORT_PROBE, MMPortProbeClass))

/* Flags to request port probing */
typedef enum { /*< underscore_name=mm_port_probe_flag >*/
	MM_PORT_PROBE_NONE       = 0,
	MM_PORT_PROBE_AT         = 1 << 0,
	MM_PORT_PROBE_AT_VENDOR  = 1 << 1,
	MM_PORT_PROBE_AT_PRODUCT = 1 << 2,
	MM_PORT_PROBE_QCDM       = 1 << 3,
} MMPortProbeFlag;

typedef struct _MMPortProbe MMPortProbe;
typedef struct _MMPortProbeClass MMPortProbeClass;
typedef struct _MMPortProbePrivate MMPortProbePrivate;

struct _MMPortProbe {
    GObject parent;
    MMPortProbePrivate *priv;
};

struct _MMPortProbeClass {
    GObjectClass parent;
};

GType mm_port_probe_get_type (void);

MMPortProbe *mm_port_probe_new (GUdevDevice *port,
                                const gchar *physdev_path,
                                const gchar *driver);

GUdevDevice *mm_port_probe_get_port         (MMPortProbe *self);
const gchar *mm_port_probe_get_port_name    (MMPortProbe *self);
const gchar *mm_port_probe_get_port_subsys  (MMPortProbe *self);
const gchar *mm_port_probe_get_port_physdev (MMPortProbe *self);
const gchar *mm_port_probe_get_port_driver  (MMPortProbe *self);

/* Probing result setters */
void mm_port_probe_set_result_at         (MMPortProbe *self,
                                          gboolean at);
void mm_port_probe_set_result_at_vendor  (MMPortProbe *self,
                                          const gchar *at_vendor);
void mm_port_probe_set_result_at_product (MMPortProbe *self,
                                          const gchar *at_product);
void mm_port_probe_set_result_qcdm       (MMPortProbe *self,
                                          gboolean qcdm);

/* Run probing */
void     mm_port_probe_run        (MMPortProbe *self,
                                   MMPortProbeFlag flags,
                                   guint64 at_send_delay,
                                   const MMPortProbeAtCommand *at_custom_init,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
gboolean mm_port_probe_run_finish (MMPortProbe *self,
                                   GAsyncResult *result,
                                   GError **error);
gboolean mm_port_probe_run_cancel (MMPortProbe *self);

gboolean mm_port_probe_run_cancel_at_probing (MMPortProbe *self);

/* Probing result getters */
MMPortType    mm_port_probe_get_port_type    (MMPortProbe *self);
gboolean      mm_port_probe_is_at            (MMPortProbe *self);
gboolean      mm_port_probe_is_qcdm          (MMPortProbe *self);
const gchar  *mm_port_probe_get_vendor       (MMPortProbe *self);
const gchar  *mm_port_probe_get_product      (MMPortProbe *self);

#endif /* MM_PORT_PROBE_H */
