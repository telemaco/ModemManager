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
 * Copyright (C) 2011 Samsung Electronics, Inc.,
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <errno.h>
#include <arpa/inet.h>
#include <dbus/dbus-glib.h>

#include "mm-modem-samsung-gsm.h"
#include "mm-modem-simple.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-modem-gsm-card.h"
#include "mm-log.h"
#include "mm-modem-icera.h"
#include "mm-utils.h"
#include "mm-modem-time.h"

static void modem_init (MMModem *modem_class);
static void modem_gsm_network_init (MMModemGsmNetwork *gsm_network_class);
static void modem_simple_init (MMModemSimple *class);
static void modem_gsm_card_init (MMModemGsmCard *class);
static void modem_icera_init (MMModemIcera *icera_class);
static void modem_time_init (MMModemTime *class);

G_DEFINE_TYPE_EXTENDED (MMModemSamsungGsm, mm_modem_samsung_gsm, MM_TYPE_GENERIC_GSM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_SIMPLE, modem_simple_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_ICERA, modem_icera_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_GSM_NETWORK, modem_gsm_network_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_GSM_CARD, modem_gsm_card_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_TIME, modem_time_init))

#define MM_MODEM_SAMSUNG_GSM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_SAMSUNG_GSM, MMModemSamsungGsmPrivate))

typedef struct {
    gboolean disposed;

    MMModemIceraPrivate *icera;
    char *band;
} MMModemSamsungGsmPrivate;

#define IPDPADDR_TAG "%IPDPADDR: "


MMModem *
mm_modem_samsung_gsm_new (const char *device,
                         const char *driver,
                         const char *plugin)
{
    MMModem *modem;

    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    modem = MM_MODEM (g_object_new (MM_TYPE_MODEM_SAMSUNG_GSM,
                                    MM_MODEM_MASTER_DEVICE, device,
                                    MM_MODEM_DRIVER, driver,
                                    MM_MODEM_PLUGIN, plugin,
                                    MM_MODEM_IP_METHOD, MM_MODEM_IP_METHOD_DHCP,
                                    NULL));
    if (modem)
        MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (modem)->icera = mm_modem_icera_init_private ();

    return modem;
}

typedef struct {
    MMModemGsmBand mm;
    char band[50];
} BandTable;

static BandTable bands[12] = {
    /* Sort 3G first since it's preferred */
    { MM_MODEM_GSM_BAND_U2100, "FDD_BAND_I" },
    { MM_MODEM_GSM_BAND_U1900, "FDD_BAND_II" },
    { MM_MODEM_GSM_BAND_U1800, "FDD_BAND_III" },
    { MM_MODEM_GSM_BAND_U17IV, "FDD_BAND_IV" },
    { MM_MODEM_GSM_BAND_U850,  "FDD_BAND_V" },
    { MM_MODEM_GSM_BAND_U800,  "FDD_BAND_VI" },
    { MM_MODEM_GSM_BAND_U900,  "FDD_BAND_VIII" },
    { MM_MODEM_GSM_BAND_G850, "G850" },
    /* 2G second */
    { MM_MODEM_GSM_BAND_DCS,   "DCS" },
    { MM_MODEM_GSM_BAND_EGSM,  "EGSM" }, /* 0x100 = Extended GSM, 0x200 = Primary GSM */
    { MM_MODEM_GSM_BAND_PCS,   "PCS" },
    /* And ANY last since it's most inclusive */
    { MM_MODEM_GSM_BAND_ANY,   "ANY" },
};

static gboolean
band_mm_to_samsung (MMModemGsmBand band, MMModemGsmNetwork *modem)
{
    int i;
    MMModemSamsungGsmPrivate *priv = MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (modem);

    for (i = 0; i < sizeof (bands) / sizeof (BandTable); i++) {
        if (bands[i].mm == band) {
            priv->band = bands[i].band;
            return TRUE;
        }
    }
    return FALSE;
}

static void
get_allowed_mode (MMGenericGsm *gsm,
                  MMModemUIntFn callback,
                  gpointer user_data)
{
    mm_modem_icera_get_allowed_mode (MM_MODEM_ICERA (gsm), callback, user_data);
}

static void
set_allowed_mode (MMGenericGsm *gsm,
                  MMModemGsmAllowedMode mode,
                  MMModemFn callback,
                  gpointer user_data)
{
    mm_modem_icera_set_allowed_mode (MM_MODEM_ICERA (gsm), mode, callback, user_data);
}

static void
set_band_done (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error)
        info->error = g_error_copy (error);

    mm_callback_info_schedule (info);
}

static void
set_band (MMModemGsmNetwork *modem,
          MMModemGsmBand band,
          MMModemFn callback,
          gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;
    char *command;
    MMModemSamsungGsmPrivate *priv = MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (modem);

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    /* TODO: Check how to pass more than one band in the same AT%%IPBM command */
    if (!utils_check_for_single_value (band)) {
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "Cannot set more than one band.");
        mm_callback_info_schedule (info);
    } else if (!band_mm_to_samsung (band, modem)) {
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "Invalid band.");
        mm_callback_info_schedule (info);
    } else {
        mm_callback_info_set_data (info, "band", g_strdup(priv->band), NULL);
        command = g_strdup_printf ("AT%%IPBM=\"%s\",1", priv->band);
        mm_at_serial_port_queue_command (port, command, 3, set_band_done, info);
        g_free (command);
        priv->band = NULL;
    }
}

static gboolean
parse_ipbm (const char *reply, MMModemGsmBand *band)
{
    int enable[12];

    g_return_val_if_fail (band != NULL, FALSE);
    g_return_val_if_fail (reply != NULL, FALSE);

    if (sscanf (reply, "\"ANY\": %d\r\n\"EGSM\": %d\r\n\"DCS\": %d\r\n\"PCS\": %d\r\n\"G850\": %d\r\n\"FDD_BAND_I\": %d\r\n\"FDD_BAND_II\": %d\r\n\"FDD_BAND_III\": %d\r\n\"FDD_BAND_IV\": %d\r\n\"FDD_BAND_V\": %d\r\n\"FDD_BAND_VI\": %d\r\n\"FDD_BAND_VIII\": %d", &enable[0], &enable[1], &enable[2], &enable[3], &enable[4], &enable[5], &enable[6], &enable[7], &enable[8], &enable[9], &enable[10], &enable[11]) != 12)
        return FALSE;

    *band = 0;
    if (enable[5] == 1)
        *band |= MM_MODEM_GSM_BAND_U2100;
    if (enable[6] == 1)
        *band |= MM_MODEM_GSM_BAND_U1900;
    if (enable[7] == 1)
        *band |= MM_MODEM_GSM_BAND_U1800;
    if (enable[8] == 1)
        *band |= MM_MODEM_GSM_BAND_U17IV;
    if (enable[9] == 1)
        *band |= MM_MODEM_GSM_BAND_U850;
    if (enable[10] == 1)
        *band |= MM_MODEM_GSM_BAND_U800;
    if (enable[11] == 1)
        *band |= MM_MODEM_GSM_BAND_U900;
    if (enable[1] == 1)
        *band |= MM_MODEM_GSM_BAND_EGSM;
    if (enable[2] == 1)
        *band |= MM_MODEM_GSM_BAND_DCS;
    if (enable[3] == 1)
        *band |= MM_MODEM_GSM_BAND_PCS;
    if (enable[4] == 1)
        *band |= MM_MODEM_GSM_BAND_G850;

    return (*band > 0 ? TRUE : FALSE);
}

static void
get_band_done (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemGsmBand mm_band = MM_MODEM_GSM_BAND_UNKNOWN;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error)
        info->error = g_error_copy (error);
    else if (parse_ipbm (response->str, &mm_band))
        mm_callback_info_set_result (info, GUINT_TO_POINTER (mm_band), NULL);

    mm_callback_info_schedule (info);
}

static void
get_band (MMModemGsmNetwork *modem,
          MMModemUIntFn callback,
          gpointer user_data)
{
    MMAtSerialPort *port;
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);

    /* Otherwise ask the modem */
    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "AT%IPBM?", 3, get_band_done, info);
}

static void
send_samsung_pinnum_done (MMAtSerialPort *port,
                          GString *response,
                          GError *error,
                          gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    int matched;
    GArray *retry_counts;
    PinRetryCount ur[4] = {
        {"sim-pin", 0}, {"sim-puk", 0}, {"sim-pin2", 0}, {"sim-puk2", 0}
    };

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        info->error = g_error_copy (error);
        goto done;
    }

    matched = sscanf (response->str, "%%PINNUM: %d, %d, %d, %d",
                      &ur[0].count, &ur[1].count, &ur[2].count, &ur[3].count);
    if (matched == 4) {
        if (ur[0].count > 998) {
            info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                       "Invalid PIN attempts left %d", ur[0].count);
            ur[0].count = 0;
        }

        retry_counts = g_array_sized_new (FALSE, TRUE, sizeof (PinRetryCount), 4);
        g_array_append_vals (retry_counts, &ur, 4);
        mm_callback_info_set_result (info, retry_counts, NULL);
    } else {
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                           "Could not parse PIN retries results");
    }

done:
    mm_serial_port_close (MM_SERIAL_PORT (port));
    mm_callback_info_schedule (info);
}

static void
reset (MMModem *modem,
       MMModemFn callback,
       gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (port)
        mm_at_serial_port_queue_command (port, "%IRESET", 3, NULL, NULL);

    mm_callback_info_schedule (info);
}

static void
get_unlock_retries (MMModemGsmCard *modem,
                    MMModemArrayFn callback,
                    gpointer user_data)
{
    MMAtSerialPort *port;
    MMCallbackInfo *info = mm_callback_info_array_new (MM_MODEM (modem), callback, user_data);

    mm_dbg ("get_unlock_retries");

    /* Ensure we have a usable port to use for the command */
    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    /* Modem may not be enabled yet, which sometimes can't be done until
     * the device has been unlocked.  In this case we have to open the port
     * ourselves.
     */
    if (!mm_serial_port_open (MM_SERIAL_PORT (port), &info->error)) {
        mm_callback_info_schedule (info);
        return;
    }

    /* if the modem have not yet been enabled we need to make sure echoing is turned off */
    mm_at_serial_port_queue_command (port, "E0", 3, NULL, NULL);
    mm_at_serial_port_queue_command (port, "%PINNUM?", 3, send_samsung_pinnum_done, info);

}

static void
get_access_technology (MMGenericGsm *gsm,
                       MMModemUIntFn callback,
                       gpointer user_data)
{
    mm_modem_icera_get_access_technology (MM_MODEM_ICERA (gsm), callback, user_data);
}

/*****************************************************************************/

static void
disable_unsolicited_done (MMAtSerialPort *port,
                          GString *response,
                          GError *error,
                          gpointer user_data)

{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    /* Ignore all errors */
    mm_callback_info_schedule (info);
}

static void
invoke_call_parent_disable_fn (MMCallbackInfo *info)
{
    /* Note: we won't call the parent disable if info->modem is no longer
     * valid. The invoke is called always once the info gets scheduled, which
     * may happen during removed modem detection. */
    if (info->modem) {
        MMModem *parent_modem_iface;

        parent_modem_iface = g_type_interface_peek_parent (MM_MODEM_GET_INTERFACE (info->modem));
        parent_modem_iface->disable (info->modem, (MMModemFn)info->callback, info->user_data);
    }
}

static void
disable (MMModem *modem,
         MMModemFn callback,
         gpointer user_data)
{
    MMAtSerialPort *primary;
    MMCallbackInfo *info;

    mm_modem_icera_cleanup (MM_MODEM_ICERA (modem));
    mm_modem_icera_change_unsolicited_messages (MM_MODEM_ICERA (modem), FALSE);

    info = mm_callback_info_new_full (modem,
                                      invoke_call_parent_disable_fn,
                                      (GCallback)callback,
                                      user_data);

    primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (modem), MM_AT_PORT_FLAG_PRIMARY);
    g_assert (primary);

    /*
     * Command to ensure unsolicited message disable completes.
     * Turns the radios off, which seems like a reasonable
     * think to do when disabling.
     */
    mm_at_serial_port_queue_command (primary, "AT+CFUN=4", 5, disable_unsolicited_done, info);
}

static void
init_all_done (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemSamsungGsm *self;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    self = MM_MODEM_SAMSUNG_GSM (info->modem);

    if (!error)
        mm_modem_icera_change_unsolicited_messages (MM_MODEM_ICERA (self), TRUE);

    mm_generic_gsm_enable_complete (MM_GENERIC_GSM (self), error, info);
}

static void
init2_done (MMAtSerialPort *port,
            GString *response,
            GError *error,
            gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemSamsungGsm *self;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    self = MM_MODEM_SAMSUNG_GSM (info->modem);

    if (error)
        mm_generic_gsm_enable_complete (MM_GENERIC_GSM (self), error, info);
    else {
        /* Finish the initialization */
        mm_at_serial_port_queue_command (port, "E0 V1 X4 &C1", 3, init_all_done, info);
    }
}

static void
init_done (MMAtSerialPort *port,
           GString *response,
           GError *error,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemSamsungGsm *self;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    self = MM_MODEM_SAMSUNG_GSM (info->modem);

    if (error)
        mm_generic_gsm_enable_complete (MM_GENERIC_GSM (self), error, info);
    else {
        /* Power up the modem */
        mm_at_serial_port_queue_command (port, "+CMEE=1", 2, NULL, NULL);
        mm_at_serial_port_queue_command (port, "+CFUN=1", 10, init2_done, info);
    }
}

static void
init_reset_done (MMAtSerialPort *port,
           GString *response,
           GError *error,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemSamsungGsm *self = MM_MODEM_SAMSUNG_GSM (info->modem);

    if (error)
        mm_generic_gsm_enable_complete (MM_GENERIC_GSM (self), error, info);
    else
        mm_at_serial_port_queue_command (port, "E0 V1", 3, init_done, info);
}

static void
do_enable (MMGenericGsm *modem, MMModemFn callback, gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *primary;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    primary = mm_generic_gsm_get_at_port (modem, MM_AT_PORT_FLAG_PRIMARY);
    g_assert (primary);
    mm_at_serial_port_queue_command (primary, "Z", 3, init_reset_done, info);
}

static void
do_connect (MMModem *modem,
            const char *number,
            MMModemFn callback,
            gpointer user_data)
{
    mm_modem_icera_do_connect (MM_MODEM_ICERA (modem), number, callback, user_data);
}

static void
do_disconnect (MMGenericGsm *gsm,
               gint cid,
               MMModemFn callback,
               gpointer user_data)
{
    mm_modem_icera_do_disconnect (gsm, cid, callback, user_data);
}

static void
get_ip4_config (MMModem *modem,
                MMModemIp4Fn callback,
                gpointer user_data)
{
    mm_modem_icera_get_ip4_config (MM_MODEM_ICERA (modem), callback, user_data);
}

static void
simple_connect (MMModemSimple *simple,
                GHashTable *properties,
                MMModemFn callback,
                gpointer user_data)
{
    MMModemSimple *parent_iface;
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* Let icera cache user & password */
    mm_modem_icera_simple_connect (MM_MODEM_ICERA (simple), properties);

    parent_iface = g_type_interface_peek_parent (MM_MODEM_SIMPLE_GET_INTERFACE (simple));
    parent_iface->connect (MM_MODEM_SIMPLE (simple), properties, callback, info);
}

static void
port_grabbed (MMGenericGsm *gsm,
              MMPort *port,
              MMAtPortFlags pflags,
              gpointer user_data)
{
    if (MM_IS_AT_SERIAL_PORT (port)) {
        g_object_set (port,
                      MM_PORT_CARRIER_DETECT, FALSE,
                      MM_SERIAL_PORT_SEND_DELAY, (guint64) 0,
                      NULL);

        /* Add Icera-specific handlers */
        mm_modem_icera_register_unsolicted_handlers (MM_MODEM_ICERA (gsm), MM_AT_SERIAL_PORT (port));
    }
}

static void
poll_timezone_done (MMModemIcera *modem,
                    MMModemIceraTimestamp *timestamp,
                    GError *error,
                    gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    gint offset;

    if (error || !timestamp) {
        return;
    }

    mm_info ("setting timezone from local timestamp "
             "%02d/%02d/%02d %02d:%02d:%02d %+02d.",
            timestamp->year, timestamp->month, timestamp->day,
            timestamp->hour, timestamp->minute, timestamp->second,
            timestamp->tz_offset);

    // Offset is in 15-minute intervals, as provided by GSM network
    offset = 15 * timestamp->tz_offset;

    mm_modem_base_set_network_timezone (MM_MODEM_BASE (modem),
                                        &offset, NULL, NULL);

    mm_callback_info_schedule (info);
}

static gboolean
poll_timezone (MMModemTime *self, MMModemFn callback, gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);
    mm_modem_icera_get_local_timestamp (MM_MODEM_ICERA (self),
                                        poll_timezone_done,
                                        info);
    return TRUE;
}

static MMModemIceraPrivate *
get_icera_private (MMModemIcera *icera)
{
    return MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (icera)->icera;
}

static void
modem_init (MMModem *modem_class)
{
    modem_class->reset = reset;
    modem_class->disable = disable;
    modem_class->connect = do_connect;
    modem_class->get_ip4_config = get_ip4_config;
}

static void
modem_icera_init (MMModemIcera *icera)
{
    icera->get_private = get_icera_private;
}

static void
modem_simple_init (MMModemSimple *class)
{
    class->connect = simple_connect;
}

static void
modem_gsm_network_init (MMModemGsmNetwork *class)
{
    class->set_band = set_band;
    class->get_band = get_band;
}

static void
modem_gsm_card_init (MMModemGsmCard *class)
{
    class->get_unlock_retries = get_unlock_retries;
}

static void
modem_time_init (MMModemTime *class)
{
    class->poll_network_timezone = poll_timezone;
}

static void
mm_modem_samsung_gsm_init (MMModemSamsungGsm *self)
{
}

static void
dispose (GObject *object)
{
    MMModemSamsungGsm *self = MM_MODEM_SAMSUNG_GSM (object);
    MMModemSamsungGsmPrivate *priv = MM_MODEM_SAMSUNG_GSM_GET_PRIVATE (self);

    if (priv->disposed == FALSE) {
        priv->disposed = TRUE;

        mm_modem_icera_dispose_private (MM_MODEM_ICERA (self));
    }

    G_OBJECT_CLASS (mm_modem_samsung_gsm_parent_class)->dispose (object);
}

static void
mm_modem_samsung_gsm_class_init (MMModemSamsungGsmClass *klass)
{

    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    mm_modem_samsung_gsm_parent_class = g_type_class_peek_parent (klass);

    g_type_class_add_private (object_class, sizeof (MMModemSamsungGsmPrivate));

    object_class->dispose = dispose;

    gsm_class->port_grabbed = port_grabbed;
    gsm_class->do_disconnect = do_disconnect;
    gsm_class->do_enable = do_enable;
    gsm_class->set_allowed_mode = set_allowed_mode;
    gsm_class->get_allowed_mode = get_allowed_mode;
    gsm_class->get_access_technology = get_access_technology;
}
