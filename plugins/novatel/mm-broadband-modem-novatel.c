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
 * Copyright (C) 2012 Google Inc.
 * Author: Nathan Williams <njw@google.com>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-bearer-novatel.h"
#include "mm-broadband-modem-novatel.h"
#include "mm-errors-types.h"
#include "mm-iface-modem.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-serial-parsers.h"


/*****************************************************************************/
/* Create Bearer (Modem interface) */

static MMBearer *
modem_create_bearer_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    MMBearer *bearer;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    bearer = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));

    return g_object_ref (bearer);
}

static void
broadband_bearer_new_ready (GObject *source,
                            GAsyncResult *res,
                            GSimpleAsyncResult *simple)
{
    MMBearer *bearer = NULL;
    GError *error = NULL;

    bearer = mm_broadband_bearer_novatel_new_finish (res, &error);
    if (!bearer)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   bearer,
                                                   (GDestroyNotify)g_object_unref);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_create_bearer (MMIfaceModem *self,
                     MMBearerProperties *properties,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    GSimpleAsyncResult *result;

    /* Set a new ref to the bearer object as result */
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_create_bearer);

    /* We just create a MMBroadbandBearer */
    mm_broadband_bearer_novatel_new (MM_BROADBAND_MODEM_NOVATEL (self),
                                     properties,
                                     NULL, /* cancellable */
                                     (GAsyncReadyCallback)broadband_bearer_new_ready,
                                     result);
}

static void iface_modem_init (MMIfaceModem *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemNovatel, mm_broadband_modem_novatel, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init));

MMBroadbandModemNovatel *
mm_broadband_modem_novatel_new (const gchar *device,
                                const gchar *driver,
                                const gchar *plugin,
                                guint16 vendor_id,
                                guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_NOVATEL,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVER, driver,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         MM_BROADBAND_MODEM_USE_WS46, TRUE,
                         NULL);
}

static void
mm_broadband_modem_novatel_init (MMBroadbandModemNovatel *self)
{
}

/*****************************************************************************/
/* SUPPORTED BANDS */

/*
 * Mapping from bits set in response of $NWBAND? command to MMModemBand values.
 * The bit positions and band names on the right come from the response to $NWBAND=?
 */
static MMModemBand bandbits[] = {
    MM_MODEM_BAND_CDMA_BC0_CELLULAR_800,   /* "00 CDMA2000 Band Class 0, A-System" */
    MM_MODEM_BAND_CDMA_BC0_CELLULAR_800,   /* "01 CDMA2000 Band Class 0, B-System" */
    MM_MODEM_BAND_CDMA_BC1_PCS_1900,       /* "02 CDMA2000 Band Class 1, all blocks" */
    MM_MODEM_BAND_CDMA_BC2_TACS,           /* "03 CDMA2000 Band Class 2, place holder" */
    MM_MODEM_BAND_CDMA_BC3_JTACS,          /* "04 CDMA2000 Band Class 3, A-System" */
    MM_MODEM_BAND_CDMA_BC4_KOREAN_PCS,     /* "05 CDMA2000 Band Class 4, all blocks" */
    MM_MODEM_BAND_CDMA_BC5_NMT450,         /* "06 CDMA2000 Band Class 5, all blocks" */
    MM_MODEM_BAND_DCS,                     /* "07 GSM DCS band" */
    MM_MODEM_BAND_EGSM,                    /* "08 GSM Extended GSM (E-GSM) band" */
    MM_MODEM_BAND_UNKNOWN,                 /* "09 GSM Primary GSM (P-GSM) band" */
    MM_MODEM_BAND_CDMA_BC6_IMT2000,        /* "10 CDMA2000 Band Class 6" */
    MM_MODEM_BAND_CDMA_BC7_CELLULAR_700,   /* "11 CDMA2000 Band Class 7" */
    MM_MODEM_BAND_CDMA_BC8_1800,           /* "12 CDMA2000 Band Class 8" */
    MM_MODEM_BAND_CDMA_BC9_900,            /* "13 CDMA2000 Band Class 9" */
    MM_MODEM_BAND_CDMA_BC10_SECONDARY_800, /* "14 CDMA2000 Band Class 10 */
    MM_MODEM_BAND_CDMA_BC11_PAMR_400,      /* "15 CDMA2000 Band Class 11 */
    MM_MODEM_BAND_UNKNOWN,                 /* "16 GSM 450 band" */
    MM_MODEM_BAND_UNKNOWN,                 /* "17 GSM 480 band" */
    MM_MODEM_BAND_UNKNOWN,                 /* "18 GSM 750 band" */
    MM_MODEM_BAND_G850,                    /* "19 GSM 850 band" */
    MM_MODEM_BAND_UNKNOWN,                 /* "20 GSM band" */
    MM_MODEM_BAND_PCS,                     /* "21 GSM PCS band" */
    MM_MODEM_BAND_U2100,                   /* "22 WCDMA I IMT 2000 band" */
    MM_MODEM_BAND_U1900,                   /* "23 WCDMA II PCS band" */
    MM_MODEM_BAND_U1800,                   /* "24 WCDMA III 1700 band" */
    MM_MODEM_BAND_U17IV,                   /* "25 WCDMA IV 1700 band" */
    MM_MODEM_BAND_U850,                    /* "26 WCDMA V US850 band" */
    MM_MODEM_BAND_U800,                    /* "27 WCDMA VI JAPAN 800 band" */
    MM_MODEM_BAND_UNKNOWN,                 /* "28 Reserved for BC12/BC14 */
    MM_MODEM_BAND_UNKNOWN,                 /* "29 Reserved for BC12/BC14 */
    MM_MODEM_BAND_UNKNOWN,                 /* "30 Reserved" */
    MM_MODEM_BAND_UNKNOWN,                 /* "31 Reserved" */
};

static GArray *
load_supported_bands_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    /* Never fails */
    return (GArray *) g_array_ref (g_simple_async_result_get_op_res_gpointer (
                                       G_SIMPLE_ASYNC_RESULT (res)));
}

static void
load_supported_bands (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    GSimpleAsyncResult *result;
    GArray *bands;
    guint i;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_supported_bands);

    /*
     * The modem doesn't support telling us what bands are supported;
     * list everything we know about.
     */
    bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 23);
    for (i = 0 ; i < G_N_ELEMENTS (bandbits) ; i++) {
        if (bandbits[i] != MM_MODEM_BAND_UNKNOWN)
            g_array_append_val(bands, bandbits[i]);
    }

    g_simple_async_result_set_op_res_gpointer (result,
                                               bands,
                                               (GDestroyNotify)g_array_unref);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}


static GArray *
load_current_bands_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           GError **error)
{
    /* Never fails */
    return (GArray *) g_array_ref (g_simple_async_result_get_op_res_gpointer (
                                       G_SIMPLE_ASYNC_RESULT (res)));
}

static void
load_current_bands_done (MMIfaceModem *self,
                         GAsyncResult *res,
                         GSimpleAsyncResult *operation_result)
{
    GArray *bands;
    const gchar *response;
    GError *error = NULL;
    guint i;
    guint32 bandval;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        mm_dbg ("Couldn't query supported bands: '%s'", error->message);
        g_simple_async_result_take_error (operation_result, error);
        g_simple_async_result_complete_in_idle (operation_result);
        g_object_unref (operation_result);
        return;
    }

    /*
     * Response is "$NWBAND: <hex value>", where the hex value is a
     * bitmask of supported bands.
     */
    bandval = (guint32)strtoul(response + 9, NULL, 16);

    bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 4);
    for (i = 0 ; i < G_N_ELEMENTS (bandbits) ; i++) {
        if ((bandval & (1 << i)) && bandbits[i] != MM_MODEM_BAND_UNKNOWN)
            g_array_append_val(bands, bandbits[i]);
    }

    g_simple_async_result_set_op_res_gpointer (operation_result,
                                               bands,
                                               (GDestroyNotify)g_array_unref);
    g_simple_async_result_complete_in_idle (operation_result);
    g_object_unref (operation_result);
}

static void
load_current_bands (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_current_bands);

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "$NWBAND?",
        3,
        FALSE,
        (GAsyncReadyCallback)load_current_bands_done,
        result);
}

static gboolean
set_bands_finish (MMIfaceModem *self,
                  GAsyncResult *res,
                  GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
set_bands_done (MMIfaceModem *self,
                GAsyncResult *res,
                GSimpleAsyncResult *operation_result)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error))
        g_simple_async_result_take_error (operation_result, error);
    else
        g_simple_async_result_set_op_res_gboolean (operation_result, TRUE);

    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static void
set_bands (MMIfaceModem *self,
           GArray *bands_array,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    GSimpleAsyncResult *result;
    gchar *cmd;
    guint32 bandval;
    guint i, j;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        set_bands);

    bandval = 0;
    for (i = 0 ; i < bands_array->len ; i++) {
        MMModemBand band = g_array_index (bands_array, MMModemBand, i);
        for (j = 0 ; j < G_N_ELEMENTS (bandbits) ; j++) {
            if (bandbits[j] == band ||
                (band == MM_MODEM_BAND_ANY && bandbits[j] != MM_MODEM_BAND_UNKNOWN))
                bandval |= 1 << j;
        }
    }

    cmd = g_strdup_printf ("$NWBAND=%x", bandval);

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        cmd,
        3,
        FALSE,
        (GAsyncReadyCallback)set_bands_done,
        result);

    g_free (cmd);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
    iface->load_supported_bands = load_supported_bands;
    iface->load_supported_bands_finish = load_supported_bands_finish;
    iface->load_current_bands = load_current_bands;
    iface->load_current_bands_finish = load_current_bands_finish;
    iface->set_bands = set_bands;
    iface->set_bands_finish = set_bands_finish;
}

static void
mm_broadband_modem_novatel_class_init (MMBroadbandModemNovatelClass *klass)
{
}
