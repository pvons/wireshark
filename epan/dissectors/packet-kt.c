/* packet-kt.c
 *
 * Routines for Kyoto Tycoon Version 1 binary protocol dissection
 * Copyright 2013, Abhik Sarkar <sarkar.abhik@gmail.com>
 *
 * http://fallabs.com/kyototycoon/spex.html#protocol
 * (Section "Binary Protocol")
 *
 * $Id: packet-kt.c 53495 2013-11-21 22:30:55Z martink $
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * Adapted from packet-bzr.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib.h>
#include <time.h>
#include <epan/packet.h>
#include <epan/prefs.h>


void proto_register_kt(void);
void proto_reg_handoff_kt(void);

static int proto_kt = -1;

/*
 * A few notes before we get into the thick of things...
 *
 * Note 1 (Abhik):
 * ============================
 * While this is probably a very efficient protocol for the purpose
 * for which it has been written, the way it has been written makes
 * dissection a bit tricky. Requests and responses have the same
 * "magic" identifier, but there is no clear cut way to distinguish
 * between them. This means that a few dirty tricks have have to be
 * employed for dissecting... and the dissector is based on sample
 * captures from two different clients working with the same version
 * of the server.
 * It is possible that the dissection will break under other conditions.
 * Hopefully, this can be fixed/improved with time.
 *
 * Note 2 (Abhik):
 * ============================
 * There are three fields which use 64-bit time stamps. Based on what
 * I can see from the sample traces I have, the value in each is
 * different. I don't know if this is something specific to the
 * version of the client and server I have, or this is how the
 * implementation is... however, the difference is not apparent
 * (at least to me) from the protocol specifications.
 * So, this note is to clarify what I have found:
 * - The timestamp (ts) in the replication requests is nanoseconds
 *   since epoch.
 * - The timestamp (xt) in the set_bulk records is the number of
 *   seconds the record must live.
 * - The timestamp (xt) in the get_bulk output records is the
 *   seconds since epoch.
 *
 * TODO: Support for reassembly of a request or response segmented over
 * multiple frames needs to be added.
 * A single frame containing multiple requests/responses seems unlikely
 * due to the fact that there is no identifier for matching a request
 * and a response. Tests suggest that the communication is synchronous.
*/

static dissector_handle_t kt_handle;

/* Sub-trees */
static gint ett_kt = -1;
static gint ett_kt_rec = -1;

/* Header fields */
static gint hf_kt_magic = -1;
static gint hf_kt_type = -1;
static gint hf_kt_ts = -1;
static gint hf_kt_flags = -1;
static gint hf_kt_rnum = -1;
static gint hf_kt_dbidx = -1;
static gint hf_kt_sid= -1;
static gint hf_kt_xt = -1;
static gint hf_kt_xt_resp = -1;
static gint hf_kt_ksiz = -1;
static gint hf_kt_vsiz = -1;
static gint hf_kt_key = -1;
static gint hf_kt_val = -1;
static gint hf_kt_key_str = -1;
static gint hf_kt_val_str = -1;
static gint hf_kt_hits = -1;
static gint hf_kt_nsiz = -1;
static gint hf_kt_name = -1;
static gint hf_kt_size = -1;
static gint hf_kt_log = -1;
static gint hf_kt_rec = -1;

/* Magic Values */
#define KT_MAGIC_REPL_WAIT      0xB0
#define KT_MAGIC_REPLICATION    0xB1
#define KT_MAGIC_PLAY_SCRIPT    0xB4
#define KT_MAGIC_SET_BULK       0xB8
#define KT_MAGIC_REMOVE_BULK    0xB9
#define KT_MAGIC_GET_BULK       0xBA
#define KT_MAGIC_ERROR          0xBF

static const value_string kt_magic_vals[] = {
    {KT_MAGIC_REPL_WAIT,   "replication - waiting for updates"},
    {KT_MAGIC_REPLICATION, "replication"},
    {KT_MAGIC_PLAY_SCRIPT, "play_script"},
    {KT_MAGIC_SET_BULK,    "set_bulk"   },
    {KT_MAGIC_REMOVE_BULK, "remove_bulk"},
    {KT_MAGIC_GET_BULK,    "get_bulk"   },
    {KT_MAGIC_ERROR,       "error"      },
    {0, NULL}
};

/* Operation type (determined/generated by the dissector) */
#define KT_OPER_REQUEST  0x00
#define KT_OPER_RESPONSE 0x01

static const value_string kt_oper_vals[] = {
    {KT_OPER_REQUEST,   "request"},
    {KT_OPER_RESPONSE,  "response"},
    {0, NULL}
};

/* Preferences */
/*
 * The default port numbers are not IANA registered but used by the
 * default configuration of the KT server all the same.
 */
#define DEFAULT_KT_PORT_RANGE "1978-1979"
static range_t *global_kt_tcp_port_range;
static gboolean kt_present_key_val_as_ascii;

/* Dissection routines */
static int
dissect_kt_replication_wait(tvbuff_t *tvb, proto_tree *tree, gint offset)
{
    gint new_offset;
    guint64 ts;
    nstime_t ns_ts;

    new_offset = offset;

    proto_tree_add_item(tree, hf_kt_magic, tvb, new_offset, 1, ENC_BIG_ENDIAN);
    new_offset++;

    ts = tvb_get_ntoh64(tvb, new_offset);
    ns_ts.secs = (time_t)(ts/1000000000);
    ns_ts.nsecs = (int)(ts%1000000000);
    proto_tree_add_time(tree, hf_kt_ts, tvb, new_offset, 8, &ns_ts);
    new_offset += 8;

    return new_offset;
}

static int
dissect_kt_replication(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, gint offset)
{
    gint new_offset;
    guint32 next32, size;
    guint64 ts;
    nstime_t ns_ts;
    proto_item *pi;

    new_offset = offset;

    proto_tree_add_item(tree, hf_kt_magic, tvb, new_offset, 1, ENC_BIG_ENDIAN);
    new_offset++;

    if (tvb_reported_length_remaining(tvb, new_offset) > 0) {
        next32 = tvb_get_ntohl(tvb, new_offset);
        if (next32 <= 1) { /* This means request. the 32 bits are flags */
            proto_tree_add_item(tree, hf_kt_flags, tvb, new_offset, 4, ENC_BIG_ENDIAN);
            new_offset += 4;

            proto_tree_add_item(tree, hf_kt_ts, tvb, new_offset, 8, ENC_BIG_ENDIAN);
            new_offset += 8;

            proto_tree_add_item(tree, hf_kt_sid, tvb, new_offset, 2, ENC_BIG_ENDIAN);
            new_offset += 2;
        } else { /* This is a response. The 32 bits are the first half of the ts */
            ts = tvb_get_ntoh64(tvb, new_offset);
            ns_ts.secs = (time_t)(ts/1000000000);
            ns_ts.nsecs = (int)(ts%1000000000);
            proto_tree_add_time(tree, hf_kt_ts, tvb, new_offset, 8, &ns_ts);
            new_offset += 8;

            size = tvb_get_ntohl(tvb, new_offset);
            proto_tree_add_uint(tree, hf_kt_size, tvb, new_offset, 4, size);
            new_offset += 4;

            proto_tree_add_item(tree, hf_kt_log, tvb, new_offset, size, ENC_NA);
            new_offset += size;
        }
    } else {
        /* This is an empty ack to the message with magic 0xB0. */
        pi = proto_tree_add_uint(tree, hf_kt_type, tvb, offset, 1, KT_OPER_RESPONSE);
        PROTO_ITEM_SET_GENERATED(pi);
        col_append_sep_str(pinfo->cinfo, COL_INFO, " ", "[response]");
    }

    return new_offset;
}

static int
dissect_kt_set_bulk(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, gint offset)
{
    guint32 next32, rnum, ksiz, vsiz;
    gint new_offset, rec_start_offset;
    proto_item *ti;
    proto_item *pi;
    proto_tree *rec_tree;

    new_offset = offset;

    proto_tree_add_item(tree, hf_kt_magic, tvb, new_offset, 1, ENC_BIG_ENDIAN);
    new_offset++;

    next32 = tvb_get_ntohl(tvb, new_offset);

    if (tvb_reported_length_remaining(tvb, (new_offset + 4)) > 0) {
        /* There's more data after the 32 bits. This is a request */
        pi = proto_tree_add_uint(tree, hf_kt_type, tvb, offset, 1, KT_OPER_REQUEST);
        PROTO_ITEM_SET_GENERATED(pi);

        proto_tree_add_uint(tree, hf_kt_flags, tvb, new_offset, 4, next32);
        new_offset += 4;

        rnum = tvb_get_ntohl(tvb, new_offset);
        proto_tree_add_uint(tree, hf_kt_rnum, tvb, new_offset, 4, rnum);
        new_offset += 4;

        while (rnum > 0) {
            /* Create a sub-tree for each record */
            ti = proto_tree_add_item(tree, hf_kt_rec, tvb, new_offset, -1, ENC_NA);
            rec_tree = proto_item_add_subtree(ti, ett_kt_rec);
            rec_start_offset = new_offset;

            proto_tree_add_item(rec_tree, hf_kt_dbidx, tvb, new_offset, 2, ENC_BIG_ENDIAN);
            new_offset += 2;

            ksiz = tvb_get_ntohl(tvb, new_offset);
            proto_tree_add_uint(rec_tree, hf_kt_ksiz, tvb, new_offset, 4, ksiz);
            new_offset += 4;

            vsiz = tvb_get_ntohl(tvb, new_offset);
            proto_tree_add_uint(rec_tree, hf_kt_vsiz, tvb, new_offset, 4, vsiz);
            new_offset += 4;

            proto_tree_add_item(rec_tree, hf_kt_xt, tvb, new_offset, 8, ENC_BIG_ENDIAN);
            new_offset += 8;

            proto_tree_add_item(rec_tree, hf_kt_key, tvb, new_offset, ksiz, ENC_NA);
            if (kt_present_key_val_as_ascii) {
                pi = proto_tree_add_item(rec_tree, hf_kt_key_str, tvb, new_offset, ksiz, ENC_ASCII|ENC_NA);
                PROTO_ITEM_SET_GENERATED(pi);
            }
            new_offset += ksiz;

            proto_tree_add_item(rec_tree, hf_kt_val, tvb, new_offset, vsiz, ENC_NA);
            if (kt_present_key_val_as_ascii) {
                pi = proto_tree_add_item(rec_tree, hf_kt_val_str, tvb, new_offset, vsiz, ENC_ASCII|ENC_NA);
                PROTO_ITEM_SET_GENERATED(pi);
            }
            new_offset += vsiz;

            proto_item_set_len(ti, new_offset - rec_start_offset);
            rnum--;
        }
    } else {
        /* Nothing remaining after the 32 bits. This is a response. */
        pi = proto_tree_add_uint(tree, hf_kt_type, tvb, offset, 1, KT_OPER_RESPONSE);
        PROTO_ITEM_SET_GENERATED(pi);
        col_append_sep_str(pinfo->cinfo, COL_INFO, " ", "[response]");

        proto_tree_add_uint(tree, hf_kt_hits, tvb, new_offset, 4, next32);
        new_offset += 4;
    }

    return new_offset;
}

static int
dissect_kt_play_script(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, gint offset)
{
    guint32 next32, rnum, ksiz, vsiz, nsiz;
    gint new_offset, rec_start_offset;
    proto_item *ti;
    proto_item *pi;
    proto_tree *rec_tree;

    new_offset = offset;

    proto_tree_add_item(tree, hf_kt_magic, tvb, new_offset, 1, ENC_BIG_ENDIAN);
    new_offset++;

    next32 = tvb_get_ntohl(tvb, new_offset);

    if (next32 == 0) {
        if (tvb_reported_length_remaining(tvb, (new_offset + 4)) > 0) {
            /* There's more data after the 32 bits. This is a request */
            pi = proto_tree_add_uint(tree, hf_kt_type, tvb, offset, 1, KT_OPER_REQUEST);
            PROTO_ITEM_SET_GENERATED(pi);

            proto_tree_add_uint(tree, hf_kt_flags, tvb, new_offset, 4, next32);
            new_offset += 4;

            nsiz = tvb_get_ntohl(tvb, new_offset);
            proto_tree_add_uint(tree, hf_kt_nsiz, tvb, new_offset, 4, nsiz);
            new_offset += 4;

            rnum = tvb_get_ntohl(tvb, new_offset);
            proto_tree_add_uint(tree, hf_kt_rnum, tvb, new_offset, 4, rnum);
            new_offset += 4;

            proto_tree_add_item(tree, hf_kt_name, tvb, new_offset, nsiz, ENC_ASCII|ENC_NA);
            new_offset += nsiz;

            while (rnum > 0) {
                /* Create a sub-tree for each record */
                ti = proto_tree_add_item(tree, hf_kt_rec, tvb, new_offset, -1, ENC_NA);
                rec_tree = proto_item_add_subtree(ti, ett_kt_rec);
                rec_start_offset = new_offset;

                ksiz = tvb_get_ntohl(tvb, new_offset);
                proto_tree_add_uint(rec_tree, hf_kt_ksiz, tvb, new_offset, 4, ksiz);
                new_offset += 4;

                vsiz = tvb_get_ntohl(tvb, new_offset);
                proto_tree_add_uint(rec_tree, hf_kt_vsiz, tvb, new_offset, 4, vsiz);
                new_offset += 4;

                proto_tree_add_item(rec_tree, hf_kt_key, tvb, new_offset, ksiz, ENC_NA);
                if (kt_present_key_val_as_ascii) {
                    pi = proto_tree_add_item(rec_tree, hf_kt_key_str, tvb, new_offset, ksiz, ENC_ASCII|ENC_NA);
                    PROTO_ITEM_SET_GENERATED(pi);
                }
                new_offset += ksiz;

                proto_tree_add_item(rec_tree, hf_kt_val, tvb, new_offset, vsiz, ENC_NA);
                if (kt_present_key_val_as_ascii) {
                    pi = proto_tree_add_item(rec_tree, hf_kt_val_str, tvb, new_offset, vsiz, ENC_ASCII|ENC_NA);
                    PROTO_ITEM_SET_GENERATED(pi);
                }
                new_offset += vsiz;

                proto_item_set_len(ti, new_offset - rec_start_offset);
                rnum--;
            }
        } else {
            /* Nothing remaining after the 32 bits. This is a response with no records. */
            pi = proto_tree_add_uint(tree, hf_kt_type, tvb, offset, 1, KT_OPER_RESPONSE);
            PROTO_ITEM_SET_GENERATED(pi);
            col_append_sep_str(pinfo->cinfo, COL_INFO, " ", "[response]");

            proto_tree_add_uint(tree, hf_kt_rnum, tvb, new_offset, 4, next32);
            new_offset += 4;
        }
    } else { /* response - one or more records */
        pi = proto_tree_add_uint(tree, hf_kt_type, tvb, offset, 1, KT_OPER_RESPONSE);
        PROTO_ITEM_SET_GENERATED(pi);
        col_append_sep_str(pinfo->cinfo, COL_INFO, " ", "[response]");

        rnum = tvb_get_ntohl(tvb, new_offset);
        proto_tree_add_uint(tree, hf_kt_hits, tvb, new_offset, 4, rnum);
        new_offset += 4;

        while (rnum > 0) {
            /* Create a sub-tree for each record */
            ti = proto_tree_add_item(tree, hf_kt_rec, tvb, new_offset, -1, ENC_NA);
            rec_tree = proto_item_add_subtree(ti, ett_kt_rec);
            rec_start_offset = new_offset;

            ksiz = tvb_get_ntohl(tvb, new_offset);
            proto_tree_add_uint(rec_tree, hf_kt_ksiz, tvb, new_offset, 4, ksiz);
            new_offset += 4;

            vsiz = tvb_get_ntohl(tvb, new_offset);
            proto_tree_add_uint(rec_tree, hf_kt_vsiz, tvb, new_offset, 4, vsiz);
            new_offset += 4;

            proto_tree_add_item(rec_tree, hf_kt_key, tvb, new_offset, ksiz, ENC_NA);
            if (kt_present_key_val_as_ascii) {
                pi = proto_tree_add_item(rec_tree, hf_kt_key_str, tvb, new_offset, ksiz, ENC_ASCII|ENC_NA);
                PROTO_ITEM_SET_GENERATED(pi);
            }
            new_offset += ksiz;

            proto_tree_add_item(rec_tree, hf_kt_val, tvb, new_offset, vsiz, ENC_NA);
            if (kt_present_key_val_as_ascii) {
                pi = proto_tree_add_item(rec_tree, hf_kt_val_str, tvb, new_offset, vsiz, ENC_ASCII|ENC_NA);
                PROTO_ITEM_SET_GENERATED(pi);
            }
            new_offset += vsiz;

            proto_item_set_len(ti, new_offset - rec_start_offset);
            rnum--;
        }
    }

    return new_offset;
}

static int
dissect_kt_get_bulk(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, gint offset)
{
    guint32 next32, rnum, ksiz, vsiz;
    guint64 xt;
    nstime_t ts;
    gint new_offset, rec_start_offset;
    proto_item *ti;
    proto_item *pi;
    proto_tree *rec_tree;

    new_offset = offset;

    proto_tree_add_item(tree, hf_kt_magic, tvb, new_offset, 1, ENC_BIG_ENDIAN);
    new_offset++;

    next32 = tvb_get_ntohl(tvb, new_offset);

    if (next32 == 0) {
        if (tvb_reported_length_remaining(tvb, (new_offset + 4)) > 0) { /* request */
            pi = proto_tree_add_uint(tree, hf_kt_type, tvb, offset, 1, KT_OPER_REQUEST);
            PROTO_ITEM_SET_GENERATED(pi);

            proto_tree_add_uint(tree, hf_kt_flags, tvb, new_offset, 4, next32);
            new_offset += 4;

            rnum = tvb_get_ntohl(tvb, new_offset);
            proto_tree_add_uint(tree, hf_kt_rnum, tvb, new_offset, 4, rnum);
            new_offset += 4;

            while (rnum > 0) {
                /* Create a sub-tree for each record */
                ti = proto_tree_add_item(tree, hf_kt_rec, tvb, new_offset, -1, ENC_NA);
                rec_tree = proto_item_add_subtree(ti, ett_kt_rec);
                rec_start_offset = new_offset;

                proto_tree_add_item(rec_tree, hf_kt_dbidx, tvb, new_offset, 2, ENC_BIG_ENDIAN);
                new_offset += 2;

                ksiz = tvb_get_ntohl(tvb, new_offset);
                proto_tree_add_uint(rec_tree, hf_kt_ksiz, tvb, new_offset, 4, ksiz);
                new_offset += 4;

                proto_tree_add_item(rec_tree, hf_kt_key, tvb, new_offset, ksiz, ENC_NA);
                if (kt_present_key_val_as_ascii) {
                    pi = proto_tree_add_item(rec_tree, hf_kt_key_str, tvb, new_offset, ksiz, ENC_ASCII|ENC_NA);
                    PROTO_ITEM_SET_GENERATED(pi);
                }
                new_offset += ksiz;

                proto_item_set_len(ti, new_offset - rec_start_offset);
                rnum--;
            }
        } else { /* response - no records */
            pi = proto_tree_add_uint(tree, hf_kt_type, tvb, offset, 1, KT_OPER_RESPONSE);
            PROTO_ITEM_SET_GENERATED(pi);
            col_append_sep_str(pinfo->cinfo, COL_INFO, " ", "[response]");

            proto_tree_add_uint(tree, hf_kt_hits, tvb, new_offset, 4, next32);
            new_offset += 4;
        }
    } else { /* response - one or more records */
        pi = proto_tree_add_uint(tree, hf_kt_type, tvb, offset, 1, KT_OPER_RESPONSE);
        PROTO_ITEM_SET_GENERATED(pi);
        col_append_sep_str(pinfo->cinfo, COL_INFO, " ", "[response]");

        rnum = tvb_get_ntohl(tvb, new_offset);
        proto_tree_add_uint(tree, hf_kt_hits, tvb, new_offset, 4, rnum);
        new_offset += 4;

        while (rnum > 0) {
            /* Create a sub-tree for each record */
            ti = proto_tree_add_item(tree, hf_kt_rec, tvb, new_offset, -1, ENC_NA);
            rec_tree = proto_item_add_subtree(ti, ett_kt_rec);
            rec_start_offset = new_offset;

            proto_tree_add_item(rec_tree, hf_kt_dbidx, tvb, new_offset, 2, ENC_BIG_ENDIAN);
            new_offset += 2;

            ksiz = tvb_get_ntohl(tvb, new_offset);
            proto_tree_add_uint(rec_tree, hf_kt_ksiz, tvb, new_offset, 4, ksiz);
            new_offset += 4;

            vsiz = tvb_get_ntohl(tvb, new_offset);
            proto_tree_add_uint(rec_tree, hf_kt_vsiz, tvb, new_offset, 4, vsiz);
            new_offset += 4;

            xt = tvb_get_ntoh64(tvb, new_offset);
            ts.secs = (time_t)(xt&0xFFFFFFFF);
            ts.nsecs = 0;
            proto_tree_add_time(rec_tree, hf_kt_xt_resp, tvb, new_offset, 8, &ts);
            new_offset += 8;

            proto_tree_add_item(rec_tree, hf_kt_key, tvb, new_offset, ksiz, ENC_NA);
            if (kt_present_key_val_as_ascii) {
                pi = proto_tree_add_item(rec_tree, hf_kt_key_str, tvb, new_offset, ksiz, ENC_ASCII|ENC_NA);
                PROTO_ITEM_SET_GENERATED(pi);
            }
            new_offset += ksiz;

            proto_tree_add_item(rec_tree, hf_kt_val, tvb, new_offset, vsiz, ENC_NA);
            if (kt_present_key_val_as_ascii) {
                pi = proto_tree_add_item(rec_tree, hf_kt_val_str, tvb, new_offset, vsiz, ENC_ASCII|ENC_NA);
                PROTO_ITEM_SET_GENERATED(pi);
            }
            new_offset += vsiz;

            proto_item_set_len(ti, new_offset - rec_start_offset);
            rnum--;
        }
    }

    return new_offset;
}

static int
dissect_kt_remove_bulk(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, gint offset)
{
    guint32 next32, rnum, ksiz;
    gint new_offset, rec_start_offset;
    proto_item *ti;
    proto_item *pi;
    proto_tree *rec_tree;

    new_offset = offset;

    proto_tree_add_item(tree, hf_kt_magic, tvb, new_offset, 1, ENC_BIG_ENDIAN);
    new_offset++;

    next32 = tvb_get_ntohl(tvb, new_offset);

    if (tvb_reported_length_remaining(tvb, (new_offset + 4)) > 0) { /* request */
        pi = proto_tree_add_uint(tree, hf_kt_type, tvb, offset, 1, KT_OPER_REQUEST);
        PROTO_ITEM_SET_GENERATED(pi);

        proto_tree_add_uint(tree, hf_kt_flags, tvb, new_offset, 4, next32);
        new_offset += 4;

        rnum = tvb_get_ntohl(tvb, new_offset);
        proto_tree_add_uint(tree, hf_kt_rnum, tvb, new_offset, 4, rnum);
        new_offset += 4;

        while (rnum > 0) {
            /* Create a sub-tree for each record */
            ti = proto_tree_add_item(tree, hf_kt_rec, tvb, new_offset, -1, ENC_NA);
            rec_tree = proto_item_add_subtree(ti, ett_kt_rec);
            rec_start_offset = new_offset;

            proto_tree_add_item(rec_tree, hf_kt_dbidx, tvb, new_offset, 2, ENC_BIG_ENDIAN);
            new_offset += 2;

            ksiz = tvb_get_ntohl(tvb, new_offset);
            proto_tree_add_uint(rec_tree, hf_kt_ksiz, tvb, new_offset, 4, ksiz);
            new_offset += 4;

            proto_tree_add_item(rec_tree, hf_kt_key, tvb, new_offset, ksiz, ENC_NA);
            if (kt_present_key_val_as_ascii) {
                pi = proto_tree_add_item(rec_tree, hf_kt_key_str, tvb, new_offset, ksiz, ENC_ASCII|ENC_NA);
                PROTO_ITEM_SET_GENERATED(pi);
            }
            new_offset += ksiz;

            proto_item_set_len(ti, new_offset - rec_start_offset);
            rnum--;
        }
    } else { /* response */
        pi = proto_tree_add_uint(tree, hf_kt_type, tvb, offset, 1, KT_OPER_RESPONSE);
        PROTO_ITEM_SET_GENERATED(pi);
        col_append_sep_str(pinfo->cinfo, COL_INFO, " ", "[response]");

        proto_tree_add_uint(tree, hf_kt_hits, tvb, new_offset, 4, next32);
        new_offset += 4;
    }

    return new_offset;
}

static int
dissect_kt_error(tvbuff_t *tvb, proto_tree *tree, gint offset)
{
    gint new_offset;

    new_offset = offset;

    proto_tree_add_item(tree, hf_kt_magic, tvb, new_offset, 1, ENC_BIG_ENDIAN);
    new_offset++;

    return new_offset;
}

static void
dissect_kt(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
    gint      magic;
    proto_item *ti;
    proto_tree *kt_tree;
    gint offset, offset_start;

    offset = 0;

    while (tvb_reported_length_remaining(tvb, offset) > 0) {
        magic = tvb_get_guint8(tvb, offset);

        /* If the magic is not one of the known values, exit */
        if (try_val_to_str(magic, kt_magic_vals) == NULL)
            return;

        /* Otherwise, the magic value is known. Continue */
        col_set_str(pinfo->cinfo, COL_PROTOCOL, "KT");
        col_set_str(pinfo->cinfo, COL_INFO, try_val_to_str(magic, kt_magic_vals));

        ti = proto_tree_add_item(tree, proto_kt, tvb, offset, -1, ENC_NA);
        kt_tree = proto_item_add_subtree(ti, ett_kt);

        offset_start=offset;

        switch (magic) {
            case KT_MAGIC_REPL_WAIT:
                offset = dissect_kt_replication_wait(tvb, kt_tree, offset);
                break;
            case KT_MAGIC_REPLICATION:
                offset = dissect_kt_replication(tvb, pinfo, kt_tree, offset);
                break;
            case KT_MAGIC_PLAY_SCRIPT:
                offset = dissect_kt_play_script(tvb, pinfo, kt_tree, offset);
                break;
            case KT_MAGIC_SET_BULK:
                offset = dissect_kt_set_bulk(tvb, pinfo, kt_tree, offset);
                break;
            case KT_MAGIC_REMOVE_BULK:
                offset = dissect_kt_remove_bulk(tvb, pinfo, kt_tree, offset);
                break;
            case KT_MAGIC_GET_BULK:
                offset = dissect_kt_get_bulk(tvb, pinfo, kt_tree, offset);
                break;
            case KT_MAGIC_ERROR:
                offset = dissect_kt_error(tvb, kt_tree, offset);
                break;
        }

        proto_item_set_len(ti, offset-offset_start);
    }
}

void
proto_register_kt(void)
{
    module_t *kt_module; /* preferences */

    static hf_register_info hf[] = {
        {   &hf_kt_magic,
            {   "magic", "kt.magic", FT_UINT8, BASE_HEX,
                VALS(kt_magic_vals), 0x0, "identifier", HFILL
            }
        },
        {   &hf_kt_type,
            {   "type", "kt.type", FT_UINT8, BASE_HEX,
                VALS(kt_oper_vals), 0x0, "request/response", HFILL
            }
        },
        {   &hf_kt_flags,
            {   "flags", "kt.flags", FT_UINT32, BASE_HEX,
                NULL, 0x0, "flags of bitwise-or", HFILL
            }
        },
        {   &hf_kt_rnum,
            {   "rnum", "kt.rnum", FT_UINT32, BASE_DEC, NULL, 0x0,
                "the number of records", HFILL
            }
        },
        {   &hf_kt_dbidx,
            {   "dbidx", "kt.dbidx", FT_UINT16,
                BASE_DEC, NULL, 0x0, "the index of the target database", HFILL
            }
        },
        {   &hf_kt_sid,
            {   "sid", "kt.sid", FT_UINT16, BASE_DEC,
                NULL, 0x0, "the server ID number", HFILL
            }
        },
        {   &hf_kt_ts,
            {   "ts", "kt.ts", FT_ABSOLUTE_TIME, ABSOLUTE_TIME_LOCAL,
                NULL, 0x0, "the timestamp of the log", HFILL
            }
        },
        {   &hf_kt_xt,
            {   "xt", "kt.xt", FT_UINT64, BASE_DEC,
                NULL, 0x0, "the expiration time in seconds", HFILL
            }
        },
        {   &hf_kt_xt_resp,
            {   "xt", "kt.xt_resp", FT_ABSOLUTE_TIME, ABSOLUTE_TIME_LOCAL,
                NULL, 0x0, "the expiration time", HFILL
            }
        },
        {   &hf_kt_ksiz,
            {   "ksiz", "kt.ksiz", FT_UINT32, BASE_DEC,
                NULL, 0x0, "the size of the key",HFILL
            }
        },
        {   &hf_kt_vsiz,
            {   "vsiz", "kt.vsiz", FT_UINT32, BASE_DEC,
                NULL, 0x0, "the size of the value", HFILL
            }
        },
        {   &hf_kt_key,
            {   "key", "kt.key", FT_BYTES, BASE_NONE,
                NULL, 0x0, "the key", HFILL
            }
        },
        {   &hf_kt_val,
            {   "value", "kt.value", FT_BYTES, BASE_NONE,
                NULL, 0x0, "the value", HFILL
            }
        },
        {   &hf_kt_key_str,
            {   "key", "kt.key_str", FT_STRING, BASE_NONE,
                NULL, 0x0, "ASCII representation of the key", HFILL
            }
        },
        {   &hf_kt_val_str,
            {   "value", "kt.value_str", FT_STRING, BASE_NONE,
                NULL, 0x0, "ASCII representation of the value", HFILL
            }
        },
        {   &hf_kt_hits,
            {   "hits", "kt.hits", FT_UINT32, BASE_DEC,
                NULL, 0x0, "the number of records", HFILL
            }
        },
        {   &hf_kt_size,
            {   "size", "kt.size", FT_UINT32, BASE_DEC,
                NULL, 0x0, "the size of the replication log", HFILL
            }
        },
        {   &hf_kt_log,
            {   "log", "kt.log", FT_BYTES, BASE_NONE,
                NULL, 0x0, "the replication log", HFILL
            }
        },
        {   &hf_kt_nsiz,
            {   "nsiz", "kt.nsiz", FT_UINT32, BASE_DEC,
                NULL, 0x0, "the size of the procedure name", HFILL
            }
        },
        {   &hf_kt_name,
            {   "name", "kt.name", FT_STRING, BASE_NONE,
                NULL, 0x0, "the procedure name", HFILL
            }
        },
        {   &hf_kt_rec,
            {   "record", "kt.record", FT_NONE, BASE_NONE,
                NULL, 0x0, "a record", HFILL
            }
        }
    };

    static gint *ett[] = {
        &ett_kt,
        &ett_kt_rec
    };

    proto_kt = proto_register_protocol("Kyoto Tycoon Protocol", "Kyoto Tycoon", "kt");
    register_dissector("kt", dissect_kt, proto_kt);
    proto_register_field_array(proto_kt, hf, array_length(hf));
    proto_register_subtree_array(ett, array_length(ett));

    /* Preferences */
    kt_module = prefs_register_protocol(proto_kt, proto_reg_handoff_kt);

    range_convert_str(&global_kt_tcp_port_range, DEFAULT_KT_PORT_RANGE, MAX_TCP_PORT);
    prefs_register_range_preference(kt_module, "tcp.ports", "Kyoto Tycoon TCP ports",
                                    "TCP ports to be decoded as Kyoto Tycoon (binary protocol) (default: "
                                    DEFAULT_KT_PORT_RANGE ")",
                                    &global_kt_tcp_port_range, MAX_TCP_PORT);

    prefs_register_bool_preference(kt_module, "present_key_val_as_ascii",
                                   "Attempt to also show ASCII string representation of keys and values",
                                   "KT allows binary values in keys and values. Attempt to show an ASCII representation anyway (which might be prematurely terminated by a NULL!",
                                   &kt_present_key_val_as_ascii);
}

void
proto_reg_handoff_kt(void)
{
    static gboolean Initialized = FALSE;
    static range_t *kt_tcp_port_range;

    if (!Initialized) {
        kt_handle = find_dissector("kt");
        Initialized = TRUE;
    }
    else {
        dissector_delete_uint_range("tcp.port", kt_tcp_port_range, kt_handle);
        g_free(kt_tcp_port_range);
    }

    kt_tcp_port_range = range_copy(global_kt_tcp_port_range);
    dissector_add_uint_range("tcp.port", kt_tcp_port_range, kt_handle);
}

/*
 * Editor modelines  -  http://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 4
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=4 tabstop=8 expandtab:
 * :indentSize=4:tabSize=8:noTabs=true:
 */
