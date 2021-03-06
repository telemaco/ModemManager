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

#include <string.h>

#include "mm-errors-types.h"
#include "mm-common-helpers.h"
#include "mm-bearer-properties.h"

G_DEFINE_TYPE (MMBearerProperties, mm_bearer_properties, G_TYPE_OBJECT);

#define PROPERTY_APN             "apn"
#define PROPERTY_USER            "user"
#define PROPERTY_PASSWORD        "password"
#define PROPERTY_IP_TYPE         "ip-type"
#define PROPERTY_NUMBER          "number"
#define PROPERTY_ALLOW_ROAMING   "allow-roaming"
#define PROPERTY_RM_PROTOCOL     "rm-protocol"

struct _MMBearerPropertiesPrivate {
    /* APN */
    gchar *apn;
    /* IP type */
    gchar *ip_type;
    /* Number */
    gchar *number;
    /* User */
    gchar *user;
    /* Password */
    gchar *password;
    /* Roaming allowance */
    gboolean allow_roaming_set;
    gboolean allow_roaming;
    /* Protocol of the Rm interface */
    MMModemCdmaRmProtocol rm_protocol;
};

/*****************************************************************************/

void
mm_bearer_properties_set_apn (MMBearerProperties *self,
                              const gchar *apn)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    g_free (self->priv->apn);
    self->priv->apn = g_strdup (apn);
}

void
mm_bearer_properties_set_user (MMBearerProperties *self,
                               const gchar *user)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    g_free (self->priv->user);
    self->priv->user = g_strdup (user);
}

void
mm_bearer_properties_set_password (MMBearerProperties *self,
                                   const gchar *password)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    g_free (self->priv->password);
    self->priv->password = g_strdup (password);
}

void
mm_bearer_properties_set_ip_type (MMBearerProperties *self,
                                  const gchar *ip_type)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    g_free (self->priv->ip_type);
    self->priv->ip_type = g_strdup (ip_type);
}

void
mm_bearer_properties_set_allow_roaming (MMBearerProperties *self,
                                        gboolean allow_roaming)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    self->priv->allow_roaming = allow_roaming;
    self->priv->allow_roaming_set = TRUE;
}

void
mm_bearer_properties_set_number (MMBearerProperties *self,
                                 const gchar *number)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    g_free (self->priv->number);
    self->priv->number = g_strdup (number);
}

void
mm_bearer_properties_set_rm_protocol (MMBearerProperties *self,
                                      MMModemCdmaRmProtocol protocol)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    self->priv->rm_protocol = protocol;
}

/*****************************************************************************/

const gchar *
mm_bearer_properties_get_apn (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    return self->priv->apn;
}

const gchar *
mm_bearer_properties_get_user (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    return self->priv->user;
}

const gchar *
mm_bearer_properties_get_password (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    return self->priv->password;
}

const gchar *
mm_bearer_properties_get_ip_type (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    return self->priv->ip_type;
}

gboolean
mm_bearer_properties_get_allow_roaming (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), FALSE);

    return self->priv->allow_roaming;
}

const gchar *
mm_bearer_properties_get_number (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    return self->priv->number;
}

MMModemCdmaRmProtocol
mm_bearer_properties_get_rm_protocol (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN);

    return self->priv->rm_protocol;
}

/*****************************************************************************/

GVariant *
mm_bearer_properties_get_dictionary (MMBearerProperties *self)
{
    GVariantBuilder builder;

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    if (self->priv->apn)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_APN,
                               g_variant_new_string (self->priv->apn));

    if (self->priv->user)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_USER,
                               g_variant_new_string (self->priv->user));

    if (self->priv->password)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_PASSWORD,
                               g_variant_new_string (self->priv->password));

    if (self->priv->ip_type)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_IP_TYPE,
                               g_variant_new_string (self->priv->ip_type));

    if (self->priv->number)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_NUMBER,
                               g_variant_new_string (self->priv->number));

    if (self->priv->allow_roaming_set)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_ALLOW_ROAMING,
                               g_variant_new_boolean (self->priv->allow_roaming));

    if (self->priv->rm_protocol)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_RM_PROTOCOL,
                               g_variant_new_uint32 (self->priv->rm_protocol));

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

gboolean
mm_bearer_properties_consume_string (MMBearerProperties *self,
                                     const gchar *key,
                                     const gchar *value,
                                     GError **error)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), FALSE);

    if (g_str_equal (key, PROPERTY_APN))
        mm_bearer_properties_set_apn (self, value);
    else if (g_str_equal (key, PROPERTY_USER))
        mm_bearer_properties_set_user (self, value);
    else if (g_str_equal (key, PROPERTY_PASSWORD))
        mm_bearer_properties_set_password (self, value);
    else if (g_str_equal (key, PROPERTY_IP_TYPE))
        mm_bearer_properties_set_ip_type (self, value);
    else if (g_str_equal (key, PROPERTY_ALLOW_ROAMING)) {
        GError *inner_error = NULL;
        gboolean allow_roaming;

        allow_roaming = mm_common_get_boolean_from_string (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }
        mm_bearer_properties_set_allow_roaming (self, allow_roaming);
    } else if (g_str_equal (key, PROPERTY_NUMBER))
        mm_bearer_properties_set_number (self, value);
    else if (g_str_equal (key, PROPERTY_RM_PROTOCOL)) {
        GError *inner_error = NULL;
        MMModemCdmaRmProtocol protocol;

        protocol = mm_common_get_rm_protocol_from_string (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }
        mm_bearer_properties_set_rm_protocol (self, protocol);
    } else {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid properties string, unexpected key '%s'",
                     key);
        return FALSE;
    }

    return TRUE;
}

typedef struct {
    MMBearerProperties *properties;
    GError *error;
} ParseKeyValueContext;

static gboolean
key_value_foreach (const gchar *key,
                   const gchar *value,
                   ParseKeyValueContext *ctx)
{
    return mm_bearer_properties_consume_string (ctx->properties,
                                                key,
                                                value,
                                                &ctx->error);
}

MMBearerProperties *
mm_bearer_properties_new_from_string (const gchar *str,
                                      GError **error)
{
    ParseKeyValueContext ctx;

    ctx.error = NULL;
    ctx.properties = mm_bearer_properties_new ();

    mm_common_parse_key_value_string (str,
                                      &ctx.error,
                                      (MMParseKeyValueForeachFn)key_value_foreach,
                                      &ctx);
    /* If error, destroy the object */
    if (ctx.error) {
        g_propagate_error (error, ctx.error);
        g_object_unref (ctx.properties);
        ctx.properties = NULL;
    }

    return ctx.properties;
}

/*****************************************************************************/

gboolean
mm_bearer_properties_consume_variant (MMBearerProperties *properties,
                                      const gchar *key,
                                      GVariant *value,
                                      GError **error)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (properties), FALSE);

    if (g_str_equal (key, PROPERTY_APN))
        mm_bearer_properties_set_apn (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_USER))
        mm_bearer_properties_set_user (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_PASSWORD))
        mm_bearer_properties_set_password (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_IP_TYPE))
        mm_bearer_properties_set_ip_type (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_NUMBER))
        mm_bearer_properties_set_number (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_ALLOW_ROAMING))
        mm_bearer_properties_set_allow_roaming (
            properties,
            g_variant_get_boolean (value));
    else {
        /* Set error */
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid properties dictionary, unexpected key '%s'",
                     key);
        return FALSE;
    }

    return TRUE;
}

MMBearerProperties *
mm_bearer_properties_new_from_dictionary (GVariant *dictionary,
                                          GError **error)
{
    GError *inner_error = NULL;
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    MMBearerProperties *properties;

    properties = mm_bearer_properties_new ();
    if (!dictionary)
        return properties;

    if (!g_variant_is_of_type (dictionary, G_VARIANT_TYPE ("a{sv}"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create Bearer properties from dictionary: "
                     "invalid variant type received");
        g_object_unref (properties);
        return NULL;
    }

    g_variant_iter_init (&iter, dictionary);
    while (!inner_error &&
           g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        mm_bearer_properties_consume_variant (properties,
                                                     key,
                                                     value,
                                                     &inner_error);
        g_free (key);
        g_variant_unref (value);
    }

    /* If error, destroy the object */
    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_object_unref (properties);
        properties = NULL;
    }

    return properties;
}

/*****************************************************************************/

MMBearerProperties *
mm_bearer_properties_dup (MMBearerProperties *orig)
{
    GVariant *dict;
    MMBearerProperties *copy;
    GError *error = NULL;

    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (orig), NULL);

    dict = mm_bearer_properties_get_dictionary (orig);
    copy = mm_bearer_properties_new_from_dictionary (dict, &error);
    g_assert_no_error (error);
    g_variant_unref (dict);

    return copy;
}

/*****************************************************************************/

MMBearerProperties *
mm_bearer_properties_new (void)
{
    return (MM_BEARER_PROPERTIES (
                g_object_new (MM_TYPE_BEARER_PROPERTIES, NULL)));
}

static void
mm_bearer_properties_init (MMBearerProperties *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BEARER_PROPERTIES,
                                              MMBearerPropertiesPrivate);

    /* Some defaults */
    self->priv->allow_roaming = TRUE;
    self->priv->rm_protocol = MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN;
}

static void
finalize (GObject *object)
{
    MMBearerProperties *self = MM_BEARER_PROPERTIES (object);

    g_free (self->priv->apn);
    g_free (self->priv->user);
    g_free (self->priv->password);
    g_free (self->priv->ip_type);
    g_free (self->priv->number);

    G_OBJECT_CLASS (mm_bearer_properties_parent_class)->finalize (object);
}

static void
mm_bearer_properties_class_init (MMBearerPropertiesClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBearerPropertiesPrivate));

    object_class->finalize = finalize;
}
