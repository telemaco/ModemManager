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
 * Copyright (C) 2011 Red Hat, Inc.
 * Copyright (C) 2012 OpenShine, S.L.
 */

#include <ctype.h>
#include <string.h>

#include <glib.h>

#include "mm-charsets.h"
#include "mm-errors.h"
#include "mm-utils.h"
#include "mm-sms-utils.h"
#include "mm-log.h"
#include "dbus/dbus-glib.h"

#define SMS_TP_MTI_MASK               0x03
#define  SMS_TP_MTI_SMS_DELIVER       0x00
#define  SMS_TP_MTI_SMS_SUBMIT_REPORT 0x01
#define  SMS_TP_MTI_SMS_STATUS_REPORT 0x02

#define SMS_NUMBER_TYPE_MASK          0x70
#define SMS_NUMBER_TYPE_UNKNOWN       0x00
#define SMS_NUMBER_TYPE_INTL          0x10
#define SMS_NUMBER_TYPE_ALPHA         0x50

#define SMS_NUMBER_PLAN_MASK          0x0f
#define SMS_NUMBER_PLAN_TELEPHONE     0x01

#define SMS_TP_MMS                    0x04
#define SMS_TP_SRI                    0x20
#define SMS_TP_UDHI                   0x40
#define SMS_TP_RP                     0x80

#define SMS_DCS_CODING_MASK           0xec
#define  SMS_DCS_CODING_DEFAULT       0x00
#define  SMS_DCS_CODING_8BIT          0x04
#define  SMS_DCS_CODING_UCS2          0x08

#define SMS_DCS_CLASS_VALID           0x10
#define SMS_DCS_CLASS_MASK            0x03

#define SMS_TIMESTAMP_LEN 7
#define SMS_MIN_PDU_LEN (7 + SMS_TIMESTAMP_LEN)

typedef enum {
    MM_SMS_ENCODING_UNKNOWN = 0x0,
    MM_SMS_ENCODING_GSM7,
    MM_SMS_ENCODING_8BIT,
    MM_SMS_ENCODING_UCS2
} SmsEncoding;

static char sms_bcd_chars[] = "0123456789*#abc\0\0";

static void
sms_semi_octets_to_bcd_string (char *dest, const guint8 *octets, int num_octets)
{
    int i;

    for (i = 0 ; i < num_octets; i++) {
        *dest++ = sms_bcd_chars[octets[i] & 0xf];
        *dest++ = sms_bcd_chars[(octets[i] >> 4) & 0xf];
    }
    *dest++ = '\0';
}

static gboolean
char_to_bcd (char in, guint8 *out)
{
    guint32 z;

    if (isdigit (in)) {
        *out = in - 0x30;
        return TRUE;
    }

    for (z = 10; z < 16; z++) {
        if (in == sms_bcd_chars[z]) {
            *out = z;
            return TRUE;
        }
    }
    return FALSE;
}

static gsize
sms_string_to_bcd_semi_octets (guint8 *buf, gsize buflen, const char *string)
{
    guint i;
    guint8 bcd;
    gsize addrlen, slen;

    addrlen = slen = strlen (string);
    if (addrlen % 2)
        addrlen++;
    g_return_val_if_fail (buflen >= addrlen, 0);

    for (i = 0; i < addrlen; i += 2) {
        if (!char_to_bcd (string[i], &bcd))
            return 0;
        buf[i / 2] = bcd & 0xF;

        if (i >= slen - 1) {
            /* PDU address gets padded with 0xF if string is odd length */
            bcd = 0xF;
        } else if (!char_to_bcd (string[i + 1], &bcd))
            return 0;
        buf[i / 2] |= bcd << 4;
    }
    return addrlen / 2;
}

/**
 * sms_encode_address:
 *
 * @address: the phone number to encode
 * @buf: the buffer to encode @address in
 * @buflen: the size  of @buf
 * @is_smsc: if %TRUE encode size as number of octets of address infromation,
 *   otherwise if %FALSE encode size as number of digits of @address
 *
 * Returns: the size in bytes of the data added to @buf
 **/
guint
sms_encode_address (const char *address,
                    guint8 *buf,
                    size_t buflen,
                    gboolean is_smsc)
{
    gsize len;

    g_return_val_if_fail (address != NULL, 0);
    g_return_val_if_fail (buf != NULL, 0);
    g_return_val_if_fail (buflen >= 2, 0);

    /* Handle number type & plan */
    buf[1] = 0x80;  /* Bit 7 always 1 */
    if (address[0] == '+') {
        buf[1] |= SMS_NUMBER_TYPE_INTL;
        address++;
    }
    buf[1] |= SMS_NUMBER_PLAN_TELEPHONE;

    len = sms_string_to_bcd_semi_octets (&buf[2], buflen, address);

    if (is_smsc)
        buf[0] = len + 1;  /* addr length + size byte */
    else
        buf[0] = strlen (address);  /* number of digits in address */

    return len ? len + 2 : 0;  /* addr length + size byte + number type/plan */
}

/* len is in semi-octets */
static char *
sms_decode_address (const guint8 *address, int len)
{
    guint8 addrtype, addrplan;
    char *utf8;

    addrtype = address[0] & SMS_NUMBER_TYPE_MASK;
    addrplan = address[0] & SMS_NUMBER_PLAN_MASK;
    address++;

    if (addrtype == SMS_NUMBER_TYPE_ALPHA) {
        guint8 *unpacked;
        guint32 unpacked_len;
        unpacked = gsm_unpack (address, (len * 4) / 7, 0, &unpacked_len);
        utf8 = (char *)mm_charset_gsm_unpacked_to_utf8 (unpacked,
                                                        unpacked_len);
        g_free(unpacked);
    } else if (addrtype == SMS_NUMBER_TYPE_INTL &&
               addrplan == SMS_NUMBER_PLAN_TELEPHONE) {
        /* International telphone number, format as "+1234567890" */
        utf8 = g_malloc (len + 3); /* '+' + digits + possible trailing 0xf + NUL */
        utf8[0] = '+';
        sms_semi_octets_to_bcd_string (utf8 + 1, address, (len + 1) / 2);
    } else {
        /*
         * All non-alphanumeric types and plans are just digits, but
         * don't apply any special formatting if we don't know the
         * format.
         */
        utf8 = g_malloc (len + 2); /* digits + possible trailing 0xf + NUL */
        sms_semi_octets_to_bcd_string (utf8, address, (len + 1) / 2);
    }

    return utf8;
}


static char *
sms_decode_timestamp (const guint8 *timestamp)
{
    /* YYMMDDHHMMSS+ZZ */
    char *timestr;
    int quarters, hours;

    timestr = g_malloc0 (16);
    sms_semi_octets_to_bcd_string (timestr, timestamp, 6);
    quarters = ((timestamp[6] & 0x7) * 10) + ((timestamp[6] >> 4) & 0xf);
    hours = quarters / 4;
    if (timestamp[6] & 0x08)
        timestr[12] = '-';
    else
        timestr[12] = '+';
    timestr[13] = (hours / 10) + '0';
    timestr[14] = (hours % 10) + '0';
    /* TODO(njw): Change timestamp rep to something that includes quarter-hours */
    return timestr;
}

static SmsEncoding
sms_encoding_type (int dcs)
{
    SmsEncoding scheme = MM_SMS_ENCODING_UNKNOWN;

    switch ((dcs >> 4) & 0xf) {
        /* General data coding group */
        case 0: case 1:
        case 2: case 3:
            switch (dcs & 0x0c) {
                case 0x08:
                    scheme = MM_SMS_ENCODING_UCS2;
                    break;
                case 0x00:
                    /* fallthrough */
                    /* reserved - spec says to treat it as default alphabet */
                case 0x0c:
                    scheme = MM_SMS_ENCODING_GSM7;
                    break;
                case 0x04:
                    scheme = MM_SMS_ENCODING_8BIT;
                    break;
            }
            break;

            /* Message waiting group (default alphabet) */
        case 0xc:
        case 0xd:
            scheme = MM_SMS_ENCODING_GSM7;
            break;

            /* Message waiting group (UCS2 alphabet) */
        case 0xe:
            scheme = MM_SMS_ENCODING_UCS2;
            break;

            /* Data coding/message class group */
        case 0xf:
            switch (dcs & 0x04) {
                case 0x00:
                    scheme = MM_SMS_ENCODING_GSM7;
                    break;
                case 0x04:
                    scheme = MM_SMS_ENCODING_8BIT;
                    break;
            }
            break;

            /* Reserved coding group values - spec says to treat it as default alphabet */
        default:
            scheme = MM_SMS_ENCODING_GSM7;
            break;
    }

    return scheme;

}

static char *
sms_decode_text (const guint8 *text, int len, SmsEncoding encoding, int bit_offset)
{
    char *utf8;
    guint8 *unpacked;
    guint32 unpacked_len;

    if (encoding == MM_SMS_ENCODING_GSM7) {
        unpacked = gsm_unpack ((const guint8 *) text, len, bit_offset, &unpacked_len);
        utf8 = (char *) mm_charset_gsm_unpacked_to_utf8 (unpacked, unpacked_len);
        g_free (unpacked);
    } else if (encoding == MM_SMS_ENCODING_UCS2)
        utf8 = g_convert ((char *) text, len, "UTF8", "UCS-2BE", NULL, NULL, NULL);
    else {
        g_warn_if_reached ();
        utf8 = g_strdup ("");
    }

    return utf8;
}

static void
simple_free_gvalue (gpointer data)
{
    g_value_unset ((GValue *) data);
    g_slice_free (GValue, data);
}



static GValue *
simple_uint_value (guint32 i)
{
    GValue *val;

    val = g_slice_new0 (GValue);
    g_value_init (val, G_TYPE_UINT);
    g_value_set_uint (val, i);

    return val;
}

static GValue *
simple_string_value (const char *str)
{
    GValue *val;

    val = g_slice_new0 (GValue);
    g_value_init (val, G_TYPE_STRING);
    g_value_set_string (val, str);

    return val;
}

static GValue *
byte_array_value (const GByteArray *array)
{
    GValue *val;

    val = g_slice_new0 (GValue);
    g_value_init (val, DBUS_TYPE_G_UCHAR_ARRAY);
    g_value_set_boxed (val, array);

    return val;
}

GHashTable *
sms_properties_hash_new (const char *smsc,
                         const char *number,
                         const char *timestamp,
                         const char *text,
                         const GByteArray *data,
                         guint data_coding_scheme,
                         guint *class)
{
    GHashTable *properties;

    g_return_val_if_fail (number != NULL, NULL);
    g_return_val_if_fail (text != NULL, NULL);
    g_return_val_if_fail (data != NULL, NULL);

    properties = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, simple_free_gvalue);
    g_hash_table_insert (properties, "number", simple_string_value (number));
    g_hash_table_insert (properties, "data", byte_array_value (data));
    g_hash_table_insert (properties, "data-coding-scheme", simple_uint_value (data_coding_scheme));
    g_hash_table_insert (properties, "text", simple_string_value (text));

    if (smsc)
        g_hash_table_insert (properties, "smsc", simple_string_value (smsc));

    if (timestamp)
        g_hash_table_insert (properties, "timestamp", simple_string_value (timestamp));

    if (class)
        g_hash_table_insert (properties, "class", simple_uint_value (*class));

    return properties;
}

GHashTable *
sms_parse_pdu (const char *hexpdu, GError **error)
{
    GHashTable *properties;
    gsize pdu_len;
    guint8 *pdu;
    guint smsc_addr_num_octets, variable_length_items, msg_start_offset,
            sender_addr_num_digits, sender_addr_num_octets,
            tp_pid_offset, tp_dcs_offset, user_data_offset, user_data_len,
            user_data_len_offset, bit_offset;
    char *smsc_addr, *sender_addr, *sc_timestamp, *msg_text;
    SmsEncoding user_data_encoding;
    GByteArray *pdu_data;
    guint concat_ref = 0, concat_max = 0, concat_seq = 0, msg_class = 0;
    gboolean multipart = FALSE, class_valid = FALSE;

    /* Convert PDU from hex to binary */
    pdu = (guint8 *) utils_hexstr2bin (hexpdu, &pdu_len);
    if (!pdu) {
        g_set_error_literal (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                             "Couldn't parse PDU of SMS GET response from hex");
        return NULL;
    }

    /* SMSC, in address format, precedes the TPDU */
    smsc_addr_num_octets = pdu[0];
    variable_length_items = smsc_addr_num_octets;
    if (pdu_len < variable_length_items + SMS_MIN_PDU_LEN) {
        g_set_error (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                     "PDU too short (1): %zd vs %d",
                     pdu_len,
                     variable_length_items + SMS_MIN_PDU_LEN);
        g_free (pdu);
        return NULL;
    }

    /* where in the PDU the actual SMS protocol message begins */
    msg_start_offset = 1 + smsc_addr_num_octets;
    sender_addr_num_digits = pdu[msg_start_offset + 1];
    /*
     * round the sender address length up to an even number of
     * semi-octets, and thus an integral number of octets
     */
    sender_addr_num_octets = (sender_addr_num_digits + 1) >> 1;
    variable_length_items += sender_addr_num_octets;
    if (pdu_len < variable_length_items + SMS_MIN_PDU_LEN) {
        g_set_error (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                     "PDU too short (2): %zd vs %d",
                     pdu_len,
                     variable_length_items + SMS_MIN_PDU_LEN);
        g_free (pdu);
        return NULL;
    }

    tp_pid_offset = msg_start_offset + 3 + sender_addr_num_octets;
    tp_dcs_offset = tp_pid_offset + 1;

    user_data_len_offset = tp_dcs_offset + 1 + SMS_TIMESTAMP_LEN;
    user_data_offset = user_data_len_offset + 1;
    user_data_len = pdu[user_data_len_offset];
    user_data_encoding = sms_encoding_type(pdu[tp_dcs_offset]);
    if (user_data_encoding == MM_SMS_ENCODING_GSM7)
        variable_length_items += (7 * (user_data_len + 1 )) / 8;
    else
        variable_length_items += user_data_len;
    if (pdu_len < variable_length_items + SMS_MIN_PDU_LEN) {
        g_set_error (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                     "PDU too short (3): %zd vs %d",
                     pdu_len,
                     variable_length_items + SMS_MIN_PDU_LEN);
        g_free (pdu);
        return NULL;
    }

    /* Only handle SMS-DELIVER */
    if ((pdu[msg_start_offset] & SMS_TP_MTI_MASK) != SMS_TP_MTI_SMS_DELIVER) {
        g_set_error (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                     "Unhandled message type: 0x%02x",
                     pdu[msg_start_offset]);
        g_free (pdu);
        return NULL;
    }

    bit_offset = 0;
    if (pdu[msg_start_offset] & SMS_TP_UDHI) {
        int udhl, end, offset;
        udhl = pdu[user_data_offset] + 1;
        end = user_data_offset + udhl;

        for (offset = user_data_offset + 1; offset < end;) {
            guint8 ie_id, ie_len;

            ie_id = pdu[offset++];
            ie_len = pdu[offset++];

            switch (ie_id) {
                case 0x00:
                    /*
                     * Ignore the IE if one of the following is true:
                     *  - it claims to be part 0 of M
                     *  - it claims to be part N of M, N > M
                     */
                    if (pdu[offset + 2] == 0 ||
                        pdu[offset + 2] > pdu[offset + 1])
                        break;

                    concat_ref = pdu[offset];
                    concat_max = pdu[offset + 1];
                    concat_seq = pdu[offset + 2];
                    multipart = TRUE;
                    break;
                case 0x08:
                    /* Concatenated short message, 16-bit reference */
                    if (pdu[offset + 3] == 0 ||
                        pdu[offset + 3] > pdu[offset + 2])
                        break;

                    concat_ref = (pdu[offset] << 8) | pdu[offset + 1];
                    concat_max = pdu[offset + 2];
                    concat_seq = pdu[offset + 3];
                    multipart = TRUE;
                    break;
            }

            offset += ie_len;
        }

        /*
         * Move past the user data headers to prevent it from being
         * decoded into garbage text.
         */
        user_data_offset += udhl;
        if (user_data_encoding == MM_SMS_ENCODING_GSM7) {
            /*
             * Find the number of bits we need to add to the length of the
             * user data to get a multiple of 7 (the padding).
             */
            bit_offset = (7 - udhl % 7) % 7;
            user_data_len -= (udhl * 8 + bit_offset) / 7;
        } else
            user_data_len -= udhl;
    }

    if (   user_data_encoding == MM_SMS_ENCODING_8BIT
        || user_data_encoding == MM_SMS_ENCODING_UNKNOWN) {
        /* 8-bit encoding is usually binary data, and we have no idea what
         * actual encoding the data is in so we can't convert it.
         */
        msg_text = g_strdup ("");
    } else {
        /* Otherwise if it's 7-bit or UCS2 we can decode it */
        msg_text = sms_decode_text (&pdu[user_data_offset], user_data_len,
                                    user_data_encoding, bit_offset);
        g_warn_if_fail (msg_text != NULL);
    }

    /* Raw PDU data */
    pdu_data = g_byte_array_sized_new (user_data_len);
    g_byte_array_append (pdu_data, &pdu[user_data_offset], user_data_len);

    if (pdu[tp_dcs_offset] & SMS_DCS_CLASS_VALID) {
        msg_class = pdu[tp_dcs_offset] & SMS_DCS_CLASS_MASK;
        class_valid = TRUE;
    }

    smsc_addr = sms_decode_address (&pdu[1], 2 * (pdu[0] - 1));
    sender_addr = sms_decode_address (&pdu[msg_start_offset + 2], pdu[msg_start_offset + 1]);
    sc_timestamp = sms_decode_timestamp (&pdu[tp_dcs_offset + 1]);

    properties = sms_properties_hash_new (smsc_addr,
                                          sender_addr,
                                          sc_timestamp,
                                          msg_text,
                                          pdu_data,
                                          pdu[tp_dcs_offset] & 0xFF,
                                          class_valid ? &msg_class : NULL);
    g_assert (properties);
    if (multipart) {
        g_hash_table_insert (properties, "concat-reference", simple_uint_value (concat_ref));
        g_hash_table_insert (properties, "concat-max", simple_uint_value (concat_max));
        g_hash_table_insert (properties, "concat-sequence", simple_uint_value (concat_seq));
    }

    g_free (smsc_addr);
    g_free (sender_addr);
    g_free (sc_timestamp);
    g_free (msg_text);
    g_byte_array_free (pdu_data, TRUE);
    g_free (pdu);

    return properties;
}

static guint8
validity_to_relative (guint validity)
{
    if (validity == 0)
        return 167; /* 24 hours */

    if (validity <= 720) {
        /* 5 minute units up to 12 hours */
        if (validity % 5)
            validity += 5;
        return (validity / 5) - 1;
    }

    if (validity > 720 && validity <= 1440) {
        /* 12 hours + 30 minute units up to 1 day */
        if (validity % 30)
            validity += 30;  /* round up to next 30 minutes */
        validity = MIN (validity, 1440);
        return 143 + ((validity - 720) / 30);
    }

    if (validity > 1440 && validity <= 43200) {
        /* 2 days up to 1 month */
        if (validity % 1440)
            validity += 1440;  /* round up to next day */
        validity = MIN (validity, 43200);
        return 167 + ((validity - 1440) / 1440);
    }

    /* 43200 = 30 days in minutes
     * 10080 = 7 days in minutes
     * 635040 = 63 weeks in minutes
     * 40320 = 4 weeks in minutes
     */
    if (validity > 43200 && validity <= 635040) {
        /* 5 weeks up to 63 weeks */
        if (validity % 10080)
            validity += 10080;  /* round up to next week */
        validity = MIN (validity, 635040);
        return 196 + ((validity - 40320) / 10080);
    }

    return 255; /* 63 weeks */
}

#define PDU_SIZE 200


static GByteArray *
sms_get_smsc_pdu (const char* smsc, 
                  GError  **error) 
{
    GByteArray *result = NULL;
    guint len, offset = 0;
    guint8 *pdu;
    
    pdu = g_malloc0(100);
    
    if (smsc) {
        len = sms_encode_address (smsc, pdu, 100, TRUE);
        if (len == 0) {
            g_set_error (error,
                         MM_MSG_ERROR,
                         MM_MSG_ERROR_INVALID_PDU_PARAMETER,
                         "Invalid SMSC address '%s'", smsc);
            g_free(pdu);
            return NULL;
        }
        offset += len;
    } else {
        /* No SMSC, use default */
        pdu[offset++] = 0x00;
    }
    result = g_byte_array_new ();
    result = g_byte_array_append (result, (guint8*) pdu, offset);
    g_free(pdu);
    return result;
}

static GByteArray *
sms_get_submit_data_pdu (gboolean request_status,
                         guint validity,
                         gboolean udh)
{
    GByteArray *result = NULL;
    guint offset = 0;
    guint8 *pdu;

    
    pdu = g_malloc0 (10);
    if (validity > 0)
        pdu[offset] = 1 << 4; /* TP-VP present; format RELATIVE */
    else
        pdu[offset] = 0;      /* TP-VP not present */

    if (udh == TRUE)
        pdu[offset] |= 0x40;

    if (request_status == FALSE)
        pdu[offset++] |= 0x01;  /* TP-MTI = SMS-SUBMIT */
    else
        pdu[offset++] |= 0x21;
    
    pdu[offset++] = 0x00;     /* TP-Message-Reference: filled by device */
        
    result = g_byte_array_new ();
    result = g_byte_array_append (result, (guint8*) pdu, offset);
    g_free (pdu);
    return result;
}

static GByteArray *
sms_get_address_pdu (const char *number,
                     GError **error)
{
    GByteArray *result = NULL;
    guint len, offset = 0;
    guint8 *pdu;

    pdu = g_malloc0 (100);

    len = sms_encode_address (number, pdu, 100, FALSE);
    if (len == 0) {
        g_set_error (error,
                     MM_MSG_ERROR,
                     MM_MSG_ERROR_INVALID_PDU_PARAMETER,
                     "Invalid send-to number '%s'", number);
        g_free(pdu);
        return NULL;
    }
    offset += len;
    
    result = g_byte_array_new ();
    result = g_byte_array_append (result, (guint8*) pdu, offset);
    g_free (pdu);
    return result;
}

static GByteArray *
sms_get_tppid_pdu (void)
{
    GByteArray *result = NULL;
    guint8 b ;

    b = 0x00;
    result = g_byte_array_new ();
    result = g_byte_array_append (result, &b, 1);
    
    return result;
}

static GByteArray *
sms_pack_msg_pdu_with_gsm7_format (const char *text,
                                   const guint textlen,
                                   GByteArray *header,
                                   GByteArray *udh,
                                   GError **error)
{
    GByteArray *msg_pdu;
    guint8 *unpacked, *packed, tlen;
    guint32 unlen = 0, packlen = 0;

    unpacked = mm_charset_utf8_to_unpacked_gsm (text, &unlen);
    if (!unpacked || unlen == 0) {
        g_free (unpacked);
        g_set_error_literal (error,
                             MM_MSG_ERROR,
                             MM_MSG_ERROR_INVALID_PDU_PARAMETER,
                             "Failed to convert message text to GSM.");
        return NULL;
    }
    
    packed = gsm_pack (unpacked, unlen, 0, &packlen);
    g_free (unpacked);
    if (!packed || packlen == 0) {
        g_free (packed);
        g_set_error_literal (error,
                             MM_MSG_ERROR,
                             MM_MSG_ERROR_INVALID_PDU_PARAMETER,
                             "Failed to pack message text to GSM.");
        return NULL;
    }
    
    msg_pdu = g_byte_array_new ();
    msg_pdu = g_byte_array_append (msg_pdu, (guint8*) packed, packlen);
    
    if (udh != NULL) {      
        msg_pdu = g_byte_array_remove_range (msg_pdu, 0, udh->len);
        msg_pdu = g_byte_array_prepend (msg_pdu, (guint8*) udh->data, udh->len);
        
        tlen = unlen -7 + udh->len + 1;
        msg_pdu = g_byte_array_prepend (msg_pdu, &tlen, 1);
    }
    
    msg_pdu = g_byte_array_prepend (msg_pdu, header->data, header->len);
    
    g_free (packed);

    return msg_pdu;
}

static GByteArray *
sms_pack_msg_pdu_with_ucs2_format (const char *text,
                                   const guint textlen,
                                   GByteArray *header,
                                   GByteArray *udh,
                                   GError **error)
{
    GByteArray *msg_pdu;
    guint8 tlen;
    
    msg_pdu = g_byte_array_sized_new (textlen/2);
    if (!mm_modem_charset_byte_array_append (msg_pdu, text, FALSE, MM_MODEM_CHARSET_UCS2)) {
        g_byte_array_free (msg_pdu, TRUE);
        g_set_error_literal (error,
                             MM_MSG_ERROR,
                             MM_MSG_ERROR_INVALID_PDU_PARAMETER,
                             "Failed to convert message text to UCS2.");
        return NULL;
    }
    
    
    if (udh != NULL){
        tlen = (textlen + 3) * 2;
        
        msg_pdu = g_byte_array_prepend (msg_pdu, (guint8*) udh->data, udh->len);
        msg_pdu = g_byte_array_prepend (msg_pdu, &tlen, 1);
    }
    
    msg_pdu = g_byte_array_prepend (msg_pdu, header->data, header->len); 
 
    return msg_pdu;
}
static GList *
sms_get_multipart_pdu(const char *text,
                      const guint textlen,
                      MMModemCharset cs,
                      GByteArray *header,
                      GError **error)
{
    GList *msgs = NULL, *msg_pdus = NULL, *tmpl = NULL;
    guint len_without_udh;
    guint ps, pe;
    guint8 udh_len, mid, data_len, csms_ref, total_parts, i;

    if (cs == MM_MODEM_CHARSET_GSM)
        len_without_udh = 160 - 7;
    else
        len_without_udh = 70 - 3;
    
    ps = 0;
    pe = len_without_udh;
    
    while ( ps < textlen ) {
        GString *msg_str;
        
        msg_str = g_string_new_len (text+ps, pe-ps);
        msgs = g_list_append (msgs, msg_str); 
        ps = pe;
        if (pe + len_without_udh < textlen)
            pe += len_without_udh;
        else
            pe = textlen;
    }
    
    i = 1;
    udh_len = 0x05;
    mid = 0x00;
    data_len = 0x03;
    csms_ref = g_random_int_range (1,255);
    total_parts = g_list_length (msgs);

    tmpl = msgs;
    while (tmpl != NULL){
        GByteArray *udh;
        GByteArray *pdu;
        GString *msg;

        udh = g_byte_array_new ();
        udh = g_byte_array_append (udh, &udh_len, 1);
        udh = g_byte_array_append (udh, &mid, 1);
        udh = g_byte_array_append (udh, &data_len, 1);
        udh = g_byte_array_append (udh, &csms_ref, 1);
        udh = g_byte_array_append (udh, &total_parts, 1);
        udh = g_byte_array_append (udh, &i, 1);
        
        
        if (cs == MM_MODEM_CHARSET_GSM) {           
            msg = tmpl->data;
            msg = g_string_prepend (msg, "       "); //UDH Padding 
            
            pdu = sms_pack_msg_pdu_with_gsm7_format(msg->str, msg->len, header, udh, error);
            
            if (pdu != NULL)
                msg_pdus = g_list_append(msg_pdus, pdu);
        }else{
            msg = tmpl->data;
            
            pdu = sms_pack_msg_pdu_with_ucs2_format (msg->str, msg->len, header, udh, error);
            
            if (pdu != NULL)
                msg_pdus = g_list_append (msg_pdus, pdu);
        }
        
        g_byte_array_free (udh, TRUE);
        tmpl = tmpl->next;
        i++;
    }
    
    return msg_pdus; 
}

static GList *
sms_get_msg_pdu_list(const char *text,
                     guint validity,
                     GError **error)
{
    GList *result = NULL;
    MMModemCharset best_cs = MM_MODEM_CHARSET_GSM;
    guint ucs2len = 0, gsm_unsupported = 0;
    guint textlen = 0;
    
    GByteArray *header;
    
    header = g_byte_array_new ();
    
    
    textlen = mm_charset_get_encoded_len (text, MM_MODEM_CHARSET_GSM, &gsm_unsupported);
    if (gsm_unsupported > 0) {
        ucs2len = mm_charset_get_encoded_len (text, MM_MODEM_CHARSET_UCS2, NULL);
        best_cs = MM_MODEM_CHARSET_UCS2;
        textlen = ucs2len;
    }
    
    if (best_cs == MM_MODEM_CHARSET_UCS2) {
        guint8 b = 0x08; 
        header = g_byte_array_append (header, &b, 1);
    }else{
        guint8 b = 0x00; 
        header = g_byte_array_append (header, &b, 1);
    }
    
    if (validity > 0){
        guint8 v;
        v = validity_to_relative (validity); 
        header = g_byte_array_append (header, &v, 1);
    }
    
    if ((best_cs == MM_MODEM_CHARSET_GSM  && textlen <= 160) ||
        (best_cs == MM_MODEM_CHARSET_UCS2 && textlen <= 140)) {
        guint8 len_uint8;
        
        len_uint8 = textlen;
        header = g_byte_array_append (header, &len_uint8, 1);
    }

    if (best_cs == MM_MODEM_CHARSET_GSM) {
        if (textlen <= 160) {
            GByteArray *msg_pdu = NULL;
            
            msg_pdu = sms_pack_msg_pdu_with_gsm7_format (text, textlen, header, NULL, error);
            result = g_list_append (result, msg_pdu);
        }else{
            result = sms_get_multipart_pdu (text, textlen, MM_MODEM_CHARSET_GSM, header, error);
        }
    }else{
        if (textlen <= 140) {
            GByteArray *msg_pdu = NULL;
            
            msg_pdu = sms_pack_msg_pdu_with_ucs2_format (text, textlen, header, NULL, error);
            result = g_list_append (result, msg_pdu);
            
        }else{
            //FIXME : mm_charset_get_encoded_len (text, MM_MODEM_CHARSET_UCS2) not return the correct textlen
            result = sms_get_multipart_pdu (text, strlen(text), MM_MODEM_CHARSET_UCS2, header, error);
        }
    }

    g_byte_array_free (header, TRUE);
    return result;
}

static void
sms_pdu_data_free (gpointer element){
    SmsPDUData *pdu_data;
    
    pdu_data = (SmsPDUData *) element;
    g_byte_array_free (pdu_data->pdu, TRUE);
    g_free (pdu_data);
}

/**
 * sms_create_submit_pdu_array:
 *
 * @number: the subscriber number to send this message to
 * @text: the body of this SMS
 * @smsc: if given, the SMSC address
 * @validity: minutes until the SMS should expire in the SMSC, or 0 for a
 *  suitable default
 * @class: unused
 * @request_status: TRUE if you need confirmation from SMSC of the sms sent.
 * @error: on error, filled with the error that occurred
 *
 * Constructs a  SMS message with the given details, preferring to
 * use the UCS2 character set when the message will fit, otherwise falling back
 * to the GSM character set.
 *
 * Returns: an array of SmsPDUData on success, or %NULL on error
 **/
GPtrArray *
sms_create_submit_pdu_array (const char *number,
                             const char *text,
                             const char *smsc,
                             guint validity,
                             guint class,
                             gboolean request_status,
                             GError **error)
{
    GPtrArray *result = NULL;
    GByteArray *smsc_pdu = NULL;
    GByteArray *submit_data_pdu = NULL;
    GByteArray *address_pdu = NULL;
    GByteArray *tppid_pdu = NULL;
    GList *sms_msg_pdu = NULL;
    GList *msg_iter = NULL;


    smsc_pdu = sms_get_smsc_pdu (smsc, error);
    address_pdu = sms_get_address_pdu (number, error);
    tppid_pdu = sms_get_tppid_pdu ();
    sms_msg_pdu = sms_get_msg_pdu_list (text, validity, error);

    if (sms_msg_pdu != NULL && smsc_pdu != NULL &&
        address_pdu != NULL && tppid_pdu != NULL) {

        for (msg_iter=sms_msg_pdu; msg_iter != NULL; msg_iter = msg_iter->next) {
            GByteArray *pdu, *msg_pdu;
            SmsPDUData *pdu_data = NULL;
            gboolean require_udh;

            if (g_list_length (sms_msg_pdu) == 1)
                require_udh = FALSE;
            else
                require_udh = TRUE;

            if (msg_iter->next == NULL) {
                submit_data_pdu = sms_get_submit_data_pdu (request_status, validity, require_udh);
            } else {
                submit_data_pdu = sms_get_submit_data_pdu (FALSE, validity, require_udh);
            }
            
            if (submit_data_pdu == NULL)
                goto final;

            msg_pdu = msg_iter->data;
                
            pdu = g_byte_array_new ();
            pdu = g_byte_array_append (pdu, (guint8*) smsc_pdu->data, smsc_pdu->len);
            pdu = g_byte_array_append (pdu, (guint8*) submit_data_pdu->data, submit_data_pdu->len);
            pdu = g_byte_array_append (pdu, (guint8*) address_pdu->data, address_pdu->len);
            pdu = g_byte_array_append (pdu, (guint8*) tppid_pdu->data, tppid_pdu->len);
            pdu = g_byte_array_append (pdu, (guint8*) msg_pdu->data, msg_pdu->len);

            /* g_print("smsc: %s\n", utils_bin2hexstr (smsc_pdu->data, smsc_pdu->len)); */
            /* g_print("submit: %s\n", utils_bin2hexstr (submit_data_pdu->data, submit_data_pdu->len)); */
            /* g_print("address: %s\n", utils_bin2hexstr (address_pdu->data, address_pdu->len)); */
            /* g_print("tppid: %s\n", utils_bin2hexstr (tppid_pdu->data, tppid_pdu->len)); */
            /* g_print("msg: %s\n", utils_bin2hexstr (msg_pdu->data, msg_pdu->len)); */

            pdu_data = g_new (SmsPDUData, 1);
            pdu_data->pdu = pdu;
            pdu_data->msg_start = smsc_pdu->len ;
                
            if (result == NULL)
                result = g_ptr_array_new_with_free_func (sms_pdu_data_free);
            
            g_ptr_array_add (result, pdu_data);
        }
    }
 
 final:
    if (smsc_pdu != NULL)
        g_byte_array_free (smsc_pdu, TRUE);
    if (submit_data_pdu != NULL)
        g_byte_array_free (submit_data_pdu, TRUE);
    if (address_pdu != NULL)
        g_byte_array_free (address_pdu, TRUE);
    if (tppid_pdu != NULL)
        g_byte_array_free (tppid_pdu, TRUE);
    if (sms_msg_pdu != NULL)
        g_list_free_full (sms_msg_pdu, (GDestroyNotify) g_byte_array_unref);
    
    return result;
}
