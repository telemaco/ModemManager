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
 * Copyright (C) 2009 - 2010 Red Hat, Inc.
 */

#define _GNU_SOURCE  /* for strcasestr */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include <gudev/gudev.h>

#include <ModemManager.h>
#include <mm-errors-types.h>

#include "mm-plugin-base.h"
#include "mm-port-probe-cache.h"
#include "mm-at-serial-port.h"
#include "mm-qcdm-serial-port.h"
#include "mm-serial-parsers.h"
#include "mm-marshal.h"
#include "mm-utils.h"
#include "mm-private-boxed-types.h"
#include "libqcdm/src/commands.h"
#include "libqcdm/src/utils.h"
#include "libqcdm/src/errors.h"
#include "mm-log.h"

static void plugin_init (MMPlugin *plugin_class);

G_DEFINE_TYPE_EXTENDED (MMPluginBase, mm_plugin_base, G_TYPE_OBJECT,
                        0, G_IMPLEMENT_INTERFACE (MM_TYPE_PLUGIN, plugin_init))

#define MM_PLUGIN_BASE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_PLUGIN_BASE, MMPluginBasePrivate))

/* Virtual port corresponding to the embeded modem */
static gchar *virtual_port[] = {"smd0", NULL};

typedef struct {
    gchar *name;
    GUdevClient *client;
    GHashTable *tasks;

    /* Plugin-specific setups */
    gchar **subsystems;
    gchar **drivers;
    guint16 *vendor_ids;
    mm_uint16_pair *product_ids;
    gchar **vendor_strings;
    mm_str_pair *product_strings;
    gchar **udev_tags;
    gboolean at;
    gboolean single_at;
    gboolean qcdm;
    MMPortProbeAtCommand *custom_init;
    guint64 send_delay;
} MMPluginBasePrivate;

enum {
    PROP_0,
    PROP_NAME,
    PROP_ALLOWED_SUBSYSTEMS,
    PROP_ALLOWED_DRIVERS,
    PROP_ALLOWED_VENDOR_IDS,
    PROP_ALLOWED_PRODUCT_IDS,
    PROP_ALLOWED_VENDOR_STRINGS,
    PROP_ALLOWED_PRODUCT_STRINGS,
    PROP_ALLOWED_UDEV_TAGS,
    PROP_ALLOWED_AT,
    PROP_ALLOWED_SINGLE_AT,
    PROP_ALLOWED_QCDM,
    PROP_CUSTOM_INIT,
    PROP_SEND_DELAY,
    LAST_PROP
};

/*****************************************************************************/

gboolean
mm_plugin_base_get_device_ids (MMPluginBase *self,
                               const char *subsys,
                               const char *name,
                               guint16 *vendor,
                               guint16 *product)
{
    MMPluginBasePrivate *priv;
    GUdevDevice *device = NULL, *parent = NULL;
    const char *vid = NULL, *pid = NULL, *parent_subsys;
    gboolean success = FALSE;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (MM_IS_PLUGIN_BASE (self), FALSE);
    g_return_val_if_fail (subsys != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    if (vendor)
        g_return_val_if_fail (*vendor == 0, FALSE);
    if (product)
        g_return_val_if_fail (*product == 0, FALSE);

    priv = MM_PLUGIN_BASE_GET_PRIVATE (self);

    device = g_udev_client_query_by_subsystem_and_name (priv->client, subsys, name);
    if (!device)
        goto out;

    parent = g_udev_device_get_parent (device);
    if (parent) {
        parent_subsys = g_udev_device_get_subsystem (parent);
        if (parent_subsys) {
            if (!strcmp (parent_subsys, "bluetooth")) {
                /* Bluetooth devices report the VID/PID of the BT adapter here,
                 * which isn't really what we want.  Just return null IDs instead.
                 */
                success = TRUE;
                goto out;
            } else if (!strcmp (parent_subsys, "pcmcia")) {
                /* For PCMCIA devices we need to grab the PCMCIA subsystem's
                 * manfid and cardid, since any IDs on the tty device itself
                 * may be from PCMCIA controller or something else.
                 */
                vid = g_udev_device_get_sysfs_attr (parent, "manf_id");
                pid = g_udev_device_get_sysfs_attr (parent, "card_id");
                if (!vid || !pid)
                    goto out;
            } else if (!strcmp (parent_subsys, "platform")) {
                /* Platform devices don't usually have a VID/PID */
                success = TRUE;
                goto out;
            }
        }
    }

    if (!vid)
        vid = g_udev_device_get_property (device, "ID_VENDOR_ID");
    if (!vid)
        goto out;

    if (strncmp (vid, "0x", 2) == 0)
        vid += 2;
    if (strlen (vid) != 4)
        goto out;

    if (vendor) {
        *vendor = (guint16) (utils_hex2byte (vid + 2) & 0xFF);
        *vendor |= (guint16) ((utils_hex2byte (vid) & 0xFF) << 8);
    }

    if (!pid)
        pid = g_udev_device_get_property (device, "ID_MODEL_ID");
    if (!pid) {
        *vendor = 0;
        goto out;
    }

    if (strncmp (pid, "0x", 2) == 0)
        pid += 2;
    if (strlen (pid) != 4) {
        *vendor = 0;
        goto out;
    }

    if (product) {
        *product = (guint16) (utils_hex2byte (pid + 2) & 0xFF);
        *product |= (guint16) ((utils_hex2byte (pid) & 0xFF) << 8);
    }

    success = TRUE;

out:
    if (device)
        g_object_unref (device);
    if (parent)
        g_object_unref (parent);
    return success;
}

static char *
get_key (const char *subsys, const char *name)
{
    return g_strdup_printf ("%s%s", subsys, name);
}

static const char *
get_name (MMPlugin *plugin)
{
    return MM_PLUGIN_BASE_GET_PRIVATE (plugin)->name;
}

static gboolean
get_sort_last (const MMPlugin *plugin)
{
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (plugin);

    /* If we have any post-probing filter, we need to sort the plugin last */
    return (priv->vendor_strings || priv->product_strings);
}

static char *
get_driver_name (GUdevDevice *device)
{
    GUdevDevice *parent = NULL;
    const char *driver, *subsys;
    char *ret = NULL;

    driver = g_udev_device_get_driver (device);
    if (!driver) {
        parent = g_udev_device_get_parent (device);
        if (parent)
            driver = g_udev_device_get_driver (parent);

        /* Check for bluetooth; it's driver is a bunch of levels up so we
         * just check for the subsystem of the parent being bluetooth.
         */
        if (!driver && parent) {
            subsys = g_udev_device_get_subsystem (parent);
            if (subsys && !strcmp (subsys, "bluetooth"))
                driver = "bluetooth";
        }
    }

    if (driver)
        ret = g_strdup (driver);
    if (parent)
        g_object_unref (parent);

    return ret;
}

static gboolean
device_file_exists (const char *name)
{
    char *devfile;
    struct stat s;
    int result;

    devfile = g_strdup_printf ("/dev/%s", name);
    result = stat (devfile, &s);
    g_free (devfile);

    return (0 == result) ? TRUE : FALSE;
}

static gboolean
is_virtual_port (const gchar *device_name)
{
    guint idx;

    /* Detect any modems accessible through the list of virtual ports */
    for (idx = 0; virtual_port[idx]; idx++)  {
        if (strcmp (device_name, virtual_port[idx]))
            continue;
        if (!device_file_exists (virtual_port[idx]))
            continue;

        return TRUE;
    }

    return FALSE;
}

/* Returns TRUE if the support check request was filtered out */
static gboolean
apply_pre_probing_filters (MMPluginBase *self,
                           GUdevDevice *port,
                           const gchar *subsys,
                           const gchar *name,
                           const gchar *driver,
                           gboolean *need_vendor_probing,
                           gboolean *need_product_probing)
{
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (self);
    guint16 vendor = 0;
    guint16 product = 0;
    gboolean product_filtered = FALSE;
    gboolean vendor_filtered = FALSE;
    guint i;

    *need_vendor_probing = FALSE;
    *need_product_probing = FALSE;

    /* The plugin may specify that only some subsystems are supported. If that
     * is the case, filter by subsystem */
    if (priv->subsystems) {
        for (i = 0; priv->subsystems[i]; i++) {
            if (g_str_equal (subsys, priv->subsystems[i]))
                break;
        }

        /* If we didn't match any subsystem: unsupported */
        if (!priv->subsystems[i])
            return TRUE;
    }

    /* The plugin may specify that only some drivers are supported. If that
     * is the case, filter by driver */
    if (priv->drivers) {
        for (i = 0; priv->drivers[i]; i++) {
            if (g_str_equal (driver, priv->drivers[i]))
                break;
        }

        /* If we didn't match any driver: unsupported */
        if (!priv->drivers[i])
            return TRUE;
    }

    mm_plugin_base_get_device_ids (self, subsys, name, &vendor, &product);

    /* The plugin may specify that only some vendor IDs are supported. If that
     * is the case, filter by vendor ID. */
    if (priv->vendor_ids) {
        /* If we didn't get any vendor: filtered */
        if (!vendor)
            vendor_filtered = TRUE;
        else {
            for (i = 0; priv->vendor_ids[i]; i++)
                if (vendor == priv->vendor_ids[i])
                    break;

            /* If we didn't match any vendor: filtered */
            if (!priv->vendor_ids[i])
                vendor_filtered = TRUE;
        }
    }

    /* The plugin may specify that only some product IDs are supported. If
     * that is the case, filter by vendor+product ID pair */
    if (priv->product_ids) {
        /* If we didn't get any product: filtered */
        if (!product)
            product_filtered = TRUE;
        else {
            for (i = 0; priv->product_ids[i].l; i++)
                if (vendor == priv->product_ids[i].l &&
                    product == priv->product_ids[i].r)
                    break;

            /* If we didn't match any product: filtered */
            if (!priv->product_ids[i].l)
                product_filtered = TRUE;
        }
    }

    /* If we got filtered by vendor or product IDs  and we do not have vendor
     * or product strings to compare with: unsupported */
    if ((vendor_filtered || product_filtered) &&
        !priv->vendor_strings &&
        !priv->product_strings)
        return TRUE;

    /* If we need to filter by vendor/product strings, need to probe for both.
     * This covers the case where a RS232 modem is connected via a USB<->RS232
     * adaptor, and we get in udev the vendor ID of the adaptor */

    if (priv->product_strings) {
        *need_vendor_probing = TRUE;
        *need_product_probing = TRUE;
    } else if (priv->vendor_strings)
        *need_vendor_probing = TRUE;

    /* The plugin may specify that only ports with some given udev tags are
     * supported. If that is the case, filter by udev tag */
    if (priv->udev_tags) {
        for (i = 0; priv->udev_tags[i]; i++) {
            /* Check if the port was tagged */
            if (g_udev_device_get_property_as_boolean (port,
                                                       priv->udev_tags[i]))
                break;
        }

        /* If we didn't match any udev tag: unsupported */
        if (!priv->udev_tags[i])
            return TRUE;
    }

    return FALSE;
}

/* Returns TRUE if the support check request was filtered out */
static gboolean
apply_post_probing_filters (MMPluginBase *self,
                            MMPortProbe *probe)
{
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (self);
    gboolean vendor_filtered = FALSE;
    gboolean product_filtered = FALSE;
    guint i;

    /* The plugin may specify that only some vendor strings are supported. If
     * that is the case, filter by vendor string. */
    if (priv->vendor_strings) {
        const gchar *vendor;

        vendor = mm_port_probe_get_vendor (probe);

        /* If we didn't get any vendor: filtered */
        if (!vendor)
            vendor_filtered = TRUE;
        else {
            for (i = 0; priv->vendor_strings[i]; i++) {
                gboolean found;
                gchar *casefolded;

                casefolded = g_utf8_casefold (priv->vendor_strings[i], -1);
                found = !!strstr (vendor, casefolded);
                g_free (casefolded);
                if (found)
                    break;
            }

            /* If we didn't match any vendor: filtered */
            if (!priv->vendor_strings[i])
                vendor_filtered = TRUE;
        }

        if (vendor_filtered) {
            if (!priv->product_strings)
                return TRUE;
        } else
            /* Vendor matched */
            return FALSE;
    }

    /* The plugin may specify that only some vendor+product string pairs are
     * supported. If that is the case, filter by product string */
    if (priv->product_strings) {
        const gchar *vendor;
        const gchar *product;

        vendor = mm_port_probe_get_vendor (probe);
        product = mm_port_probe_get_product (probe);

        /* If we didn't get any vendor or product: filtered */
        if (!vendor || !product)
            product_filtered = TRUE;
        else {
            for (i = 0; priv->product_strings[i].l; i++) {
                gboolean found;
                gchar *casefolded_vendor;
                gchar *casefolded_product;

                casefolded_vendor = g_utf8_casefold (priv->product_strings[i].l, -1);
                casefolded_product = g_utf8_casefold (priv->product_strings[i].r, -1);
                found = (!!strstr (vendor, casefolded_vendor) &&
                         !!strstr (product, casefolded_product));
                g_free (casefolded_vendor);
                g_free (casefolded_product);
                if (found)
                    break;
            }

            /* If we didn't match any product: unsupported */
            if (!priv->product_strings[i].l)
                product_filtered = TRUE;
        }

        return product_filtered;
    }

    return FALSE;
}

/* Context for the asynchronous probing operation */
typedef struct {
    GSimpleAsyncResult *result;
    MMPluginBase *plugin;
    MMPortProbeFlag flags;
} PortProbeRunContext;

static void
cancel_at_probing_in_other_probes (const gchar *key,
                                   MMPortProbe *other,
                                   MMPortProbe *self)
{
    if (self != other &&
        g_str_equal (mm_port_probe_get_port_physdev (self),
                     mm_port_probe_get_port_physdev (other)))
        mm_port_probe_run_cancel_at_probing (other);
}

static void
port_probe_run_ready (MMPortProbe *probe,
                      GAsyncResult *probe_result,
                      PortProbeRunContext *ctx)
{
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (ctx->plugin);
    GError *error = NULL;
    gboolean keep_probe = FALSE;

    if (!mm_port_probe_run_finish (probe, probe_result, &error)) {
        /* Probing failed */
        g_simple_async_result_take_error (ctx->result, error);
    } else {
        /* Probing succeeded */
        MMPluginSupportsResult supports_result;

        if (!apply_post_probing_filters (ctx->plugin, probe)) {
            /* Port is supported! Leave it in the internal HT until port gets
             * grabbed. */
            supports_result = MM_PLUGIN_SUPPORTS_PORT_SUPPORTED;
            keep_probe = TRUE;

            /* If we were looking for AT ports, and the port is AT,
             * and we were told that only one AT port is expected, cancel AT
             * probings in the other available support tasks of the SAME
             * device. */
            if (priv->single_at &&
                ctx->flags & MM_PORT_PROBE_AT &&
                mm_port_probe_is_at (probe)) {
                g_hash_table_foreach (priv->tasks,
                                      (GHFunc) cancel_at_probing_in_other_probes,
                                      probe);
            }

        } else {
            /* Unsupported port, remove from internal tracking HT */
            supports_result = MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;
        }

        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   GUINT_TO_POINTER (supports_result),
                                                   NULL);
    }

    /* Complete the async supports port request */
    g_simple_async_result_complete_in_idle (ctx->result);

    /* If no longer needed, Remove probe from internal HT */
    if (!keep_probe) {
        gchar *key;

        key = get_key (mm_port_probe_get_port_subsys (probe),
                       mm_port_probe_get_port_name (probe));
        g_hash_table_remove (priv->tasks, key);
        g_free (key);
    }

    g_object_unref (ctx->result);
    g_object_unref (ctx->plugin);
    g_free (ctx);
}

static MMPluginSupportsResult
supports_port_finish (MMPlugin *self,
                      GAsyncResult *result,
                      GError **error)
{
    g_return_val_if_fail (MM_IS_PLUGIN (self),
                          MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result),
                          MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED);

    /* Propagate error, if any */
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                               error)) {
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;
    }

    return (MMPluginSupportsResult) GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result)));
}

static void
supports_port (MMPlugin *plugin,
               const gchar *subsys,
               const gchar *name,
               const gchar *physdev_path,
               MMBaseModem *existing,
               GAsyncReadyCallback callback,
               gpointer user_data)
{
    MMPluginBase *self = MM_PLUGIN_BASE (plugin);
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (self);
    GUdevDevice *port = NULL;
    gchar *driver = NULL;
    gchar *key = NULL;
    MMPortProbe *probe;
    GSimpleAsyncResult *async_result;
    PortProbeRunContext *ctx;
    gboolean need_vendor_probing;
    gboolean need_product_probing;
    MMPortProbeFlag probe_run_flags;

    /* Setup key */
    key = get_key (subsys, name);

    async_result = g_simple_async_result_new (G_OBJECT (self),
                                              callback,
                                              user_data,
                                              supports_port);

    /* Get port device */
    if (!(port = g_udev_client_query_by_subsystem_and_name (priv->client,
                                                            subsys,
                                                            name))) {
        g_simple_async_result_set_error (async_result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't find port for (%s/%s)",
                                         subsys,
                                         name);
        g_simple_async_result_complete_in_idle (async_result);
        goto out;
    }

    /* Detect any modems accessible through the list of virtual ports */
    if (!(driver = (is_virtual_port (name) ?
                    g_strdup ("virtual") :
                    get_driver_name (port)))) {
        g_simple_async_result_set_error (async_result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't find driver for (%s/%s)",
                                         subsys,
                                         name);
        g_simple_async_result_complete_in_idle (async_result);
        goto out;
    }

    /* Apply filters before launching the probing */
    if (apply_pre_probing_filters (self,
                                   port,
                                   subsys,
                                   name,
                                   driver,
                                   &need_vendor_probing,
                                   &need_product_probing)) {
        /* Filtered! */
        g_simple_async_result_set_op_res_gpointer (async_result,
                                                   GUINT_TO_POINTER (MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED),
                                                   NULL);
        g_simple_async_result_complete_in_idle (async_result);
        goto out;
    }

    mm_dbg ("(%s) checking port support (%s,%s)", priv->name, subsys, name);

    /* Need to launch new probing */

    /* Lookup current probes, there shouldn't be any (unless for net devices) */
    probe = g_hash_table_lookup (priv->tasks, key);
    if (!probe)
        probe = mm_port_probe_cache_get (port, physdev_path, driver);
    g_assert (probe);

    /* Before launching any probing, check if the port is a net device (which
     * cannot be probed).
     * TODO: With the new defer-until-suggested we probably don't need the modem
     * object being passed down here just for this. */
    if (g_str_equal (subsys, "net")) {
        /* Keep track of the probe object, which is considered finished */
        if (!g_hash_table_lookup (priv->tasks, key))
            g_hash_table_insert (priv->tasks,
                                 g_strdup (key),
                                 g_object_ref (probe));

        /* If we already have a existing modem, then mark it as supported.
         * Otherwise, just defer a bit */
        g_simple_async_result_set_op_res_gpointer (
            async_result,
            GUINT_TO_POINTER ((existing ?
                               MM_PLUGIN_SUPPORTS_PORT_SUPPORTED :
                               MM_PLUGIN_SUPPORTS_PORT_DEFER_UNTIL_SUGGESTED)),
            NULL);
        g_simple_async_result_complete_in_idle (async_result);
        goto out;
    }

    /* Build flags depending on what probing needed */
    probe_run_flags = MM_PORT_PROBE_NONE;
    if (priv->at)
        probe_run_flags |= MM_PORT_PROBE_AT;
    else if (priv->single_at)
        probe_run_flags |= MM_PORT_PROBE_AT;
    if (need_vendor_probing)
        probe_run_flags |= (MM_PORT_PROBE_AT | MM_PORT_PROBE_AT_VENDOR);
    if (need_product_probing)
        probe_run_flags |= (MM_PORT_PROBE_AT | MM_PORT_PROBE_AT_PRODUCT);
    if (priv->qcdm)
        probe_run_flags |= MM_PORT_PROBE_QCDM;

    g_assert (probe_run_flags != MM_PORT_PROBE_NONE);

    /* If a modem is already available and the plugin says that only one AT port is
     * expected, check if we alredy got the single AT port. And if so, we know this
     * port being probed won't be AT. */
    if (priv->single_at &&
        existing &&
        mm_base_modem_has_at_port (existing)) {
        mm_dbg ("(%s)   not setting up AT probing tasks for (%s,%s): "
                "modem already has the expected single AT port",
                priv->name, subsys, name);

        /* Assuming it won't be an AT port. We still run the probe anyway, in
         * case we need to check for other port types (e.g. QCDM) */
        mm_port_probe_set_result_at (probe, FALSE);
    }

    /* Setup async call context */
    ctx = g_new (PortProbeRunContext, 1);
    ctx->plugin = g_object_ref (self);
    ctx->result = g_object_ref (async_result);
    ctx->flags = probe_run_flags;

    /* Launch the probe */
    mm_dbg ("(%s)   launching probe for (%s,%s)", priv->name, subsys, name);
    mm_port_probe_run (probe,
                       ctx->flags,
                       priv->send_delay,
                       priv->custom_init,
                       (GAsyncReadyCallback)port_probe_run_ready,
                       ctx);

    /* Keep track of the probe. Note that we got a new reference already
     * from the cache. */
    g_hash_table_insert (priv->tasks,
                         g_strdup (key),
                         probe);

out:
    if (port)
        g_object_unref (port);
    g_free (key);
    g_free (driver);
    g_object_unref (async_result);
}

static void
supports_port_cancel (MMPlugin *plugin,
                      const char *subsys,
                      const char *name)
{
    MMPluginBase *self = MM_PLUGIN_BASE (plugin);
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (self);
    MMPortProbe *probe;
    gchar *key;

    key = get_key (subsys, name);
    probe = g_hash_table_lookup (priv->tasks, key);
    if (probe) {
        mm_port_probe_run_cancel (probe);
        g_hash_table_remove (priv->tasks, key);
    }

    g_free (key);
}

static MMBaseModem *
grab_port (MMPlugin *plugin,
           const char *subsys,
           const char *name,
           MMBaseModem *existing,
           GError **error)
{
    MMPluginBase *self = MM_PLUGIN_BASE (plugin);
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (self);
    MMPortProbe *probe;
    MMBaseModem *modem = NULL;
    char *key;

    key = get_key (subsys, name);
    probe = g_hash_table_lookup (priv->tasks, key);
    g_assert (probe);

    /* Let the modem grab the port */
    modem = MM_PLUGIN_BASE_GET_CLASS (self)->grab_port (self,
                                                        existing,
                                                        probe,
                                                        error);

    g_hash_table_remove (priv->tasks, key);
    g_free (key);
    return modem;
}

/*****************************************************************************/

static void
plugin_init (MMPlugin *plugin_class)
{
    /* interface implementation */
    plugin_class->get_name = get_name;
    plugin_class->get_sort_last = get_sort_last;
    plugin_class->supports_port = supports_port;
    plugin_class->supports_port_finish = supports_port_finish;
    plugin_class->supports_port_cancel = supports_port_cancel;
    plugin_class->grab_port = grab_port;
}

static void
mm_plugin_base_init (MMPluginBase *self)
{
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (self);

    /* We pass NULL as we won't need to get notified about uevents,
     * we just use this client for sync queries. */
    priv->client = g_udev_client_new (NULL);

    priv->tasks = g_hash_table_new_full (g_str_hash,
                                         g_str_equal,
                                         g_free,
                                         (GDestroyNotify) g_object_unref);
    /* Defaults */
    priv->send_delay = 100000;
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (object);

    switch (prop_id) {
    case PROP_NAME:
        /* Construct only */
        priv->name = g_value_dup_string (value);
        break;
    case PROP_ALLOWED_SUBSYSTEMS:
        /* Construct only */
        priv->subsystems = g_value_dup_boxed (value);
        break;
    case PROP_ALLOWED_DRIVERS:
        /* Construct only */
        priv->drivers = g_value_dup_boxed (value);
        break;
    case PROP_ALLOWED_VENDOR_IDS:
        /* Construct only */
        priv->vendor_ids = g_value_dup_boxed (value);
        break;
    case PROP_ALLOWED_PRODUCT_IDS:
        /* Construct only */
        priv->product_ids = g_value_dup_boxed (value);
        break;
    case PROP_ALLOWED_VENDOR_STRINGS:
        /* Construct only */
        priv->vendor_strings = g_value_dup_boxed (value);
        break;
    case PROP_ALLOWED_PRODUCT_STRINGS:
        /* Construct only */
        priv->product_strings = g_value_dup_boxed (value);
        break;
    case PROP_ALLOWED_UDEV_TAGS:
        /* Construct only */
        priv->udev_tags = g_value_dup_boxed (value);
        break;
    case PROP_ALLOWED_AT:
        /* Construct only */
        priv->at = g_value_get_boolean (value);
        break;
    case PROP_ALLOWED_SINGLE_AT:
        /* Construct only */
        priv->single_at = g_value_get_boolean (value);
        break;
    case PROP_ALLOWED_QCDM:
        /* Construct only */
        priv->qcdm = g_value_get_boolean (value);
        break;
    case PROP_CUSTOM_INIT:
        /* Construct only */
        priv->custom_init = g_value_dup_boxed (value);
        break;
    case PROP_SEND_DELAY:
        /* Construct only */
        priv->send_delay = (guint64)g_value_get_uint64 (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (object);

    switch (prop_id) {
    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
    case PROP_ALLOWED_SUBSYSTEMS:
        g_value_set_boxed (value, priv->subsystems);
        break;
    case PROP_ALLOWED_DRIVERS:
        g_value_set_boxed (value, priv->drivers);
        break;
    case PROP_ALLOWED_VENDOR_IDS:
        g_value_set_boxed (value, priv->vendor_ids);
        break;
    case PROP_ALLOWED_PRODUCT_IDS:
        g_value_set_boxed (value, priv->product_ids);
        break;
    case PROP_ALLOWED_VENDOR_STRINGS:
        g_value_set_boxed (value, priv->vendor_strings);
        break;
    case PROP_ALLOWED_PRODUCT_STRINGS:
        g_value_set_boxed (value, priv->product_strings);
        break;
    case PROP_ALLOWED_AT:
        g_value_set_boolean (value, priv->at);
        break;
    case PROP_ALLOWED_SINGLE_AT:
        g_value_set_boolean (value, priv->single_at);
        break;
    case PROP_ALLOWED_QCDM:
        g_value_set_boolean (value, priv->qcdm);
        break;
    case PROP_ALLOWED_UDEV_TAGS:
        g_value_set_boxed (value, priv->udev_tags);
        break;
    case PROP_CUSTOM_INIT:
        g_value_set_boxed (value, priv->custom_init);
        break;
    case PROP_SEND_DELAY:
        g_value_set_uint64 (value, priv->send_delay);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
finalize (GObject *object)
{
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (object);

    g_free (priv->name);

    g_object_unref (priv->client);

    g_hash_table_destroy (priv->tasks);

    G_OBJECT_CLASS (mm_plugin_base_parent_class)->finalize (object);
}

static void
mm_plugin_base_class_init (MMPluginBaseClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPluginBasePrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;

    g_object_class_install_property
        (object_class, PROP_NAME,
         g_param_spec_string (MM_PLUGIN_BASE_NAME,
                              "Name",
                              "Name",
                              NULL,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_SUBSYSTEMS,
         g_param_spec_boxed (MM_PLUGIN_BASE_ALLOWED_SUBSYSTEMS,
                             "Allowed subsystems",
                             "List of subsystems this plugin can support, "
                             "should be an array of strings finished with 'NULL'",
                             G_TYPE_STRV,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_DRIVERS,
         g_param_spec_boxed (MM_PLUGIN_BASE_ALLOWED_DRIVERS,
                             "Allowed drivers",
                             "List of drivers this plugin can support, "
                             "should be an array of strings finished with 'NULL'",
                             G_TYPE_STRV,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_VENDOR_IDS,
         g_param_spec_boxed (MM_PLUGIN_BASE_ALLOWED_VENDOR_IDS,
                             "Allowed vendor IDs",
                             "List of vendor IDs this plugin can support, "
                             "should be an array of guint16 finished with '0'",
                             MM_TYPE_UINT16_ARRAY,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_PRODUCT_IDS,
         g_param_spec_boxed (MM_PLUGIN_BASE_ALLOWED_PRODUCT_IDS,
                             "Allowed product IDs",
                             "List of vendor+product ID pairs this plugin can support, "
                             "should be an array of mm_uint16_pair finished with '0,0'",
                             MM_TYPE_UINT16_PAIR_ARRAY,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));


    g_object_class_install_property
        (object_class, PROP_ALLOWED_VENDOR_STRINGS,
         g_param_spec_boxed (MM_PLUGIN_BASE_ALLOWED_VENDOR_STRINGS,
                             "Allowed vendor strings",
                             "List of vendor strings this plugin can support, "
                             "should be an array of strings finished with 'NULL'",
                             G_TYPE_STRV,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_PRODUCT_STRINGS,
         g_param_spec_boxed (MM_PLUGIN_BASE_ALLOWED_PRODUCT_STRINGS,
                             "Allowed product strings",
                             "List of vendor+product string pairs this plugin can support, "
                             "should be an array of mm_str_pair finished with 'NULL,NULL'",
                             MM_TYPE_STR_PAIR_ARRAY,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_UDEV_TAGS,
         g_param_spec_boxed (MM_PLUGIN_BASE_ALLOWED_UDEV_TAGS,
                             "Allowed Udev tags",
                             "List of udev tags this plugin may expect, "
                             "should be an array of strings finished with 'NULL'",
                             G_TYPE_STRV,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_AT,
         g_param_spec_boolean (MM_PLUGIN_BASE_ALLOWED_AT,
                               "Allowed AT",
                               "Whether AT ports are allowed in this plugin",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_SINGLE_AT,
         g_param_spec_boolean (MM_PLUGIN_BASE_ALLOWED_SINGLE_AT,
                               "Allowed single AT",
                               "Whether just a single AT port is allowed in this plugin. ",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_QCDM,
         g_param_spec_boolean (MM_PLUGIN_BASE_ALLOWED_QCDM,
                               "Allowed QCDM",
                               "Whether QCDM ports are allowed in this plugin",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_CUSTOM_INIT,
         g_param_spec_boxed (MM_PLUGIN_BASE_CUSTOM_INIT,
                             "Custom initialization",
                             "List of custom initializations this plugin needs, "
                             "should be an array of MMPortProbeAtCommand structs "
                             "finished with 'NULL'",
                             MM_TYPE_POINTER_ARRAY,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_SEND_DELAY,
         g_param_spec_uint64 (MM_PLUGIN_BASE_SEND_DELAY,
                              "Send delay",
                              "Send delay for characters in the AT port, "
                              "in microseconds",
                              0, G_MAXUINT64, 100000,
                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}
