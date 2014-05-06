/* emem.h
 * Definitions for Wireshark memory management and garbage collection
 * Ronnie Sahlberg 2005
 *
 * $Id: emem.h 54436 2013-12-24 03:39:19Z eapache $
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
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

#ifndef __EMEM_H__
#define __EMEM_H__

#include <glib.h>

#include "ws_symbol_export.h"

/**  Initialize all the memory allocation pools described below.
 *  This function must be called once when *shark initialize to set up the
 *  required structures.
 */
WS_DLL_PUBLIC
void emem_init(void);

/* Functions for handling memory allocation and garbage collection with
 * a packet lifetime scope.
 * These functions are used to allocate memory that will only remain persistent
 * until Wireshark starts dissecting the next packet in the list.
 * Everytime Wireshark starts decoding the next packet all memory allocated
 * through these functions will be released back to the free pool.
 *
 * These functions are very fast and offer automatic garbage collection:
 * Everytime a new packet is dissected, all memory allocations done in
 * the previous packet is freed.
 */

/** Allocate memory with a packet lifetime scope */
WS_DLL_PUBLIC
void *ep_alloc(size_t size) G_GNUC_MALLOC;
#define ep_new(type) ((type*)ep_alloc(sizeof(type)))

/** Allocate memory with a packet lifetime scope and fill it with zeros*/
WS_DLL_PUBLIC
void* ep_alloc0(size_t size) G_GNUC_MALLOC;
#define ep_new0(type) ((type*)ep_alloc0(sizeof(type)))

/** Duplicate a string with a packet lifetime scope */
WS_DLL_PUBLIC
gchar* ep_strdup(const gchar* src) G_GNUC_MALLOC;

/** Duplicate at most n characters of a string with a packet lifetime scope */
WS_DLL_PUBLIC
gchar* ep_strndup(const gchar* src, size_t len) G_GNUC_MALLOC;

/** Duplicate a buffer with a packet lifetime scope */
WS_DLL_PUBLIC
void* ep_memdup(const void* src, size_t len) G_GNUC_MALLOC;

/** Create a formatted string with a packet lifetime scope */
WS_DLL_PUBLIC
gchar* ep_strdup_vprintf(const gchar* fmt, va_list ap) G_GNUC_MALLOC;
WS_DLL_PUBLIC
gchar* ep_strdup_printf(const gchar* fmt, ...)
     G_GNUC_MALLOC G_GNUC_PRINTF(1, 2);

WS_DLL_PUBLIC
gchar *ep_strconcat(const gchar *string, ...) G_GNUC_MALLOC G_GNUC_NULL_TERMINATED;

/** allocates with a packet lifetime scope an array of type made of num elements */
#define ep_alloc_array(type,num) (type*)ep_alloc(sizeof(type)*(num))

/** allocates with a packet lifetime scope an array of type made of num elements,
 * initialised to zero.
 */
#define ep_alloc_array0(type,num) (type*)ep_alloc0(sizeof(type)*(num))

/**
 * Splits a string into a maximum of max_tokens pieces, using the given
 * delimiter. If max_tokens is reached, the remainder of string is appended
 * to the last token. Consecutive delimiters are treated as a single delimiter.
 *
 * The vector and all the strings are allocated with packet lifetime scope
 */
WS_DLL_PUBLIC
gchar** ep_strsplit(const gchar* string, const gchar* delimiter, int max_tokens);

/** release all memory allocated in the previous packet dissection */
void ep_free_all(void);

/* Functions for handling memory allocation and garbage collection with
 * a capture lifetime scope.
 * These functions are used to allocate memory that will only remain persistent
 * until Wireshark opens a new capture or capture file.
 * Everytime Wireshark starts a new capture or opens a new capture file
 * all the data allocated through these functions will be released back
 * to the free pool.
 *
 * These functions are very fast and offer automatic garbage collection.
 */

/** Allocate memory with a capture lifetime scope */
WS_DLL_PUBLIC
void *se_alloc(size_t size) G_GNUC_MALLOC;
#define se_new(type) ((type*)se_alloc(sizeof(type)))

/** Allocate memory with a capture lifetime scope and fill it with zeros*/
WS_DLL_PUBLIC
void* se_alloc0(size_t size) G_GNUC_MALLOC;
#define se_new0(type) ((type*)se_alloc0(sizeof(type)))

/** Duplicate a string with a capture lifetime scope */
WS_DLL_PUBLIC
gchar* se_strdup(const gchar* src) G_GNUC_MALLOC;

/** Duplicate at most n characters of a string with a capture lifetime scope */
WS_DLL_PUBLIC
gchar* se_strndup(const gchar* src, size_t len) G_GNUC_MALLOC;

/** Duplicate a buffer with a capture lifetime scope */
WS_DLL_PUBLIC
void* se_memdup(const void* src, size_t len) G_GNUC_MALLOC;

/* Create a formatted string with a capture lifetime scope */
WS_DLL_PUBLIC
gchar* se_strdup_vprintf(const gchar* fmt, va_list ap) G_GNUC_MALLOC;
WS_DLL_PUBLIC
gchar* se_strdup_printf(const gchar* fmt, ...)
     G_GNUC_MALLOC G_GNUC_PRINTF(1, 2);

/** allocates with a capture lifetime scope an array of type made of num elements */
#define se_alloc_array(type,num) (type*)se_alloc(sizeof(type)*(num))

/** release all memory allocated */
void se_free_all(void);

/**************************************************************
 * slab allocator
 **************************************************************/
struct _emem_chunk_t;

/* G_MEM_ALIGN is not always enough: http://mail.gnome.org/archives/gtk-devel-list/2004-December/msg00091.html
 * So, we check (in configure) if we need 8-byte alignment.  (Windows
 * shouldn't need such a check until someone trys running it 32-bit on a CPU
 * with more stringent alignment requirements than i386.)
 *
 * Yes, this ignores the possibility of needing 16-byte alignment for long doubles.
 */
#if defined(NEED_8_BYTE_ALIGNMENT) && (G_MEM_ALIGN < 8)
#define WS_MEM_ALIGN 8
#else
#define WS_MEM_ALIGN G_MEM_ALIGN
#endif

/**************************************************************
 * binary trees
 **************************************************************/
typedef struct _emem_tree_node_t {
	struct _emem_tree_node_t *parent;
	struct _emem_tree_node_t *left;
	struct _emem_tree_node_t *right;
	struct {
#define EMEM_TREE_RB_COLOR_RED		0
#define EMEM_TREE_RB_COLOR_BLACK	1
		guint32 rb_color:1;
#define EMEM_TREE_NODE_IS_DATA		0
#define EMEM_TREE_NODE_IS_SUBTREE	1
		guint32 is_subtree:1;
	} u;
	guint32 key32;
	void *data;
} emem_tree_node_t;

/** Right now we only do basic red/black trees   but in the future we might want
 * to try something different, such as a tree where each node keeps track
 * of how many times it has been looked up, and letting often looked up
 * nodes bubble upwards in the tree using rotate_right/left.
 * That would probably be good for things like nfs filehandles
 */
#define EMEM_TREE_TYPE_RED_BLACK	1
typedef struct _emem_tree_t {
	struct _emem_tree_t *next;
	int type;
	const char *name;    /**< just a string to make debugging easier */
	emem_tree_node_t *tree;
	void *(*malloc)(size_t);
} emem_tree_t;

/* *******************************************************************
 * Tree functions for SE memory allocation scope
 * ******************************************************************* */
/** This function is used to create a se based tree with monitoring.
 * When the SE heap is released back to the system the pointer to the
 * tree is automatically reset to NULL.
 *
 * type is : EMEM_TREE_TYPE_RED_BLACK for a standard red/black tree.
 */
WS_DLL_PUBLIC
emem_tree_t *se_tree_create(int type, const char *name) G_GNUC_MALLOC;

/** se_tree_insert32
 * Insert data into the tree and key it by a 32bit integer value
 */
#define se_tree_insert32 emem_tree_insert32

/** se_tree_lookup32
 * Retrieve the data at the search key. The search key is a 32bit integer value
 */
#define se_tree_lookup32 emem_tree_lookup32

/* ******************************************************************
 * Real tree functions
 * ****************************************************************** */

/** This function is used to insert a node indexed by a guint32 key value.
 * The data pointer should be allocated by the appropriate storage scope
 * so that it will be released at the same time as the tree itself is
 * destroyed.
 */
WS_DLL_PUBLIC
void emem_tree_insert32(emem_tree_t *se_tree, guint32 key, void *data);

/** This function will look up a node in the tree indexed by a guint32 integer
 * value.
 */
WS_DLL_PUBLIC
void *emem_tree_lookup32(emem_tree_t *se_tree, guint32 key);

typedef struct _emem_tree_key_t {
	guint32 length;			/**< length in guint32 words */
	guint32 *key;
} emem_tree_key_t;

/** traverse a tree. if the callback returns TRUE the traversal will end */
typedef gboolean (*tree_foreach_func)(void *value, void *userdata);

WS_DLL_PUBLIC
gboolean emem_tree_foreach(emem_tree_t* emem_tree, tree_foreach_func callback, void *user_data);


/* ******************************************************************
 * String buffers - Growable strings similar to GStrings
 * ****************************************************************** */

typedef struct _emem_strbuf_t {
    gchar *str;             /**< Points to the character data. It may move as text is       */
                            /*  added. The str field is null-terminated and so can        */
                            /*  be used as an ordinary C string.                          */
    gsize len;              /**< strlen: ie: length of str not including trailing '\0'      */
    gsize alloc_len;        /**< num bytes curently allocated for str: 1 .. MAX_STRBUF_LEN  */
    gsize max_alloc_len;    /**< max num bytes to allocate for str: 1 .. MAX_STRBUF_LEN     */
} emem_strbuf_t;

/*
 * The maximum length is limited to 64K. If you need something bigger, you
 * should probably use an actual GString or GByteArray.
 */

/**
 * Allocate an ephemeral string buffer with "unlimited" size.
 *
 * @param init The initial string for the buffer, or NULL to allocate an initial zero-length string.
 *
 * @return A newly-allocated string buffer.
 */
WS_DLL_PUBLIC
emem_strbuf_t *ep_strbuf_new(const gchar *init) G_GNUC_MALLOC;

/**
 * Allocate an ephemeral string buffer suitable for the protocol tree.
 * The string will never grow beyond the maximum tree item length.
 *
 * @param init The initial string for the buffer, or NULL to allocate an initial zero-length string.
 *
 * @return A newly-allocated string buffer.
 */
WS_DLL_PUBLIC
emem_strbuf_t *ep_strbuf_new_label(const gchar *init) G_GNUC_MALLOC;

/**
 * Allocate an ephemeral string buffer with enough initial space for alloc_len bytes
 * and a maximum of max_alloc_len bytes.
 *
 * @param alloc_len The initial size of the buffer. This value can be 0, but a nonzero
 * value is recommended.
 * @param max_alloc_len The maximum size of the buffer. 0 means "unlimited" (within
 * reason).
 *
 * @return A newly-allocated string buffer. str will be empty.
 */
WS_DLL_PUBLIC
emem_strbuf_t *ep_strbuf_sized_new(gsize alloc_len, gsize max_alloc_len) G_GNUC_MALLOC;

/**
 * Append vprintf-style formatted text to a string buffer.
 *
 * @param strbuf The ep_strbuf-allocated string buffer to append to.
 * @param format A printf-style string format.
 * @param ap The list of arguments to append.
 */
WS_DLL_PUBLIC
void ep_strbuf_append_vprintf(emem_strbuf_t *strbuf, const gchar *format, va_list ap);

/**
 * Apply printf-style formatted text to a string buffer.
 *
 * @param strbuf The ep_strbuf-allocated string buffer to set to.
 * @param format A printf-style string format.
 */
WS_DLL_PUBLIC
void ep_strbuf_printf(emem_strbuf_t *strbuf, const gchar *format, ...)
     G_GNUC_PRINTF(2, 3);

/**
 * Append printf-style formatted text to a string buffer.
 *
 * @param strbuf The ep_strbuf-allocated string buffer to append to.
 * @param format A printf-style string format.
 */
WS_DLL_PUBLIC
void ep_strbuf_append_printf(emem_strbuf_t *strbuf, const gchar *format, ...)
    G_GNUC_PRINTF(2, 3);

/**
 * Append a string to a string buffer.
 *
 * @param strbuf The ep_strbuf-allocated string buffer to append to.
 * @param str A null-terminated string.
 *
 * @return strbuf
 */
WS_DLL_PUBLIC
emem_strbuf_t *ep_strbuf_append(emem_strbuf_t *strbuf, const gchar *str);

/**
 * Append a character to a string buffer.
 *
 * @param strbuf The ep_strbuf-allocated string buffer to append to.
 * @param c The character to append.
 *
 * @return strbuf
 */
WS_DLL_PUBLIC
emem_strbuf_t *ep_strbuf_append_c(emem_strbuf_t *strbuf, const gchar c);

/**
 * Append a Unicode characeter converted to UTF-8 to a string buffer.
 *
 * @param strbuf The ep_strbuf-allocated string buffer to append to.
 * @param c The Unicode character to append.
 *
 * @return strbuf
 */
WS_DLL_PUBLIC
emem_strbuf_t *ep_strbuf_append_unichar(emem_strbuf_t *strbuf, const gunichar c);

/**
 * Chop off the end of a string buffer.
 *
 * @param strbuf The ep_strbuf-allocated string buffer to append to.
 * @param len The new string length.
 *
 * @return strbuf
 */
WS_DLL_PUBLIC
emem_strbuf_t *ep_strbuf_truncate(emem_strbuf_t *strbuf, gsize len);

/**
 * Dump the whole tree (of trees) to stdout.
 *
 * @param emem_tree The tree to dump to standard output.
 *
 * 
 */
void emem_print_tree(emem_tree_t* emem_tree);

/* #define DEBUG_INTENSE_CANARY_CHECKS */

/** Helper to troubleshoot ep memory corruption.
 * If compiled and the environment variable WIRESHARK_DEBUG_EP_INTENSE_CANARY exists
 * it will check the canaries and when found corrupt stop there in the hope
 * the corruptor is still there in the stack.
 * Some checkpoints are already set in packet.c in strategic points
 * before and after dissection of a frame or a dissector call.
 */

#ifdef DEBUG_INTENSE_CANARY_CHECKS
void ep_check_canary_integrity(const char* fmt, ...)
    G_GNUC_PRINTF(1, 2);
#define EP_CHECK_CANARY(args) ep_check_canary_integrity args
#else
#define EP_CHECK_CANARY(args)
#endif

/**
 * Verify that the given pointer is of ephemeral type.
 *
 * @param ptr The pointer to verify
 *
 * @return TRUE if the pointer belongs to the ephemeral pool.
 */
gboolean ep_verify_pointer(const void *ptr);
/**
 * Verify that the given pointer is of seasonal type.
 *
 * @param ptr The pointer to verify
 *
 * @return TRUE if the pointer belongs to the seasonal pool.
 */
gboolean se_verify_pointer(const void *ptr);

#endif /* emem.h */
