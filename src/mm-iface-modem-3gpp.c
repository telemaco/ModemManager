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
 * Copyright (C) 2011 - 2012 Google, Inc.
 */

#include <stdlib.h>
#include <string.h>

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-location.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-base-modem.h"
#include "mm-modem-helpers.h"
#include "mm-log.h"

#define REGISTRATION_CHECK_TIMEOUT_SEC 30

#define SUBSYSTEM_3GPP "3gpp"

#define INDICATORS_CHECKED_TAG            "3gpp-indicators-checked-tag"
#define UNSOLICITED_EVENTS_SUPPORTED_TAG  "3gpp-unsolicited-events-supported-tag"
#define REGISTRATION_STATE_CONTEXT_TAG    "3gpp-registration-state-context-tag"
#define REGISTRATION_CHECK_CONTEXT_TAG    "3gpp-registration-check-context-tag"

static GQuark indicators_checked_quark;
static GQuark unsolicited_events_supported_quark;
static GQuark registration_state_context_quark;
static GQuark registration_check_context_quark;

/*****************************************************************************/

void
mm_iface_modem_3gpp_bind_simple_status (MMIfaceModem3gpp *self,
                                        MMSimpleStatus *status)
{
    MmGdbusModem3gpp *skeleton;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &skeleton,
                  NULL);

    g_object_bind_property (skeleton, "registration-state",
                            status, MM_SIMPLE_PROPERTY_3GPP_REGISTRATION_STATE,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_bind_property (skeleton, "operator-code",
                            status, MM_SIMPLE_PROPERTY_3GPP_OPERATOR_CODE,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_bind_property (skeleton, "operator-name",
                            status, MM_SIMPLE_PROPERTY_3GPP_OPERATOR_NAME,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_unref (skeleton);
}

/*****************************************************************************/

gboolean
mm_iface_modem_3gpp_register_in_network_finish (MMIfaceModem3gpp *self,
                                                GAsyncResult *res,
                                                GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
register_in_network_ready (MMIfaceModem3gpp *self,
                           GAsyncResult *res,
                           GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

void
mm_iface_modem_3gpp_register_in_network (MMIfaceModem3gpp *self,
                                         const gchar *operator_id,
                                         guint max_registration_time,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
    GSimpleAsyncResult *result;
    MmGdbusModem3gpp *skeleton;
    const gchar *current_operator_code;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_iface_modem_3gpp_register_in_network);

    /* If no operator ID given, or if we're already registered with the requested one,
     * then we're done */
    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &skeleton,
                  NULL);
    current_operator_code = mm_gdbus_modem3gpp_get_operator_code (skeleton);
    if (current_operator_code &&
        (!operator_id || g_str_equal (current_operator_code, operator_id))) {
        /* Already registered, no need to request it again */
        mm_dbg ("Already registered in network '%s'...", current_operator_code);
        g_simple_async_result_set_op_res_gboolean (result, TRUE);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
    } else {
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network (
            self,
            operator_id,
            max_registration_time,
            (GAsyncReadyCallback)register_in_network_ready,
            result);
    }

    g_object_unref (skeleton);
}

typedef struct {
    MmGdbusModem3gpp *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem3gpp *self;
    gchar *operator_id;
} HandleRegisterContext;

static void
handle_register_context_free (HandleRegisterContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx->operator_id);
    g_free (ctx);
}

static void
handle_register_ready (MMIfaceModem3gpp *self,
                       GAsyncResult *res,
                       HandleRegisterContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network_finish (self, res,&error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem3gpp_complete_register (ctx->skeleton, ctx->invocation);

    handle_register_context_free (ctx);
}

static void
handle_register_auth_ready (MMBaseModem *self,
                            GAsyncResult *res,
                            HandleRegisterContext *ctx)
{
    MMModemState modem_state;
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_register_context_free (ctx);
        return;
    }

    g_assert (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network != NULL);
    g_assert (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network_finish != NULL);

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    switch (modem_state) {
    case MM_MODEM_STATE_FAILED:
    case MM_MODEM_STATE_UNKNOWN:
    case MM_MODEM_STATE_LOCKED:
        /* We should never have such request (interface wasn't exported yet) */
        g_assert_not_reached ();
        break;

    case MM_MODEM_STATE_INITIALIZING:
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot register modem: "
                                               "device not fully initialized yet");
        break;

    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
        mm_iface_modem_3gpp_register_in_network (MM_IFACE_MODEM_3GPP (self),
                                                 ctx->operator_id,
                                                 60,
                                                 (GAsyncReadyCallback)handle_register_ready,
                                                 ctx);
        return;

    case MM_MODEM_STATE_DISABLING:
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot register modem: "
                                               "currently being disabled");
        break;

    case MM_MODEM_STATE_ENABLING:
    case MM_MODEM_STATE_DISABLED:
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot register modem: "
                                               "not yet enabled");
        break;

    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot register modem: "
                                               "modem is connected");
        break;
    }

    handle_register_context_free (ctx);
}

static gboolean
handle_register (MmGdbusModem3gpp *skeleton,
                 GDBusMethodInvocation *invocation,
                 const gchar *operator_id,
                 MMIfaceModem3gpp *self)
{
    HandleRegisterContext *ctx;

    ctx = g_new (HandleRegisterContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->operator_id = g_strdup (operator_id);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_register_auth_ready,
                             ctx);

    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem3gpp *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem3gpp *self;
} HandleScanContext;

static void
handle_scan_context_free (HandleScanContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static GVariant *
scan_networks_build_result (GList *info_list)
{
    GList *l;
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));

    for (l = info_list; l; l = g_list_next (l)) {
        MM3gppNetworkInfo *info = l->data;

        if (!info->operator_code) {
            g_warn_if_reached ();
            continue;
        }

        g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{sv}"));

        g_variant_builder_add (&builder, "{sv}",
                               "operator-code", g_variant_new_string (info->operator_code));
        g_variant_builder_add (&builder, "{sv}",
                               "status", g_variant_new_uint32 (info->status));
        g_variant_builder_add (&builder, "{sv}",
                               "access-technology", g_variant_new_uint32 (info->access_tech));
        if (info->operator_long)
            g_variant_builder_add (&builder, "{sv}",
                                   "operator-long", g_variant_new_string (info->operator_long));
        if (info->operator_short)
            g_variant_builder_add (&builder, "{sv}",
                                   "operator-short", g_variant_new_string (info->operator_short));
        g_variant_builder_close (&builder);
    }

    return g_variant_builder_end (&builder);
}

static void
handle_scan_ready (MMIfaceModem3gpp *self,
                   GAsyncResult *res,
                   HandleScanContext *ctx)
{
    GError *error = NULL;
    GList *info_list;

    info_list = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->scan_networks_finish (self, res, &error);
    if (error)
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        GVariant *dict_array;

        dict_array = scan_networks_build_result (info_list);
        mm_gdbus_modem3gpp_complete_scan (ctx->skeleton,
                                          ctx->invocation,
                                          dict_array);
        g_variant_unref (dict_array);
    }

    mm_3gpp_network_info_list_free (info_list);
    handle_scan_context_free (ctx);
}

static void
handle_scan_auth_ready (MMBaseModem *self,
                        GAsyncResult *res,
                        HandleScanContext *ctx)
{
    MMModemState modem_state;
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_scan_context_free (ctx);
        return;
    }

    /* If scanning is not implemented, report an error */
    if (!MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->scan_networks ||
        !MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->scan_networks_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot scan networks: operation not supported");
        handle_scan_context_free (ctx);
        return;
    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    switch (modem_state) {
    case MM_MODEM_STATE_FAILED:
    case MM_MODEM_STATE_UNKNOWN:
    case MM_MODEM_STATE_LOCKED:
        /* We should never have such request (interface wasn't exported yet) */
        g_assert_not_reached ();
        break;

    case MM_MODEM_STATE_INITIALIZING:
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot scan networks: "
                                               "device not fully initialized yet");
        break;

    case MM_MODEM_STATE_DISABLED:
    case MM_MODEM_STATE_DISABLING:
    case MM_MODEM_STATE_ENABLING:
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot scan networks: not enabled yet");
        break;

    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->scan_networks (
            MM_IFACE_MODEM_3GPP (self),
            (GAsyncReadyCallback)handle_scan_ready,
            ctx);
        return;
    }

    handle_scan_context_free (ctx);
}

static gboolean
handle_scan (MmGdbusModem3gpp *skeleton,
             GDBusMethodInvocation *invocation,
             MMIfaceModem3gpp *self)
{
    HandleScanContext *ctx;

    ctx = g_new (HandleScanContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_scan_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    GSimpleAsyncResult *result;
    gboolean cs_supported;
    gboolean ps_supported;
    gboolean cs_done;
    GError *cs_reg_error;
    GError *ps_reg_error;
} RunAllRegistrationChecksContext;

static void
run_all_registration_checks_context_complete_and_free (RunAllRegistrationChecksContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_clear_error (&ctx->cs_reg_error);
    g_clear_error (&ctx->ps_reg_error);
    g_object_unref (ctx->result);
    g_free (ctx);
}

gboolean
mm_iface_modem_3gpp_run_all_registration_checks_finish (MMIfaceModem3gpp *self,
                                                        GAsyncResult *res,
                                                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
run_ps_registration_check_ready (MMIfaceModem3gpp *self,
                                 GAsyncResult *res,
                                 RunAllRegistrationChecksContext *ctx)
{
    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check_finish (self, res, &ctx->ps_reg_error);

    /* If both CS and PS registration checks returned errors we fail */
    if (ctx->ps_reg_error &&
        (ctx->cs_reg_error || !ctx->cs_done))
        /* Prefer the PS error */
        g_simple_async_result_set_from_error (ctx->result, ctx->ps_reg_error);
    else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    run_all_registration_checks_context_complete_and_free (ctx);
}

static void
run_cs_registration_check_ready (MMIfaceModem3gpp *self,
                                 GAsyncResult *res,
                                 RunAllRegistrationChecksContext *ctx)
{
    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_cs_registration_check_finish (self, res, &ctx->cs_reg_error);

    if (ctx->ps_supported &&
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check &&
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check_finish) {
        ctx->cs_done = TRUE;
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check (
            self,
            (GAsyncReadyCallback)run_ps_registration_check_ready,
            ctx);
        return;
    }

    /* All done */
    if (ctx->cs_reg_error)
        g_simple_async_result_set_from_error (ctx->result, ctx->cs_reg_error);
    else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    run_all_registration_checks_context_complete_and_free (ctx);
}

void
mm_iface_modem_3gpp_run_all_registration_checks (MMIfaceModem3gpp *self,
                                                 GAsyncReadyCallback callback,
                                                 gpointer user_data)
{
    RunAllRegistrationChecksContext *ctx;

    ctx = g_new0 (RunAllRegistrationChecksContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_3gpp_run_all_registration_checks);

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED, &ctx->ps_supported,
                  MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED, &ctx->cs_supported,
                  NULL);

    mm_dbg ("Running registration checks (CS: '%s', PS: '%s')",
            ctx->cs_supported ? "yes" : "no",
            ctx->ps_supported ? "yes" : "no");

    if (ctx->cs_supported &&
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_cs_registration_check &&
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_cs_registration_check_finish) {
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_cs_registration_check (
            self,
            (GAsyncReadyCallback)run_cs_registration_check_ready,
            ctx);
        return;
    }

    if (ctx->ps_supported &&
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check &&
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check_finish) {
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check (
            self,
            (GAsyncReadyCallback)run_ps_registration_check_ready,
            ctx);
        return;
    }

    /* Nothing to do :-/ all done */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    run_all_registration_checks_context_complete_and_free (ctx);
}

/*****************************************************************************/

static void
load_operator_name_ready (MMIfaceModem3gpp *self,
                          GAsyncResult *res)
{
    GError *error = NULL;
    MmGdbusModem3gpp *skeleton = NULL;
    gchar *str;

    str = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_name_finish (self, res, &error);
    if (error) {
        mm_warn ("Couldn't load Operator Name: '%s'", error->message);
        g_error_free (error);
    }

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &skeleton,
                  NULL);
    mm_gdbus_modem3gpp_set_operator_name (skeleton, str);
    g_free (str);
    g_object_unref (skeleton);
}

static gboolean
parse_mcc_mnc (const gchar *mccmnc,
               guint *mcc_out,
               guint *mnc_out)
{
    guint mccmnc_len;
    gchar mcc[4] = { 0, 0, 0, 0 };
    gchar mnc[4] = { 0, 0, 0, 0 };

    mccmnc_len = (mccmnc ? strlen (mccmnc) : 0);
    if (mccmnc_len != 5 &&
        mccmnc_len != 6) {
        mm_dbg ("Unexpected MCC/MNC string '%s'", mccmnc);
        return FALSE;
    }

    memcpy (mcc, mccmnc, 3);
    /* Not all modems report 6-digit MNCs */
    memcpy (mnc, mccmnc + 3, 2);
    if (mccmnc_len == 6)
        mnc[2] = mccmnc[5];

    *mcc_out = atoi (mcc);
    *mnc_out = atoi (mnc);

    return TRUE;
}

static void
load_operator_code_ready (MMIfaceModem3gpp *self,
                          GAsyncResult *res)
{
    GError *error = NULL;
    MmGdbusModem3gpp *skeleton = NULL;
    gchar *str;

    str = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_code_finish (self, res, &error);
    if (error) {
        mm_warn ("Couldn't load Operator Code: '%s'", error->message);
        g_error_free (error);
    }

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &skeleton,
                  NULL);
    mm_gdbus_modem3gpp_set_operator_code (skeleton, str);

    /* If we also implement the location interface, update the 3GPP location */
    if (str && MM_IS_IFACE_MODEM_LOCATION (self)) {
        guint mcc = 0;
        guint mnc = 0;

        if (parse_mcc_mnc (str, &mcc, &mnc))
            mm_iface_modem_location_3gpp_update_mcc_mnc (MM_IFACE_MODEM_LOCATION (self), mcc, mnc);
    }

    g_free (str);
    g_object_unref (skeleton);
}

void
mm_iface_modem_3gpp_reload_current_operator (MMIfaceModem3gpp *self)
{
    MMModem3gppRegistrationState state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    MmGdbusModem3gpp *skeleton = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, &state,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &skeleton,
                  NULL);

    if (state == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
        state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING) {
        /* Launch operator code update */
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_code &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_code_finish)
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_code (
                self,
                (GAsyncReadyCallback)load_operator_code_ready,
                NULL);
        /* Launch operator name update */
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_name &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_name_finish)
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_name (
                self,
                (GAsyncReadyCallback)load_operator_name_ready,
                NULL);
    } else {
        mm_gdbus_modem3gpp_set_operator_code (skeleton, NULL);
        mm_gdbus_modem3gpp_set_operator_name (skeleton, NULL);
        if (MM_IS_IFACE_MODEM_LOCATION (self))
            mm_iface_modem_location_3gpp_update_mcc_mnc (MM_IFACE_MODEM_LOCATION (self), 0, 0);
    }

    g_object_unref (skeleton);
}

/*****************************************************************************/

static void
update_registration_state (MMIfaceModem3gpp *self,
                           MMModem3gppRegistrationState new_state,
                           MMModemAccessTechnology access_tech,
                           gulong location_area_code,
                           gulong cell_id)
{
    MMModem3gppRegistrationState old_state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    MmGdbusModem3gpp *skeleton = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, &old_state,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &skeleton,
                  NULL);

    /* Only set new state if different */
    if (new_state != old_state) {
        mm_info ("Modem %s: 3GPP Registration state changed (%s -> %s)",
                 g_dbus_object_get_object_path (G_DBUS_OBJECT (self)),
                 mm_modem_3gpp_registration_state_get_string (old_state),
                 mm_modem_3gpp_registration_state_get_string (new_state));

        /* The property in the interface is bound to the property
         * in the skeleton, so just updating here is enough */
        g_object_set (self,
                      MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, new_state,
                      NULL);

        /* Reload current operator */
        mm_iface_modem_3gpp_reload_current_operator (self);

        if (new_state == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
            new_state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING) {
            mm_iface_modem_update_subsystem_state (MM_IFACE_MODEM (self),
                                                   SUBSYSTEM_3GPP,
                                                   MM_MODEM_STATE_REGISTERED,
                                                   MM_MODEM_STATE_CHANGE_REASON_UNKNOWN);
        }
        /* Not registered neither in home nor roaming network */
        else {
            mm_iface_modem_update_subsystem_state (
                MM_IFACE_MODEM (self),
                SUBSYSTEM_3GPP,
                (new_state == MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING ?
                 MM_MODEM_STATE_SEARCHING :
                 MM_MODEM_STATE_ENABLED),
                MM_MODEM_STATE_CHANGE_REASON_UNKNOWN);
        }
    }

    /* Even if registration state didn't change, report access technology or
     * location updates, but only if something valid to report */
    if (new_state == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
        new_state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING) {
        if (access_tech != MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN)
            mm_iface_modem_update_access_technologies (MM_IFACE_MODEM (self),
                                                       access_tech,
                                                       MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK);
        if (MM_IS_IFACE_MODEM_LOCATION (self) &&
            location_area_code > 0 &&
            cell_id > 0)
            mm_iface_modem_location_3gpp_update_lac_ci (MM_IFACE_MODEM_LOCATION (self),
                                                        location_area_code,
                                                        cell_id);
    } else {
        mm_iface_modem_update_access_technologies (MM_IFACE_MODEM (self),
                                                   MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN,
                                                   MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK);
        if (MM_IS_IFACE_MODEM_LOCATION (self))
            mm_iface_modem_location_3gpp_clear (MM_IFACE_MODEM_LOCATION (self));
    }

    g_object_unref (skeleton);
}

typedef struct {
    MMModem3gppRegistrationState cs;
    MMModem3gppRegistrationState ps;
} RegistrationStateContext;

static RegistrationStateContext *
get_registration_state_context (MMIfaceModem3gpp *self)
{
    RegistrationStateContext *ctx;

    if (G_UNLIKELY (!registration_state_context_quark))
        registration_state_context_quark =  (g_quark_from_static_string (
                                                 REGISTRATION_STATE_CONTEXT_TAG));

    ctx = g_object_get_qdata (G_OBJECT (self), registration_state_context_quark);
    if (!ctx) {
        /* Create context and keep it as object data */
        ctx = g_new0 (RegistrationStateContext, 1);
        ctx->cs = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
        ctx->ps = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;

        g_object_set_qdata_full (
            G_OBJECT (self),
            registration_state_context_quark,
            ctx,
            (GDestroyNotify)g_free);
    }

    return ctx;
}

static MMModem3gppRegistrationState
get_consolidated_reg_state (RegistrationStateContext *ctx)
{
    /* Some devices (Blackberries for example) will respond to +CGREG, but
     * return ERROR for +CREG, probably because their firmware is just stupid.
     * So here we prefer the +CREG response, but if we never got a successful
     * +CREG response, we'll take +CGREG instead.
     */
    if (ctx->cs == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
        ctx->cs == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING)
        return ctx->cs;

    if (ctx->ps == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
        ctx->ps == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING)
        return ctx->ps;

    if (ctx->cs == MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING)
        return ctx->cs;

    if (ctx->ps == MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING)
        return ctx->ps;

    return ctx->cs;
}

void
mm_iface_modem_3gpp_update_cs_registration_state (MMIfaceModem3gpp *self,
                                                  MMModem3gppRegistrationState state,
                                                  MMModemAccessTechnology access_tech,
                                                  gulong location_area_code,
                                                  gulong cell_id)
{
    RegistrationStateContext *ctx;
    gboolean supported = FALSE;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED, &supported,
                  NULL);

    if (!supported)
        return;

    ctx = get_registration_state_context (self);
    ctx->cs = state;
    update_registration_state (self,
                               get_consolidated_reg_state (ctx),
                               access_tech,
                               location_area_code,
                               cell_id);
}

void
mm_iface_modem_3gpp_update_ps_registration_state (MMIfaceModem3gpp *self,
                                                  MMModem3gppRegistrationState state,
                                                  MMModemAccessTechnology access_tech,
                                                  gulong location_area_code,
                                                  gulong cell_id)
{
    RegistrationStateContext *ctx;
    gboolean supported = FALSE;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED, &supported,
                  NULL);

    if (!supported)
        return;

    ctx = get_registration_state_context (self);
    ctx->ps = state;
    update_registration_state (self,
                               get_consolidated_reg_state (ctx),
                               access_tech,
                               location_area_code,
                               cell_id);
}

/*****************************************************************************/

typedef struct {
    guint timeout_source;
    gboolean running;
} RegistrationCheckContext;

static void
registration_check_context_free (RegistrationCheckContext *ctx)
{
    if (ctx->timeout_source)
        g_source_remove (ctx->timeout_source);
    g_free (ctx);
}

static void
periodic_registration_checks_ready (MMIfaceModem3gpp *self,
                                    GAsyncResult *res)
{
    RegistrationCheckContext *ctx;
    GError *error = NULL;

    mm_iface_modem_3gpp_run_all_registration_checks_finish (self, res, &error);
    if (error) {
        mm_dbg ("Couldn't refresh 3GPP registration status: '%s'", error->message);
        g_error_free (error);
    }

    /* Remove the running tag */
    ctx = g_object_get_qdata (G_OBJECT (self), registration_check_context_quark);
    ctx->running = FALSE;
}

static gboolean
periodic_registration_check (MMIfaceModem3gpp *self)
{
    RegistrationCheckContext *ctx;

    /* Only launch a new one if not one running already */
    ctx = g_object_get_qdata (G_OBJECT (self), registration_check_context_quark);
    if (!ctx->running) {
        ctx->running = TRUE;
        mm_iface_modem_3gpp_run_all_registration_checks (
            self,
            (GAsyncReadyCallback)periodic_registration_checks_ready,
            NULL);
    }
    return TRUE;
}

static void
periodic_registration_check_disable (MMIfaceModem3gpp *self)
{
    if (G_UNLIKELY (!registration_check_context_quark))
        registration_check_context_quark = (g_quark_from_static_string (
                                                REGISTRATION_CHECK_CONTEXT_TAG));

    /* Overwriting the data will free the previous context */
    g_object_set_qdata (G_OBJECT (self),
                        registration_check_context_quark,
                        NULL);

    mm_dbg ("Periodic 3GPP registration checks disabled");
}

static void
periodic_registration_check_enable (MMIfaceModem3gpp *self)
{
    RegistrationCheckContext *ctx;

    if (G_UNLIKELY (!registration_check_context_quark))
        registration_check_context_quark = (g_quark_from_static_string (
                                                REGISTRATION_CHECK_CONTEXT_TAG));

    ctx = g_object_get_qdata (G_OBJECT (self), registration_check_context_quark);

    /* If context is already there, we're already enabled */
    if (ctx)
        return;

    /* Create context and keep it as object data */
    mm_dbg ("Periodic 3GPP registration checks enabled");
    ctx = g_new0 (RegistrationCheckContext, 1);
    ctx->timeout_source = g_timeout_add_seconds (REGISTRATION_CHECK_TIMEOUT_SEC,
                                                 (GSourceFunc)periodic_registration_check,
                                                 self);
    g_object_set_qdata_full (G_OBJECT (self),
                             registration_check_context_quark,
                             ctx,
                             (GDestroyNotify)registration_check_context_free);
}

/*****************************************************************************/

typedef struct _DisablingContext DisablingContext;
static void interface_disabling_step (DisablingContext *ctx);

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_PERIODIC_REGISTRATION_CHECKS,
    DISABLING_STEP_CLEANUP_PS_REGISTRATION,
    DISABLING_STEP_CLEANUP_CS_REGISTRATION,
    DISABLING_STEP_CLEANUP_UNSOLICITED_REGISTRATION,
    DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS,
    DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS,
    DISABLING_STEP_LAST
} DisablingStep;

struct _DisablingContext {
    MMIfaceModem3gpp *self;
    DisablingStep step;
    GSimpleAsyncResult *result;
    MmGdbusModem *skeleton;
};

static DisablingContext *
disabling_context_new (MMIfaceModem3gpp *self,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    DisablingContext *ctx;

    ctx = g_new0 (DisablingContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             disabling_context_new);
    ctx->step = DISABLING_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    g_assert (ctx->skeleton != NULL);

    return ctx;
}

static void
disabling_context_complete_and_free (DisablingContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->result);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_3gpp_disable_finish (MMIfaceModem3gpp *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

#undef VOID_REPLY_READY_FN
#define VOID_REPLY_READY_FN(NAME,DISPLAY)                               \
    static void                                                         \
    NAME##_ready (MMIfaceModem3gpp *self,                               \
                  GAsyncResult *res,                                    \
                  DisablingContext *ctx)                                \
    {                                                                   \
        GError *error = NULL;                                           \
                                                                        \
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->NAME##_finish (self, res, &error); \
        if (error) {                                                    \
            mm_dbg ("Couldn't %s: '%s'", DISPLAY, error->message);      \
            g_error_free (error);                                       \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        interface_disabling_step (ctx);                                 \
    }

VOID_REPLY_READY_FN (cleanup_unsolicited_registration,
                     "cleanup unsolicited registration")
VOID_REPLY_READY_FN (cleanup_ps_registration,
                     "cleanup PS registration")
VOID_REPLY_READY_FN (cleanup_cs_registration,
                     "cleanup CS registration")
VOID_REPLY_READY_FN (cleanup_unsolicited_events,
                     "cleanup unsolicited events")
VOID_REPLY_READY_FN (disable_unsolicited_events,
                     "disable unsolicited events")

static void
interface_disabling_step (DisablingContext *ctx)
{
    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_PERIODIC_REGISTRATION_CHECKS:
        periodic_registration_check_disable (ctx->self);
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_CLEANUP_PS_REGISTRATION: {
        gboolean ps_supported = FALSE;

        g_object_get (ctx->self,
                      MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED, &ps_supported,
                      NULL);

        if (ps_supported &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_ps_registration &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_ps_registration_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_ps_registration (
                ctx->self,
                (GAsyncReadyCallback)cleanup_ps_registration_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;
    }

    case DISABLING_STEP_CLEANUP_CS_REGISTRATION: {
        gboolean cs_supported = FALSE;

        g_object_get (ctx->self,
                      MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED, &cs_supported,
                      NULL);

        if (cs_supported &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_cs_registration &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_cs_registration_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_cs_registration (
                ctx->self,
                (GAsyncReadyCallback)cleanup_cs_registration_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;
    }

    case DISABLING_STEP_CLEANUP_UNSOLICITED_REGISTRATION:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_unsolicited_registration &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_unsolicited_registration_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_unsolicited_registration (
                ctx->self,
                (GAsyncReadyCallback)cleanup_unsolicited_registration_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS:
        if (G_UNLIKELY (!unsolicited_events_supported_quark))
            unsolicited_events_supported_quark = (g_quark_from_static_string (
                                                      UNSOLICITED_EVENTS_SUPPORTED_TAG));

        /* Only try to disable if supported */
        if (GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (ctx->self),
                                                  unsolicited_events_supported_quark))) {
            if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_unsolicited_events &&
                MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_unsolicited_events_finish) {
                MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_unsolicited_events (
                    ctx->self,
                    (GAsyncReadyCallback)cleanup_unsolicited_events_ready,
                    ctx);
                return;
            }
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS:
        if (G_UNLIKELY (!unsolicited_events_supported_quark))
            unsolicited_events_supported_quark = (g_quark_from_static_string (
                                                      UNSOLICITED_EVENTS_SUPPORTED_TAG));

        /* Only try to disable if supported */
        if (GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (ctx->self),
                                                  unsolicited_events_supported_quark))) {
            if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->disable_unsolicited_events &&
                MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->disable_unsolicited_events_finish) {
                MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->disable_unsolicited_events (
                    ctx->self,
                    (GAsyncReadyCallback)disable_unsolicited_events_ready,
                    ctx);
                return;
            }
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        disabling_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_3gpp_disable (MMIfaceModem3gpp *self,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    interface_disabling_step (disabling_context_new (self,
                                                     callback,
                                                     user_data));
}

/*****************************************************************************/

typedef struct _EnablingContext EnablingContext;
static void interface_enabling_step (EnablingContext *ctx);

typedef enum {
    ENABLING_STEP_FIRST,
    ENABLING_STEP_SETUP_INDICATORS,
    ENABLING_STEP_SETUP_UNSOLICITED_EVENTS,
    ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS,
    ENABLING_STEP_SETUP_UNSOLICITED_REGISTRATION,
    ENABLING_STEP_SETUP_CS_REGISTRATION,
    ENABLING_STEP_SETUP_PS_REGISTRATION,
    ENABLING_STEP_RUN_ALL_REGISTRATION_CHECKS,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    MMIfaceModem3gpp *self;
    EnablingStep step;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    MmGdbusModem3gpp *skeleton;
};

static EnablingContext *
enabling_context_new (MMIfaceModem3gpp *self,
                      GCancellable *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    EnablingContext *ctx;

    ctx = g_new0 (EnablingContext, 1);
    ctx->self = g_object_ref (self);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             enabling_context_new);
    ctx->step = ENABLING_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    g_assert (ctx->skeleton != NULL);

    return ctx;
}

static void
enabling_context_complete_and_free (EnablingContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->result);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static gboolean
enabling_context_complete_and_free_if_cancelled (EnablingContext *ctx)
{
    if (!g_cancellable_is_cancelled (ctx->cancellable))
        return FALSE;

    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_CANCELLED,
                                     "Interface enabling cancelled");
    enabling_context_complete_and_free (ctx);
    return TRUE;
}

gboolean
mm_iface_modem_3gpp_enable_finish (MMIfaceModem3gpp *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

#undef VOID_REPLY_READY_FN
#define VOID_REPLY_READY_FN(NAME)                                       \
    static void                                                         \
    NAME##_ready (MMIfaceModem3gpp *self,                               \
                  GAsyncResult *res,                                    \
                  EnablingContext *ctx)                                 \
    {                                                                   \
        GError *error = NULL;                                           \
                                                                        \
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->NAME##_finish (self, res, &error); \
        if (error) {                                                    \
            g_simple_async_result_take_error (ctx->result, error);      \
            enabling_context_complete_and_free (ctx);                   \
            return;                                                     \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        interface_enabling_step (ctx);                                  \
    }

static void
setup_indicators_ready (MMIfaceModem3gpp *self,
                        GAsyncResult *res,
                        EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->setup_indicators_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_dbg ("Indicator control setup failed: '%s'", error->message);
        g_error_free (error);

        /* If we get an error setting up indicators, don't even bother trying to
         * enable unsolicited events. */
        ctx->step = ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS + 1;
        interface_enabling_step (ctx);
        return;
    }

    /* Indicators setup, so assume we support unsolicited events */
    g_object_set_qdata (G_OBJECT (self),
                        unsolicited_events_supported_quark,
                        GUINT_TO_POINTER (TRUE));

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
enable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                 GAsyncResult *res,
                                 EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->enable_unsolicited_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_dbg ("Enabling unsolicited events failed: '%s'", error->message);
        g_error_free (error);

        /* Reset support flag */
        g_object_set_qdata (G_OBJECT (self),
                            unsolicited_events_supported_quark,
                            GUINT_TO_POINTER (FALSE));
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
setup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                GAsyncResult *res,
                                EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->setup_unsolicited_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_dbg ("Setting up unsolicited events failed: '%s'", error->message);
        g_error_free (error);

        /* Reset support flag */
        g_object_set_qdata (G_OBJECT (self),
                            unsolicited_events_supported_quark,
                            GUINT_TO_POINTER (FALSE));

        /* If we get an error setting up unsolicited events, don't even bother trying to
         * enable them. */
        ctx->step = ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS + 1;
        interface_enabling_step (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
setup_cs_registration_ready (MMIfaceModem3gpp *self,
                             GAsyncResult *res,
                             EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->setup_cs_registration_finish (self, res, &error);
    if (error) {
        /* If error, setup periodic registration checks */
        periodic_registration_check_enable (ctx->self);
        mm_dbg ("Couldn't setup CS registration: '%s'",
                error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
setup_ps_registration_ready (MMIfaceModem3gpp *self,
                             GAsyncResult *res,
                             EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->setup_ps_registration_finish (self, res, &error);
    if (error) {
        /* If error, setup periodic registration checks */
        periodic_registration_check_enable (ctx->self);
        mm_dbg ("Couldn't setup PS registration: '%s'",
                error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
run_all_registration_checks_ready (MMIfaceModem3gpp *self,
                                   GAsyncResult *res,
                                   EnablingContext *ctx)
{
    GError *error = NULL;

    mm_iface_modem_3gpp_run_all_registration_checks_finish (self, res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        enabling_context_complete_and_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
setup_unsolicited_registration_ready (MMIfaceModem3gpp *self,
                                      GAsyncResult *res,
                                      EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->setup_unsolicited_registration_finish (self, res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        enabling_context_complete_and_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
interface_enabling_step (EnablingContext *ctx)
{
    /* Don't run new steps if we're cancelled */
    if (enabling_context_complete_and_free_if_cancelled (ctx))
        return;

    switch (ctx->step) {
    case ENABLING_STEP_FIRST:
        /* Setup quarks if we didn't do it before */
        if (G_UNLIKELY (!indicators_checked_quark))
            indicators_checked_quark = (g_quark_from_static_string (
                                            INDICATORS_CHECKED_TAG));
        if (G_UNLIKELY (!unsolicited_events_supported_quark))
            unsolicited_events_supported_quark = (g_quark_from_static_string (
                                                      UNSOLICITED_EVENTS_SUPPORTED_TAG));
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_SETUP_INDICATORS:
        if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (ctx->self),
                                                   indicators_checked_quark))) {
            /* Set the checked flag so that we don't run it again */
            g_object_set_qdata (G_OBJECT (ctx->self),
                                indicators_checked_quark,
                                GUINT_TO_POINTER (TRUE));
            /* Initially, assume we don't support unsolicited events */
            g_object_set_qdata (G_OBJECT (ctx->self),
                                unsolicited_events_supported_quark,
                                GUINT_TO_POINTER (FALSE));
            if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_indicators &&
                MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_indicators_finish) {
                MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_indicators (
                    ctx->self,
                    (GAsyncReadyCallback)setup_indicators_ready,
                    ctx);
                return;
            }
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_SETUP_UNSOLICITED_EVENTS:
        /* Only try to setup unsolicited events if they are supported */
        if (GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (ctx->self),
                                                  unsolicited_events_supported_quark))) {
            if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_unsolicited_events &&
                MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_unsolicited_events_finish) {
                MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_unsolicited_events (
                    ctx->self,
                    (GAsyncReadyCallback)setup_unsolicited_events_ready,
                    ctx);
                return;
            }
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS:
        /* Only try to enable unsolicited events if they are supported */
        if (GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (ctx->self),
                                                  unsolicited_events_supported_quark))) {
            if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->enable_unsolicited_events &&
                MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->enable_unsolicited_events_finish) {
                MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->enable_unsolicited_events (
                    ctx->self,
                    (GAsyncReadyCallback)enable_unsolicited_events_ready,
                    ctx);
                return;
            }
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_SETUP_UNSOLICITED_REGISTRATION:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_unsolicited_registration &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_unsolicited_registration_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_unsolicited_registration (
                ctx->self,
                (GAsyncReadyCallback)setup_unsolicited_registration_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_SETUP_CS_REGISTRATION: {
        gboolean cs_supported = FALSE;

        g_object_get (ctx->self,
                      MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED, &cs_supported,
                      NULL);

        if (cs_supported &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_cs_registration &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_cs_registration_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_cs_registration (
                ctx->self,
                (GAsyncReadyCallback)setup_cs_registration_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;
    }

    case ENABLING_STEP_SETUP_PS_REGISTRATION: {
        gboolean ps_supported = FALSE;

        g_object_get (ctx->self,
                      MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED, &ps_supported,
                      NULL);

        if (ps_supported &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_ps_registration &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_ps_registration_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_ps_registration (
                ctx->self,
                (GAsyncReadyCallback)setup_ps_registration_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;
    }
    case ENABLING_STEP_RUN_ALL_REGISTRATION_CHECKS:
        mm_iface_modem_3gpp_run_all_registration_checks (
            ctx->self,
            (GAsyncReadyCallback)run_all_registration_checks_ready,
            ctx);
        return;

    case ENABLING_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        enabling_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_3gpp_enable (MMIfaceModem3gpp *self,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    interface_enabling_step (enabling_context_new (self,
                                                   cancellable,
                                                   callback,
                                                   user_data));
}

/*****************************************************************************/

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (InitializationContext *ctx);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_IMEI,
    INITIALIZATION_STEP_ENABLED_FACILITY_LOCKS,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MMIfaceModem3gpp *self;
    MmGdbusModem3gpp *skeleton;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    InitializationStep step;
};

static InitializationContext *
initialization_context_new (MMIfaceModem3gpp *self,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    InitializationContext *ctx;

    ctx = g_new0 (InitializationContext, 1);
    ctx->self = g_object_ref (self);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             initialization_context_new);
    ctx->step = INITIALIZATION_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    g_assert (ctx->skeleton != NULL);
    return ctx;
}

static void
initialization_context_complete_and_free (InitializationContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->result);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static gboolean
initialization_context_complete_and_free_if_cancelled (InitializationContext *ctx)
{
    if (!g_cancellable_is_cancelled (ctx->cancellable))
        return FALSE;

    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_CANCELLED,
                                     "Interface initialization cancelled");
    initialization_context_complete_and_free (ctx);
    return TRUE;
}

static void
sim_pin_lock_enabled_cb (MMSim *self,
                         gboolean enabled,
                         MmGdbusModem3gpp *skeleton)
{
    MMModem3gppFacility facilities;

    facilities = mm_gdbus_modem3gpp_get_enabled_facility_locks (skeleton);
    if (enabled)
        facilities |= MM_MODEM_3GPP_FACILITY_SIM;
    else
        facilities &= ~MM_MODEM_3GPP_FACILITY_SIM;

    mm_gdbus_modem3gpp_set_enabled_facility_locks (skeleton, facilities);
}

static void
load_enabled_facility_locks_ready (MMIfaceModem3gpp *self,
                                   GAsyncResult *res,
                                   InitializationContext *ctx)
{
    GError *error = NULL;
    MMModem3gppFacility facilities;

    facilities = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_enabled_facility_locks_finish (self, res, &error);
    mm_gdbus_modem3gpp_set_enabled_facility_locks (ctx->skeleton, facilities);

    if (error) {
        mm_warn ("couldn't load facility locks: '%s'", error->message);
        g_error_free (error);
    } else {
        MMSim *sim = NULL;

        /* We loaded the initial list of facility locks; but we do need to update
         * the SIM PIN lock status when that changes. We'll connect to the signal
         * which notifies about such update. There is no need to ref self as the
         * SIM itself is an object which exists as long as self exists. */
        g_object_get (self,
                      MM_IFACE_MODEM_SIM, &sim,
                      NULL);
        g_signal_connect (sim,
                          MM_SIM_PIN_LOCK_ENABLED,
                          G_CALLBACK (sim_pin_lock_enabled_cb),
                          ctx->skeleton);
        g_object_unref (sim);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

static void
load_imei_ready (MMIfaceModem3gpp *self,
                 GAsyncResult *res,
                 InitializationContext *ctx)
{
    GError *error = NULL;
    gchar *imei;

    imei = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_imei_finish (self, res, &error);
    mm_gdbus_modem3gpp_set_imei (ctx->skeleton, imei);
    g_free (imei);

    if (error) {
        mm_warn ("couldn't load IMEI: '%s'", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

static void
interface_initialization_step (InitializationContext *ctx)
{
    /* Don't run new steps if we're cancelled */
    if (initialization_context_complete_and_free_if_cancelled (ctx))
        return;

    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_IMEI:
        /* IMEI value is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have it loaded,
         * don't try to load it again. */
        if (!mm_gdbus_modem3gpp_get_imei (ctx->skeleton) &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->load_imei &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->load_imei_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->load_imei (
                ctx->self,
                (GAsyncReadyCallback)load_imei_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_ENABLED_FACILITY_LOCKS:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->load_enabled_facility_locks &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->load_enabled_facility_locks_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->load_enabled_facility_locks (
                ctx->self,
                (GAsyncReadyCallback)load_enabled_facility_locks_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-register",
                          G_CALLBACK (handle_register),
                          ctx->self);
        g_signal_connect (ctx->skeleton,
                          "handle-scan",
                          G_CALLBACK (handle_scan),
                          ctx->self);


        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem3gpp (MM_GDBUS_OBJECT_SKELETON (ctx->self),
                                                MM_GDBUS_MODEM3GPP (ctx->skeleton));

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        initialization_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_3gpp_initialize_finish (MMIfaceModem3gpp *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    g_return_val_if_fail (MM_IS_IFACE_MODEM_3GPP (self), FALSE);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (res), FALSE);

    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

void
mm_iface_modem_3gpp_initialize (MMIfaceModem3gpp *self,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    MmGdbusModem3gpp *skeleton = NULL;

    g_return_if_fail (MM_IS_IFACE_MODEM_3GPP (self));

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem3gpp_skeleton_new ();

        /* Set all initial property defaults */
        mm_gdbus_modem3gpp_set_imei (skeleton, NULL);
        mm_gdbus_modem3gpp_set_operator_code (skeleton, NULL);
        mm_gdbus_modem3gpp_set_operator_name (skeleton, NULL);
        mm_gdbus_modem3gpp_set_enabled_facility_locks (skeleton, MM_MODEM_3GPP_FACILITY_NONE);

        /* Bind our RegistrationState property */
        g_object_bind_property (self, MM_IFACE_MODEM_3GPP_REGISTRATION_STATE,
                                skeleton, "registration-state",
                                G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

        g_object_set (self,
                      MM_IFACE_MODEM_3GPP_DBUS_SKELETON, skeleton,
                      NULL);

        /* If the modem is *only* LTE, we assume that CS network is not
         * supported */
        if (mm_iface_modem_is_3gpp_lte_only (MM_IFACE_MODEM (self))) {
            mm_dbg ("Modem is LTE-only, assuming CS network is not supported");
            g_object_set (self,
                          MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED, FALSE,
                          NULL);
        }
    }

    /* Perform async initialization here */
    interface_initialization_step (initialization_context_new (self,
                                                               cancellable,
                                                               callback,
                                                               user_data));
    g_object_unref (skeleton);
    return;
}

void
mm_iface_modem_3gpp_shutdown (MMIfaceModem3gpp *self)
{
    g_return_if_fail (MM_IS_IFACE_MODEM_3GPP (self));

    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem3gpp (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

static void
iface_modem_3gpp_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_3GPP_DBUS_SKELETON,
                              "3GPP DBus skeleton",
                              "DBus skeleton for the 3GPP interface",
                              MM_GDBUS_TYPE_MODEM3GPP_SKELETON,
                              G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_enum (MM_IFACE_MODEM_3GPP_REGISTRATION_STATE,
                            "RegistrationState",
                            "Registration state of the modem",
                            MM_TYPE_MODEM_3GPP_REGISTRATION_STATE,
                            MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN,
                            G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boolean (MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED,
                               "CS network supported",
                               "Whether the modem works in the CS network",
                               TRUE,
                               G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boolean (MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED,
                               "PS network supported",
                               "Whether the modem works in the PS network",
                               TRUE,
                               G_PARAM_READWRITE));

    initialized = TRUE;
}

GType
mm_iface_modem_3gpp_get_type (void)
{
    static GType iface_modem_3gpp_type = 0;

    if (!G_UNLIKELY (iface_modem_3gpp_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModem3gpp), /* class_size */
            iface_modem_3gpp_init,      /* base_init */
            NULL,                  /* base_finalize */
        };

        iface_modem_3gpp_type = g_type_register_static (G_TYPE_INTERFACE,
                                                        "MMIfaceModem3gpp",
                                                        &info,
                                                        0);

        g_type_interface_add_prerequisite (iface_modem_3gpp_type, MM_TYPE_IFACE_MODEM);
    }

    return iface_modem_3gpp_type;
}
