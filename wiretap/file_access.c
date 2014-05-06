/* file_access.c
 *
 * $Id: file_access.c 54495 2013-12-30 06:43:19Z guy $
 *
 * Wiretap Library
 * Copyright (c) 1998 by Gilbert Ramirez <gram@alumni.rice.edu>
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <errno.h>

#include <wsutil/file_util.h>

#include "wtap-int.h"
#include "file_wrappers.h"
#include "buffer.h"
#include "lanalyzer.h"
#include "ngsniffer.h"
#include "radcom.h"
#include "ascendtext.h"
#include "nettl.h"
#include "libpcap.h"
#include "snoop.h"
#include "iptrace.h"
#include "iseries.h"
#include "netmon.h"
#include "netxray.h"
#include "toshiba.h"
#include "eyesdn.h"
#include "i4btrace.h"
#include "csids.h"
#include "pppdump.h"
#include "peekclassic.h"
#include "peektagged.h"
#include "vms.h"
#include "dbs-etherwatch.h"
#include "visual.h"
#include "cosine.h"
#include "5views.h"
#include "erf.h"
#include "hcidump.h"
#include "network_instruments.h"
#include "k12.h"
#include "ber.h"
#include "catapult_dct2000.h"
#include "mp2t.h"
#include "mpeg.h"
#include "netscreen.h"
#include "commview.h"
#include "pcapng.h"
#include "aethra.h"
#include "btsnoop.h"
#include "tnef.h"
#include "dct3trace.h"
#include "packetlogger.h"
#include "daintree-sna.h"
#include "netscaler.h"
#include "mime_file.h"
#include "ipfix.h"
#include "vwr.h"
#include "camins.h"
#include "stanag4607.h"
#include "pcap-encap.h"

/*
 * Add an extension, and all compressed versions thereof, to a GSList
 * of extensions.
 */
static GSList *add_extensions(GSList *extensions, const gchar *extension,
    GSList *compressed_file_extensions)
{
	GSList *compressed_file_extension;

	/*
	 * Add the specified extension.
	 */
	extensions = g_slist_append(extensions, g_strdup(extension));

	/*
	 * Now add the extensions for compressed-file versions of
	 * that extension.
	 */
	for (compressed_file_extension = compressed_file_extensions;
	    compressed_file_extension != NULL;
	    compressed_file_extension = g_slist_next(compressed_file_extension)) {
		extensions = g_slist_append(extensions,
		    g_strdup_printf("%s.%s", extension,
		      (gchar *)compressed_file_extension->data));
	}

	return extensions;
}

/*
 * File types that can be identified by file extensions.
 */
static const struct file_extension_info file_type_extensions_base[] = {
	{ "Wireshark/tcpdump/... - pcap", "pcap;cap;dmp" },
	{ "Wireshark/... - pcapng", "pcapng;ntar" },
	{ "Network Monitor, Surveyor, NetScaler", "cap" },
	{ "InfoVista 5View capture", "5vw" },
	{ "Sniffer (DOS)", "cap;enc;trc;fdc;syc" },
	{ "NetXRay, Sniffer (Windows)", "cap;caz" },
	{ "Endace ERF capture", "erf" },
	{ "EyeSDN USB S0/E1 ISDN trace format", "trc" },
	{ "HP-UX nettl trace", "trc0;trc1" },
	{ "Network Instruments Observer", "bfr" },
	{ "Novell LANalyzer", "tr1" },
	{ "Tektronix K12xx 32-bit .rf5 format", "rf5" },
	{ "WildPackets *Peek", "pkt;tpc;apc;wpz" },
	{ "Catapult DCT2000 trace (.out format)", "out" },
	{ "MPEG files", "mpg;mp3" },
	{ "CommView", "ncf" },
	{ "Symbian OS btsnoop", "log" },
	{ "Transport-Neutral Encapsulation Format", "tnef" },
	{ "XML files (including Gammu DCT3 traces)", "xml" },
	{ "OS X PacketLogger", "pklg" },
	{ "Daintree SNA", "dcf" },
	{ "JPEG/JFIF files", "jpg;jpeg;jfif" },
	{ "IPFIX File Format", "pfx;ipfix" },
	{ "Aethra .aps file", "aps" },
	{ "MPEG2 transport stream", "mp2t;ts;mpg" },
	{ "Ixia IxVeriWave .vwr Raw 802.11 Capture", "vwr" },
	{ "CAM Inspector file", "camins" },
};

#define	N_FILE_TYPE_EXTENSIONS	(sizeof file_type_extensions_base / sizeof file_type_extensions_base[0])

static const struct file_extension_info* file_type_extensions = NULL;

static GArray* file_type_extensions_arr = NULL;

/* initialize the extensions array if it has not been initialized yet */
static void init_file_type_extensions(void) {

	if (file_type_extensions_arr) return;

	file_type_extensions_arr = g_array_new(FALSE,TRUE,sizeof(struct file_extension_info));

	g_array_append_vals(file_type_extensions_arr,file_type_extensions_base,N_FILE_TYPE_EXTENSIONS);

	file_type_extensions = (struct file_extension_info*)(void *)file_type_extensions_arr->data;
}

void wtap_register_file_type_extension(const struct file_extension_info *ei) {
	init_file_type_extensions();

	g_array_append_val(file_type_extensions_arr,*ei);

	file_type_extensions = (const struct file_extension_info*)(void *)file_type_extensions_arr->data;
}

int wtap_get_num_file_type_extensions(void)
{
	return file_type_extensions_arr->len;
}

const char *wtap_get_file_extension_type_name(int extension_type)
{
	return file_type_extensions[extension_type].name;
}

static GSList *add_extensions_for_file_extensions_type(int extension_type,
    GSList *extensions, GSList *compressed_file_extensions)
{
	gchar **extensions_set, **extensionp, *extension;

	/*
	 * Split the extension-list string into a set of extensions.
	 */
	extensions_set = g_strsplit(file_type_extensions[extension_type].extensions,
	    ";", 0);

	/*
	 * Add each of those extensions to the list.
	 */
	for (extensionp = extensions_set; *extensionp != NULL; extensionp++) {
		extension = *extensionp;

		/*
		 * Add the extension, and all compressed variants
		 * of it.
		 */
		extensions = add_extensions(extensions, extension,
		    compressed_file_extensions);
	}

	g_strfreev(extensions_set);
	return extensions;
}

/* Return a list of file extensions that are used by the specified file
   extension type.

   All strings in the list are allocated with g_malloc() and must be freed
   with g_free(). */
GSList *wtap_get_file_extension_type_extensions(guint extension_type)
{
	GSList *compressed_file_extensions;
	GSList *extensions;

	if (extension_type >= file_type_extensions_arr->len)
		return NULL;	/* not a valid extension type */

	extensions = NULL;	/* empty list, to start with */

	/*
	 * Get the list of compressed-file extensions.
	 */
	compressed_file_extensions = wtap_get_compressed_file_extensions();

	/*
	 * Add all this file extension type's extensions, with compressed
	 * variants.
	 */
	extensions = add_extensions_for_file_extensions_type(extension_type,
	    extensions, compressed_file_extensions);

	g_slist_free(compressed_file_extensions);
	return extensions;
}

/* Return a list of all extensions that are used by all file types,
   including compressed extensions, e.g. not just "pcap" but also
   "pcap.gz" if we can read gzipped files.

   All strings in the list are allocated with g_malloc() and must be freed
   with g_free(). */
GSList *wtap_get_all_file_extensions_list(void)
{
	GSList *compressed_file_extensions;
	GSList *extensions;
	unsigned int i;

	init_file_type_extensions();

	extensions = NULL;	/* empty list, to start with */

	/*
	 * Get the list of compressed-file extensions.
	 */
	compressed_file_extensions = wtap_get_compressed_file_extensions();

	for (i = 0; i < file_type_extensions_arr->len; i++) {
		/*
		 * Add all this file extension type's extensions, with
		 * compressed variants.
		 */
		extensions = add_extensions_for_file_extensions_type(i,
		    extensions, compressed_file_extensions);
	}

	g_slist_free(compressed_file_extensions);
	return extensions;
}

/* The open_file_* routines should return:
 *
 *	-1 on an I/O error;
 *
 *	1 if the file they're reading is one of the types it handles;
 *
 *	0 if the file they're reading isn't the type they're checking for.
 *
 * If the routine handles this type of file, it should set the "file_type"
 * field in the "struct wtap" to the type of the file.
 *
 * Note that the routine does not have to free the private data pointer on
 * error. The caller takes care of that by calling wtap_close on error.
 * (See https://bugs.wireshark.org/bugzilla/show_bug.cgi?id=8518)
 *
 * However, the caller does have to free the private data pointer when
 * returning 0, since the next file type will be called and will likely
 * just overwrite the pointer.
 */


/* Files that have magic bytes in fixed locations. These
 * are easy to identify.  Only an open routine is needed.
 */
static const wtap_open_routine_t magic_number_open_routines_base[] = {
	libpcap_open,
	pcapng_open,
	ngsniffer_open,
	snoop_open,
	iptrace_open,
	netmon_open,
	netxray_open,
	radcom_open,
	nettl_open,
	visual_open,
	_5views_open,
	network_instruments_open,
	peektagged_open,
	dbs_etherwatch_open,
	k12_open,
	catapult_dct2000_open,
	aethra_open,
	btsnoop_open,
	eyesdn_open,
	tnef_open,
};
#define	N_MAGIC_FILE_TYPES	(sizeof magic_number_open_routines_base / sizeof magic_number_open_routines_base[0])

static wtap_open_routine_t* magic_number_open_routines = NULL;

static GArray* magic_number_open_routines_arr = NULL;

/*
 * Initialize the magic-number open routines array if it has not been
 * initialized yet.
 */
static void init_magic_number_open_routines(void) {

	if (magic_number_open_routines_arr) return;

	magic_number_open_routines_arr = g_array_new(FALSE,TRUE,sizeof(wtap_open_routine_t));

	g_array_append_vals(magic_number_open_routines_arr,magic_number_open_routines_base,N_MAGIC_FILE_TYPES);

	magic_number_open_routines = (wtap_open_routine_t*)(void *)magic_number_open_routines_arr->data;
}

void wtap_register_magic_number_open_routine(wtap_open_routine_t open_routine) {
	init_magic_number_open_routines();

	g_array_append_val(magic_number_open_routines_arr,open_routine);

	magic_number_open_routines = (wtap_open_routine_t*)(void *)magic_number_open_routines_arr->data;
}

/* Files that don't have magic bytes at a fixed location,
 * but that instead require a heuristic of some sort to
 * identify them.  This includes ASCII trace files.
 *
 * Entries for the ASCII trace files that would be, for example,
 * saved copies of a Telnet session to some box are put after
 * most of the other entries, as we don't want to treat a capture
 * of such a session as a trace file from such a session
 * merely because it has the right text in it.  They still
 * appear before the *really* weak entries, such as the VWR entry.
 */
static const struct heuristic_open_info heuristic_open_info_base[] = {
	{ lanalyzer_open, "tr1", },
	/*
	 * PacketLogger must come before MPEG, because its files
	 * are sometimes grabbed by mpeg_open.
	 */
	{ packetlogger_open, "pklg" },
	/* Some MPEG files have magic numbers, others just have heuristics. */
	{ mpeg_open, "mpg;mp3" },
	{ dct3trace_open, "xml" },
	{ daintree_sna_open, "dcf" },
	{ mime_file_open, NULL },
	{ stanag4607_open, NULL },
	{ ber_open, NULL },

	/* I put NetScreen *before* erf, because there were some
	 * false positives with my test-files (Sake Blok, July 2007)
	 *
	 * I put VWR *after* ERF, because there were some cases where
	 * ERF files were misidentified as vwr files (Stephen
	 * Donnelly, August 2013; see bug 9054)
	 *
	 * I put VWR *after* Peek Classic, CommView, iSeries text,
	 * Toshiba text, K12 text, VMS tcpiptrace text, and NetScaler,
	 * because there were some cases where files of those types were
	 * misidentified as vwr files (Guy Harris, December 2013)
	 */
	{ netscreen_open, "txt" },
	{ erf_open, "erf" },
	{ ipfix_open, "pfx;ipfix" },
	{ k12text_open, "txt" },
	{ peekclassic_open, "pkt;tpc;apc;wpz" },
	{ pppdump_open, NULL },
	{ iseries_open, "txt" },
	{ i4btrace_open, NULL },
	{ mp2t_open, "ts;mpg" },
	{ csids_open, NULL },
	{ vms_open, "txt" },
	{ cosine_open, "txt" },
	{ hcidump_open, NULL },
	{ commview_open, "ncf" },
	{ nstrace_open, "txt" },

	/* ASCII trace files from Telnet sessions. */
	{ ascend_open, "txt" },
	{ toshiba_open, "txt" },

	/* Extremely weak heuristics - put them at the end. */
	{ vwr_open, "vwr" },
	{ camins_open, "camins" },
};
#define	N_HEURISTIC_FILE_TYPES	(sizeof heuristic_open_info_base / sizeof heuristic_open_info_base[0])

static const struct heuristic_open_info* heuristic_open_info = NULL;

static GArray* heuristic_open_info_arr = NULL;

/*
 * Initialize the heuristics array if it has not been initialized yet.
 */
static void init_heuristic_open_info(void) {

	if (heuristic_open_info_arr) return;

	heuristic_open_info_arr = g_array_new(FALSE,TRUE,sizeof(struct heuristic_open_info));

	g_array_append_vals(heuristic_open_info_arr,heuristic_open_info_base,N_HEURISTIC_FILE_TYPES);

	heuristic_open_info = (const struct heuristic_open_info*)(void *)heuristic_open_info_arr->data;
}

void wtap_register_heuristic_open_info(const struct heuristic_open_info *hi) {
	init_heuristic_open_info();

	g_array_append_val(heuristic_open_info_arr,*hi);

	heuristic_open_info = (const struct heuristic_open_info*)(void *)heuristic_open_info_arr->data;
}

/*
 * Visual C++ on Win32 systems doesn't define these.  (Old UNIX systems don't
 * define them either.)
 *
 * Visual C++ on Win32 systems doesn't define S_IFIFO, it defines _S_IFIFO.
 */
#ifndef S_ISREG
#define S_ISREG(mode)   (((mode) & S_IFMT) == S_IFREG)
#endif
#ifndef S_IFIFO
#define S_IFIFO	_S_IFIFO
#endif
#ifndef S_ISFIFO
#define S_ISFIFO(mode)  (((mode) & S_IFMT) == S_IFIFO)
#endif
#ifndef S_ISDIR
#define S_ISDIR(mode)   (((mode) & S_IFMT) == S_IFDIR)
#endif

static char *get_file_extension(const char *pathname)
{
	gchar *filename;
	gchar **components;
	size_t ncomponents;
	GSList *compressed_file_extensions, *compressed_file_extension;
	gchar *extensionp;

	/*
	 * Is the pathname empty?
	 */
	if (strcmp(pathname, "") == 0)
		return NULL;	/* no extension */

	/*
	 * Find the last component of the pathname.
	 */
	filename = g_path_get_basename(pathname);

	/*
	 * Does it have an extension?
	 */
	if (strchr(filename, '.') == NULL) {
		g_free(filename);
		return NULL;	/* no extension whatsoever */
	}

	/*
	 * Yes.  Split it into components separated by ".".
	 */
	components = g_strsplit(filename, ".", 0);
	g_free(filename);

	/*
	 * Count the components.
	 */
	for (ncomponents = 0; components[ncomponents] != NULL; ncomponents++)
		;

	if (ncomponents == 0) {
		g_strfreev(components);
		return NULL;	/* no components */
	}
	if (ncomponents == 1) {
		g_strfreev(components);
		return NULL;	/* only one component, with no "." */
	}

	/*
	 * Is the last component one of the extensions used for compressed
	 * files?
	 */
	compressed_file_extensions = wtap_get_compressed_file_extensions();
	if (compressed_file_extensions == NULL) {
		/*
		 * We don't support reading compressed files, so just
		 * return a copy of whatever extension we did find.
		 */
		extensionp = g_strdup(components[ncomponents - 1]);
		g_strfreev(components);
		return extensionp;
	}
	extensionp = components[ncomponents - 1];
	for (compressed_file_extension = compressed_file_extensions;
	    compressed_file_extension != NULL;
	    compressed_file_extension = g_slist_next(compressed_file_extension)) {
		if (strcmp(extensionp, (char *)compressed_file_extension->data) == 0) {
			/*
			 * Yes, it's one of the compressed-file extensions.
			 * Is there an extension before that?
			 */
			if (ncomponents == 2) {
				g_strfreev(components);
				return NULL;	/* no, only two components */
			}

			/*
			 * Yes, return that extension.
			 */
			extensionp = g_strdup(components[ncomponents - 2]);
			g_strfreev(components);
			return extensionp;
		}
	}

	/*
	 * The extension isn't one of the compressed-file extensions;
	 * return it.
	 */
	extensionp = g_strdup(extensionp);
	g_strfreev(components);
	return extensionp;
}	

gboolean heuristic_uses_extension(unsigned int i, const char *extension)
{
	gchar **extensions_set, **extensionp;

	/*
	 * Does this file type *have* any extensions?
	 */
	if (heuristic_open_info[i].extensions == NULL)
		return FALSE;	/* no */

	/*
	 * Get a list of the extensions used by the specified file type.
	 */
	extensions_set = g_strsplit(heuristic_open_info[i].extensions, ";", 0);

	/*
	 * Check each of them against the specified extension.
	 */
	for (extensionp = extensions_set; *extensionp != NULL;
	    extensionp++) {
		if (strcmp(extension, *extensionp) == 0) {
			g_strfreev(extensions_set);
			return TRUE;	/* it's one of them */
		}
	}
	g_strfreev(extensions_set);
	return FALSE;	/* it's not one of them */
}

/* Opens a file and prepares a wtap struct.
   If "do_random" is TRUE, it opens the file twice; the second open
   allows the application to do random-access I/O without moving
   the seek offset for sequential I/O, which is used by Wireshark
   so that it can do sequential I/O to a capture file that's being
   written to as new packets arrive independently of random I/O done
   to display protocol trees for packets when they're selected. */
wtap* wtap_open_offline(const char *filename, int *err, char **err_info,
			gboolean do_random)
{
	int	fd;
	ws_statb64 statb;
	wtap	*wth;
	unsigned int	i;
	gboolean use_stdin = FALSE;
	gchar *extension;

	/* open standard input if filename is '-' */
	if (strcmp(filename, "-") == 0)
		use_stdin = TRUE;

	/* First, make sure the file is valid */
	if (use_stdin) {
		if (ws_fstat64(0, &statb) < 0) {
			*err = errno;
			return NULL;
		}
	} else {
		if (ws_stat64(filename, &statb) < 0) {
			*err = errno;
			return NULL;
		}
	}
	if (S_ISFIFO(statb.st_mode)) {
		/*
		 * Opens of FIFOs are allowed only when not opening
		 * for random access.
		 *
		 * XXX - currently, we do seeking when trying to find
		 * out the file type, so we don't actually support
		 * opening FIFOs.  However, we may eventually
		 * do buffering that allows us to do at least some
		 * file type determination even on pipes, so we
		 * allow FIFO opens and let things fail later when
		 * we try to seek.
		 */
		if (do_random) {
			*err = WTAP_ERR_RANDOM_OPEN_PIPE;
			return NULL;
		}
	} else if (S_ISDIR(statb.st_mode)) {
		/*
		 * Return different errors for "this is a directory"
		 * and "this is some random special file type", so
		 * the user can get a potentially more helpful error.
		 */
		*err = EISDIR;
		return NULL;
	} else if (! S_ISREG(statb.st_mode)) {
		*err = WTAP_ERR_NOT_REGULAR_FILE;
		return NULL;
	}

	/*
	 * We need two independent descriptors for random access, so
	 * they have different file positions.  If we're opening the
	 * standard input, we can only dup it to get additional
	 * descriptors, so we can't have two independent descriptors,
	 * and thus can't do random access.
	 */
	if (use_stdin && do_random) {
		*err = WTAP_ERR_RANDOM_OPEN_STDIN;
		return NULL;
	}

	errno = ENOMEM;
	wth = (wtap *)g_malloc0(sizeof(wtap));

	/* Open the file */
	errno = WTAP_ERR_CANT_OPEN;
	if (use_stdin) {
		/*
		 * We dup FD 0, so that we don't have to worry about
		 * a file_close of wth->fh closing the standard
		 * input of the process.
		 */
		fd = ws_dup(0);
		if (fd < 0) {
			*err = errno;
			g_free(wth);
			return NULL;
		}
#ifdef _WIN32
		if (_setmode(fd, O_BINARY) == -1) {
			/* "Shouldn't happen" */
			*err = errno;
			g_free(wth);
			return NULL;
		}
#endif
		if (!(wth->fh = file_fdopen(fd))) {
			*err = errno;
			ws_close(fd);
			g_free(wth);
			return NULL;
		}
	} else {
		if (!(wth->fh = file_open(filename))) {
			*err = errno;
			g_free(wth);
			return NULL;
		}
	}

	if (do_random) {
		if (!(wth->random_fh = file_open(filename))) {
			*err = errno;
			file_close(wth->fh);
			g_free(wth);
			return NULL;
		}
	} else
		wth->random_fh = NULL;

	/* initialization */
	wth->file_encap = WTAP_ENCAP_UNKNOWN;
	wth->subtype_sequential_close = NULL;
	wth->subtype_close = NULL;
	wth->tsprecision = WTAP_FILE_TSPREC_USEC;
	wth->priv = NULL;

	init_magic_number_open_routines();
	init_heuristic_open_info();
	if (wth->random_fh) {
		wth->fast_seek = g_ptr_array_new();

		file_set_random_access(wth->fh, FALSE, wth->fast_seek);
		file_set_random_access(wth->random_fh, TRUE, wth->fast_seek);
	}

	/* Try all file types that support magic numbers */
	for (i = 0; i < magic_number_open_routines_arr->len; i++) {
		/* Seek back to the beginning of the file; the open routine
		   for the previous file type may have left the file
		   position somewhere other than the beginning, and the
		   open routine for this file type will probably want
		   to start reading at the beginning.

		   Initialize the data offset while we're at it. */
		if (file_seek(wth->fh, 0, SEEK_SET, err) == -1) {
			/* I/O error - give up */
			wtap_close(wth);
			return NULL;
		}

		switch ((*magic_number_open_routines[i])(wth, err, err_info)) {

		case -1:
			/* I/O error - give up */
			wtap_close(wth);
			return NULL;

		case 0:
			/* No I/O error, but not that type of file */
			break;

		case 1:
			/* We found the file type */
			goto success;
		}
	}

	/* Does this file's name have an extension? */
	extension = get_file_extension(filename);
	if (extension != NULL) {
		/* Yes - try the heuristic types that use that extension first. */
		for (i = 0; i < heuristic_open_info_arr->len; i++) {
			/* Does this type use that extension? */
			if (heuristic_uses_extension(i, extension)) {
				/* Yes. */
				if (file_seek(wth->fh, 0, SEEK_SET, err) == -1) {
					/* I/O error - give up */
					g_free(extension);
					wtap_close(wth);
					return NULL;
				}

				switch ((*heuristic_open_info[i].open_routine)(wth,
				    err, err_info)) {

				case -1:
					/* I/O error - give up */
					g_free(extension);
					wtap_close(wth);
					return NULL;

				case 0:
					/* No I/O error, but not that type of file */
					break;

				case 1:
					/* We found the file type */
					g_free(extension);
					goto success;
				}
			}
		}

		/* Now try the ones that don't use it. */
		for (i = 0; i < heuristic_open_info_arr->len; i++) {
			/* Does this type use that extension? */
			if (!heuristic_uses_extension(i, extension)) {
				/* No. */
				if (file_seek(wth->fh, 0, SEEK_SET, err) == -1) {
					/* I/O error - give up */
					g_free(extension);
					wtap_close(wth);
					return NULL;
				}

				switch ((*heuristic_open_info[i].open_routine)(wth,
				    err, err_info)) {

				case -1:
					/* I/O error - give up */
					g_free(extension);
					wtap_close(wth);
					return NULL;

				case 0:
					/* No I/O error, but not that type of file */
					break;

				case 1:
					/* We found the file type */
					g_free(extension);
					goto success;
				}
			}
		}
		g_free(extension);
	} else {
		/* No - try all the heuristics types in order. */
		for (i = 0; i < heuristic_open_info_arr->len; i++) {
			if (file_seek(wth->fh, 0, SEEK_SET, err) == -1) {
				/* I/O error - give up */
				wtap_close(wth);
				return NULL;
			}

			switch ((*heuristic_open_info[i].open_routine)(wth,
			    err, err_info)) {

			case -1:
				/* I/O error - give up */
				wtap_close(wth);
				return NULL;

			case 0:
				/* No I/O error, but not that type of file */
				break;

			case 1:
				/* We found the file type */
				goto success;
			}
		}
	}

	/* Well, it's not one of the types of file we know about. */
	wtap_close(wth);
	*err = WTAP_ERR_FILE_UNKNOWN_FORMAT;
	return NULL;

success:
	wth->frame_buffer = (struct Buffer *)g_malloc(sizeof(struct Buffer));
	buffer_init(wth->frame_buffer, 1500);

	if(wth->file_type_subtype == WTAP_FILE_TYPE_SUBTYPE_PCAP){

		wtapng_if_descr_t descr;

		descr.wtap_encap = wth->file_encap;
		descr.time_units_per_second = 1000000; /* default microsecond resolution */
		descr.link_type = wtap_wtap_encap_to_pcap_encap(wth->file_encap);
		descr.snap_len = wth->snapshot_length;
		descr.opt_comment = NULL;
		descr.if_name = NULL;
		descr.if_description = NULL;
		descr.if_speed = 0;
		descr.if_tsresol = 6;
		descr.if_filter_str= NULL;
		descr.bpf_filter_len= 0;
		descr.if_filter_bpf_bytes= NULL;
		descr.if_os = NULL;
		descr.if_fcslen = -1;
		descr.num_stat_entries = 0;          /* Number of ISB:s */
		descr.interface_statistics = NULL;
		wth->number_of_interfaces= 1;
		wth->interface_data= g_array_new(FALSE, FALSE, sizeof(wtapng_if_descr_t));
		g_array_append_val(wth->interface_data, descr);

	}
	return wth;
}

/*
 * Given the pathname of the file we just closed with wtap_fdclose(), attempt
 * to reopen that file and assign the new file descriptor(s) to the sequential
 * stream and, if do_random is TRUE, to the random stream.  Used on Windows
 * after the rename of a file we had open was done or if the rename of a
 * file on top of a file we had open failed.
 *
 * This is only required by Wireshark, not TShark, and, at the point that
 * Wireshark is doing this, the sequential stream is closed, and the
 * random stream is open, so this refuses to open pipes, and only
 * reopens the random stream.
 */
gboolean
wtap_fdreopen(wtap *wth, const char *filename, int *err)
{
	ws_statb64 statb;

	/*
	 * We need two independent descriptors for random access, so
	 * they have different file positions.  If we're opening the
	 * standard input, we can only dup it to get additional
	 * descriptors, so we can't have two independent descriptors,
	 * and thus can't do random access.
	 */
	if (strcmp(filename, "-") == 0) {
		*err = WTAP_ERR_RANDOM_OPEN_STDIN;
		return FALSE;
	}

	/* First, make sure the file is valid */
	if (ws_stat64(filename, &statb) < 0) {
		*err = errno;
		return FALSE;
	}
	if (S_ISFIFO(statb.st_mode)) {
		/*
		 * Opens of FIFOs are not allowed; see above.
		 */
		*err = WTAP_ERR_RANDOM_OPEN_PIPE;
		return FALSE;
	} else if (S_ISDIR(statb.st_mode)) {
		/*
		 * Return different errors for "this is a directory"
		 * and "this is some random special file type", so
		 * the user can get a potentially more helpful error.
		 */
		*err = EISDIR;
		return FALSE;
	} else if (! S_ISREG(statb.st_mode)) {
		*err = WTAP_ERR_NOT_REGULAR_FILE;
		return FALSE;
	}

	/* Open the file */
	errno = WTAP_ERR_CANT_OPEN;
	if (!file_fdreopen(wth->random_fh, filename)) {
		*err = errno;
		return FALSE;
	}
	return TRUE;
}

/* Table of the file types we know about.
   Entries must be sorted by WTAP_FILE_TYPE_SUBTYPE_xxx values in ascending order */
static const struct file_type_subtype_info dump_open_table_base[] = {
	/* WTAP_FILE_TYPE_SUBTYPE_UNKNOWN (only used internally for initialization) */
	{ NULL, NULL, NULL, NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_PCAP */
        /* Gianluca Varenni suggests that we add "deprecated" to the description. */
	{ "Wireshark/tcpdump/... - pcap", "pcap", "pcap", "cap;dmp",
	  FALSE, FALSE, 0,
	  libpcap_dump_can_write_encap, libpcap_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_PCAPNG */
	{ "Wireshark/... - pcapng", "pcapng", "pcapng", "ntar",
	  FALSE, TRUE, WTAP_COMMENT_PER_SECTION|WTAP_COMMENT_PER_INTERFACE|WTAP_COMMENT_PER_PACKET,
	  pcapng_dump_can_write_encap, pcapng_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_PCAP_NSEC */
	{ "Wireshark - nanosecond libpcap", "nseclibpcap", "pcap", "cap;dmp",
	  FALSE, FALSE, 0,
	  libpcap_dump_can_write_encap, libpcap_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_PCAP_AIX */
	{ "AIX tcpdump - libpcap", "aixlibpcap", "pcap", "cap;dmp",
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_PCAP_SS991029 */
	{ "Modified tcpdump - libpcap", "modlibpcap", "pcap", "cap;dmp",
	  FALSE, FALSE, 0,
	  libpcap_dump_can_write_encap, libpcap_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_PCAP_NOKIA */
	{ "Nokia tcpdump - libpcap ", "nokialibpcap", "pcap", "cap;dmp",
	  FALSE, FALSE, 0,
	  libpcap_dump_can_write_encap, libpcap_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_PCAP_SS990417 */
	{ "RedHat 6.1 tcpdump - libpcap", "rh6_1libpcap", "pcap", "cap;dmp",
	  FALSE, FALSE, 0,
	  libpcap_dump_can_write_encap, libpcap_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_PCAP_SS990915 */
	{ "SuSE 6.3 tcpdump - libpcap", "suse6_3libpcap", "pcap", "cap;dmp",
	  FALSE, FALSE, 0,
	  libpcap_dump_can_write_encap, libpcap_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_5VIEWS */
	{ "InfoVista 5View capture", "5views", "5vw", NULL,
	   TRUE, FALSE, 0,
	  _5views_dump_can_write_encap, _5views_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_IPTRACE_1_0 */
	{ "AIX iptrace 1.0", "iptrace_1", NULL, NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_IPTRACE_2_0 */
	{ "AIX iptrace 2.0", "iptrace_2", NULL, NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_BER */
	{ "ASN.1 Basic Encoding Rules", "ber", NULL, NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_HCIDUMP */
	{ "Bluetooth HCI dump", "hcidump", NULL, NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_CATAPULT_DCT2000 */
	{ "Catapult DCT2000 trace (.out format)", "dct2000", "out", NULL,
	  FALSE, FALSE, 0,
	  catapult_dct2000_dump_can_write_encap, catapult_dct2000_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_NETXRAY_OLD */
	{ "Cinco Networks NetXRay 1.x", "netxray1", "cap", NULL,
	  TRUE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_NETXRAY_1_0 */
	{ "Cinco Networks NetXRay 2.0 or later", "netxray2", "cap", NULL,
	  TRUE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_COSINE */
	{ "CoSine IPSX L2 capture", "cosine", "txt", NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_CSIDS */
	{ "CSIDS IPLog", "csids", NULL, NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_DBS_ETHERWATCH */
	{ "DBS Etherwatch (VMS)", "etherwatch", "txt", NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL},

	/* WTAP_FILE_TYPE_SUBTYPE_ERF */
	{ "Endace ERF capture", "erf", "erf", NULL,
	  FALSE, FALSE, 0,
	  erf_dump_can_write_encap, erf_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_EYESDN */
	{ "EyeSDN USB S0/E1 ISDN trace format", "eyesdn", "trc", NULL,
	   FALSE, FALSE, 0,
	   eyesdn_dump_can_write_encap, eyesdn_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_NETTL */
	{ "HP-UX nettl trace", "nettl", "trc0", "trc1",
	  FALSE, FALSE, 0,
	  nettl_dump_can_write_encap, nettl_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_ISERIES */
	{ "IBM iSeries comm. trace (ASCII)", "iseries_ascii", "txt", NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_ISERIES_UNICODE */
	{ "IBM iSeries comm. trace (UNICODE)", "iseries_unicode", "txt", NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_I4BTRACE */
	{ "I4B ISDN trace", "i4btrace", NULL, NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_ASCEND */
	{ "Lucent/Ascend access server trace", "ascend", "txt", NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_NETMON_1_x */
	{ "Microsoft NetMon 1.x", "netmon1", "cap", NULL,
	  TRUE, FALSE, 0,
	  netmon_dump_can_write_encap_1_x, netmon_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_NETMON_2_x */
	{ "Microsoft NetMon 2.x", "netmon2", "cap", NULL,
	  TRUE, FALSE, 0,
	  netmon_dump_can_write_encap_2_x, netmon_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_NGSNIFFER_UNCOMPRESSED */
	{ "Sniffer (DOS)", "ngsniffer", "cap", "enc;trc;fdc;syc",
	  FALSE, FALSE, 0,
	  ngsniffer_dump_can_write_encap, ngsniffer_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_NGSNIFFER_COMPRESSED */
	{ "Sniffer (DOS), compressed", "ngsniffer_comp", "cap", "enc;trc;fdc;syc",
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_NETXRAY_1_1 */
	{ "NetXray, Sniffer (Windows) 1.1", "ngwsniffer_1_1", "cap", NULL,
	  TRUE, FALSE, 0,
	  netxray_dump_can_write_encap_1_1, netxray_dump_open_1_1 },

	/* WTAP_FILE_TYPE_SUBTYPE_NETXRAY_2_00x */
	{ "Sniffer (Windows) 2.00x", "ngwsniffer_2_0", "cap", "caz",
	  TRUE, FALSE, 0,
	  netxray_dump_can_write_encap_2_0, netxray_dump_open_2_0 },

	/* WTAP_FILE_TYPE_SUBTYPE_NETWORK_INSTRUMENTS */
	{ "Network Instruments Observer", "niobserver", "bfr", NULL,
	  FALSE, FALSE, 0,
	  network_instruments_dump_can_write_encap, network_instruments_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_LANALYZER */
	{ "Novell LANalyzer","lanalyzer", "tr1", NULL,
	  TRUE, FALSE, 0,
	  lanalyzer_dump_can_write_encap, lanalyzer_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_PPPDUMP */
	{ "pppd log (pppdump format)", "pppd", NULL, NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_RADCOM */
	{ "RADCOM WAN/LAN analyzer", "radcom", NULL, NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_SNOOP */
	{ "Sun snoop", "snoop", "snoop", "cap",
	  FALSE, FALSE, 0,
	  snoop_dump_can_write_encap, snoop_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_SHOMITI */
	{ "Shomiti/Finisar Surveyor", "shomiti", "cap", NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_VMS */
	{ "TCPIPtrace (VMS)", "tcpiptrace", "txt", NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL},

	/* WTAP_FILE_TYPE_SUBTYPE_K12 */
	{ "Tektronix K12xx 32-bit .rf5 format", "rf5", "rf5", NULL,
	   TRUE, FALSE, 0,
	   k12_dump_can_write_encap, k12_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_TOSHIBA */
	{ "Toshiba Compact ISDN Router snoop", "toshiba", "txt", NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_VISUAL_NETWORKS */
	{ "Visual Networks traffic capture", "visual", NULL, NULL,
	  TRUE, FALSE, 0,
	  visual_dump_can_write_encap, visual_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_PEEKCLASSIC_V56 */
	{ "WildPackets classic (V5 and V6)", "peekclassic56", "pkt", "tpc;apc;wpz",
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_PEEKCLASSIC_V7 */
	{ "WildPackets classic (V7)", "peekclassic7", "pkt", "tpc;apc;wpz",
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_PEEKTAGGED */
	{ "WildPackets tagged", "peektagged", "pkt", "tpc;apc;wpz",
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_MPEG */
	{ "MPEG", "mpeg", "mpeg", "mpg;mp3",
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_K12TEXT  */
	{ "K12 text file", "k12text", "txt", NULL,
	  FALSE, FALSE, 0,
	  k12text_dump_can_write_encap, k12text_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_NETSCREEN */
	{ "NetScreen snoop text file", "netscreen", "txt", NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_COMMVIEW */
	{ "TamoSoft CommView", "commview", "ncf", NULL,
	  FALSE, FALSE, 0,
	  commview_dump_can_write_encap, commview_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_BTSNOOP */
	{ "Symbian OS btsnoop", "btsnoop", "log", NULL,
	  FALSE, FALSE, 0,
	  btsnoop_dump_can_write_encap, btsnoop_dump_open_h4 },

	/* WTAP_FILE_TYPE_SUBTYPE_TNEF */
	{ "Transport-Neutral Encapsulation Format", "tnef", NULL, NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_DCT3TRACE */
	{ "Gammu DCT3 trace", "dct3trace", "xml", NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_PACKETLOGGER */
	{ "PacketLogger", "pklg", "pklg", NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_DAINTREE_SNA */
	{ "Daintree SNA", "dsna", "dcf", NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_NETSCALER_1_0 */
	{ "NetScaler Trace (Version 1.0)", "nstrace10", NULL, NULL,
	  TRUE, FALSE, 0,
	  nstrace_10_dump_can_write_encap, nstrace_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_NETSCALER_2_0 */
	{ "NetScaler Trace (Version 2.0)", "nstrace20", "cap", NULL,
	  TRUE, FALSE, 0,
	  nstrace_20_dump_can_write_encap, nstrace_dump_open },

	/* WTAP_FILE_TYPE_SUBTYPE_JPEG_JFIF */
	{ "JPEG/JFIF", "jpeg", "jpg", "jpeg;jfif",
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_IPFIX */
	{ "IPFIX File Format", "ipfix", "pfx", "ipfix",
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_ENCAP_MIME */
	{ "MIME File Format", "mime", NULL, NULL,
	   FALSE, FALSE, 0,
	   NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_AETHRA */
	{ "Aethra .aps file", "aethra", "aps", NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_MPEG_2_TS */
	{ "MPEG2 transport stream", "mp2t", "mp2t", "ts;mpg",
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_VWR_80211 */
	{ "Ixia IxVeriWave .vwr Raw 802.11 Capture", "vwr80211", "vwr", NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_VWR_ETH */
	{ "Ixia IxVeriWave .vwr Raw Ethernet Capture", "vwreth", "vwr", NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_CAMINS */
	{ "CAM Inspector file", "camins", "camins", NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL },

	/* WTAP_FILE_TYPE_SUBTYPE_STANAG_4607 */
	{ "STANAG 4607 Format", "stanag4607", NULL, NULL,
	  FALSE, FALSE, 0,
	  NULL, NULL }

};

gint wtap_num_file_types_subtypes = sizeof(dump_open_table_base) / sizeof(struct file_type_subtype_info);

static GArray*  dump_open_table_arr = NULL;
static const struct file_type_subtype_info* dump_open_table = dump_open_table_base;

/* initialize the file types array if it has not being initialized yet */
static void init_file_types_subtypes(void) {

	if (dump_open_table_arr) return;

	dump_open_table_arr = g_array_new(FALSE,TRUE,sizeof(struct file_type_subtype_info));

	g_array_append_vals(dump_open_table_arr,dump_open_table_base,wtap_num_file_types_subtypes);

	dump_open_table = (const struct file_type_subtype_info*)(void *)dump_open_table_arr->data;
}

int wtap_register_file_type_subtypes(const struct file_type_subtype_info* fi) {
	init_file_types_subtypes();

	g_array_append_val(dump_open_table_arr,*fi);

	dump_open_table = (const struct file_type_subtype_info*)(void *)dump_open_table_arr->data;

	return wtap_num_file_types_subtypes++;
}

int wtap_get_num_file_types_subtypes(void)
{
	return wtap_num_file_types_subtypes;
}

/*
 * Given a GArray of WTAP_ENCAP_ types, return the per-file encapsulation
 * type that would be needed to write out a file with those types.  If
 * there's only one type, it's that type, otherwise it's
 * WTAP_ENCAP_PER_PACKET.
 */
int
wtap_dump_file_encap_type(const GArray *file_encaps)
{
	int encap;

	encap = WTAP_ENCAP_PER_PACKET;
	if (file_encaps->len == 1) {
		/* OK, use the one-and-only encapsulation type. */
		encap = g_array_index(file_encaps, gint, 0);
	}
	return encap;
}

static gboolean
wtap_dump_can_write_encap(int filetype, int encap)
{
	if (filetype < 0 || filetype >= wtap_num_file_types_subtypes
	    || dump_open_table[filetype].can_write_encap == NULL)
		return FALSE;

	if ((*dump_open_table[filetype].can_write_encap)(encap) != 0)
		return FALSE;

	return TRUE;
}

/*
 * Return TRUE if a capture with a given GArray of encapsulation types
 * and a given bitset of comment types can be written in a specified
 * format, and FALSE if it can't.
 */
static gboolean
wtap_dump_can_write_format(int ft, const GArray *file_encaps,
    guint32 required_comment_types)
{
	guint i;

	/*
	 * Can we write in this format?
	 */
	if (!wtap_dump_can_open(ft)) {
		/* No. */
		return FALSE;
	}

	/*
	 * Yes.  Can we write out all the required comments in this
	 * format?
	 */
	if (!wtap_dump_supports_comment_types(ft, required_comment_types)) {
		/* No. */
		return FALSE;
	}

	/*
	 * Yes.  Is the required per-file encapsulation type supported?
	 * This might be WTAP_ENCAP_PER_PACKET.
	 */
	if (!wtap_dump_can_write_encap(ft, wtap_dump_file_encap_type(file_encaps))) {
		/* No. */
		return FALSE;
	}

	/*
	 * Yes.  Are all the individual encapsulation types supported?
	 */
	for (i = 0; i < file_encaps->len; i++) {
		if (!wtap_dump_can_write_encap(ft,
		    g_array_index(file_encaps, int, i))) {
			/* No - one of them isn't. */
			return FALSE;
		}
	}

	/* Yes - we're OK. */
	return TRUE;
}

/**
 * Return TRUE if we can write a file with the given GArray of
 * encapsulation types and the given bitmask of comment types.
 */
gboolean
wtap_dump_can_write(const GArray *file_encaps, guint32 required_comment_types)
{
  int ft;

  for (ft = 0; ft < WTAP_NUM_FILE_TYPES_SUBTYPES; ft++) {
    /* To save a file with Wiretap, Wiretap has to handle that format,
       and its code to handle that format must be able to write a file
       with this file's encapsulation types. */
    if (wtap_dump_can_write_format(ft, file_encaps, required_comment_types)) {
      /* OK, we can write it out in this type. */
      return TRUE;
    }
  }

  /* No, we couldn't save it in any format. */
  return FALSE;
}

/**
 * Get a GArray of WTAP_FILE_TYPE_SUBTYPE_ values for file types/subtypes
 * that can be used to save a file of a given type/subtype with a given
 * GArray of encapsulation types and the given bitmask of comment types.
 */
GArray *
wtap_get_savable_file_types_subtypes(int file_type_subtype,
    const GArray *file_encaps, guint32 required_comment_types)
{
	GArray *savable_file_types_subtypes;
	int ft;
	int default_file_type_subtype = -1;
	int other_file_type_subtype = -1;

	/* Can we save this file in its own file type/subtype? */
	if (wtap_dump_can_write_format(file_type_subtype, file_encaps,
	                               required_comment_types)) {
		/* Yes - make that the default file type/subtype. */
		default_file_type_subtype = file_type_subtype;
	} else {
		/* OK, find the first file type/subtype we *can* save it as. */
		default_file_type_subtype = -1;
		for (ft = 0; ft < WTAP_NUM_FILE_TYPES_SUBTYPES; ft++) {
			if (wtap_dump_can_write_format(ft, file_encaps,
			                               required_comment_types)) {
				/* OK, got it. */
				default_file_type_subtype = ft;
			}
		}
	}

	if (default_file_type_subtype == -1) {
		/* We don't support writing this file as any file type/subtype. */
		return NULL;
	}

	/* Allocate the array. */
	savable_file_types_subtypes = g_array_new(FALSE, FALSE, (guint)sizeof (int));

	/* Put the default file type/subtype first in the list. */
	g_array_append_val(savable_file_types_subtypes, default_file_type_subtype);

	/* If the default is pcap, put pcap-NG right after it if we can
	   also write it in pcap-NG format; otherwise, if the default is
	   pcap-NG, put pcap right after it if we can also write it in
	   pcap format. */
	if (default_file_type_subtype == WTAP_FILE_TYPE_SUBTYPE_PCAP) {
		if (wtap_dump_can_write_format(WTAP_FILE_TYPE_SUBTYPE_PCAPNG, file_encaps,
		                               required_comment_types))
			other_file_type_subtype = WTAP_FILE_TYPE_SUBTYPE_PCAPNG;
	} else if (default_file_type_subtype == WTAP_FILE_TYPE_SUBTYPE_PCAPNG) {
		if (wtap_dump_can_write_format(WTAP_FILE_TYPE_SUBTYPE_PCAP, file_encaps,
		                               required_comment_types))
			other_file_type_subtype = WTAP_FILE_TYPE_SUBTYPE_PCAP;
	}
	if (other_file_type_subtype != -1)
		g_array_append_val(savable_file_types_subtypes, other_file_type_subtype);

	/* Add all the other file types/subtypes that work. */
	for (ft = 0; ft < WTAP_NUM_FILE_TYPES_SUBTYPES; ft++) {
		if (ft == WTAP_FILE_TYPE_SUBTYPE_UNKNOWN)
			continue;	/* not a real file type */
		if (ft == default_file_type_subtype || ft == other_file_type_subtype)
			continue;	/* we've already done this one */
		if (wtap_dump_can_write_format(ft, file_encaps,
		                               required_comment_types)) {
			/* OK, we can write it out in this type. */
			g_array_append_val(savable_file_types_subtypes, ft);
		}
	}

	return savable_file_types_subtypes;
}

/* Name that should be somewhat descriptive. */
const char *wtap_file_type_subtype_string(int file_type_subtype)
{
	if (file_type_subtype < 0 || file_type_subtype >= wtap_num_file_types_subtypes) {
		g_error("Unknown capture file type %d", file_type_subtype);
		/** g_error() does an abort() and thus never returns **/
		return "";
	} else
		return dump_open_table[file_type_subtype].name;
}

/* Name to use in, say, a command-line flag specifying the type/subtype. */
const char *wtap_file_type_subtype_short_string(int file_type_subtype)
{
	if (file_type_subtype < 0 || file_type_subtype >= wtap_num_file_types_subtypes)
		return NULL;
	else
		return dump_open_table[file_type_subtype].short_name;
}

/* Translate a short name to a capture file type/subtype. */
int wtap_short_string_to_file_type_subtype(const char *short_name)
{
	int file_type_subtype;

	for (file_type_subtype = 0; file_type_subtype < wtap_num_file_types_subtypes; file_type_subtype++) {
		if (dump_open_table[file_type_subtype].short_name != NULL &&
		    strcmp(short_name, dump_open_table[file_type_subtype].short_name) == 0)
			return file_type_subtype;
	}

	/*
	 * We now call the "libpcap" file format just "pcap", but we
	 * allow it to be specified as "libpcap" as well, for
	 * backwards compatibility.
	 */
	if (strcmp(short_name, "libpcap") == 0)
		return WTAP_FILE_TYPE_SUBTYPE_PCAP;

	return -1;	/* no such file type, or we can't write it */
}

static GSList *
add_extensions_for_file_type_subtype(int file_type_subtype, GSList *extensions,
    GSList *compressed_file_extensions)
{
	gchar **extensions_set, **extensionp;
	gchar *extension;

	/*
	 * Add the default extension, and all compressed variants of
	 * it.
	 */
	extensions = add_extensions(extensions,
	    dump_open_table[file_type_subtype].default_file_extension,
	    compressed_file_extensions);

	if (dump_open_table[file_type_subtype].additional_file_extensions != NULL) {
		/*
		 * We have additional extensions; add them.
		 *
		 * First, split the extension-list string into a set of
		 * extensions.
		 */
		extensions_set = g_strsplit(dump_open_table[file_type_subtype].additional_file_extensions,
		    ";", 0);

		/*
		 * Add each of those extensions to the list.
		 */
		for (extensionp = extensions_set; *extensionp != NULL;
		    extensionp++) {
			extension = *extensionp;

			/*
			 * Add the extension, and all compressed variants
			 * of it.
			 */
			extensions = add_extensions(extensions, extension,
			    compressed_file_extensions);
		}

		g_strfreev(extensions_set);
	}
	return extensions;
}

/* Return a list of file extensions that are used by the specified file type.

   If include_compressed is TRUE, the list will include compressed
   extensions, e.g. not just "pcap" but also "pcap.gz" if we can read
   gzipped files.

   All strings in the list are allocated with g_malloc() and must be freed
   with g_free(). */
GSList *wtap_get_file_extensions_list(int file_type_subtype, gboolean include_compressed)
{
	GSList *compressed_file_extensions;
	GSList *extensions;

	if (file_type_subtype < 0 || file_type_subtype >= wtap_num_file_types_subtypes)
		return NULL;	/* not a valid file type */

	if (dump_open_table[file_type_subtype].default_file_extension == NULL)
		return NULL;	/* valid, but no extensions known */

	extensions = NULL;	/* empty list, to start with */

	/*
	 * If include_compressions is true, get the list of compressed-file
	 * extensions.
	 */
	if (include_compressed)
		compressed_file_extensions = wtap_get_compressed_file_extensions();
	else
		compressed_file_extensions = NULL;

	/*
	 * Add all this file type's extensions, with compressed
	 * variants.
	 */
	extensions = add_extensions_for_file_type_subtype(file_type_subtype, extensions,
	    compressed_file_extensions);

	g_slist_free(compressed_file_extensions);
	return extensions;
}

/*
 * Free a list returned by wtap_get_file_extension_type_extensions(),
 * wtap_get_all_file_extensions_list, or wtap_get_file_extensions_list().
 */
void wtap_free_extensions_list(GSList *extensions)
{
	GSList *extension;

	for (extension = extensions; extension != NULL;
	    extension = g_slist_next(extension)) {
		g_free(extension->data);
	}
	g_slist_free(extensions);
}

/* Return the default file extension to use with the specified file type;
   that's just the extension, without any ".". */
const char *wtap_default_file_extension(int file_type_subtype)
{
	if (file_type_subtype < 0 || file_type_subtype >= wtap_num_file_types_subtypes)
		return NULL;
	else
		return dump_open_table[file_type_subtype].default_file_extension;
}

gboolean wtap_dump_can_open(int file_type_subtype)
{
	if (file_type_subtype < 0 || file_type_subtype >= wtap_num_file_types_subtypes
	    || dump_open_table[file_type_subtype].dump_open == NULL)
		return FALSE;

	return TRUE;
}

#ifdef HAVE_LIBZ
gboolean wtap_dump_can_compress(int file_type_subtype)
{
	/*
	 * If this is an unknown file type, or if we have to
	 * seek when writing out a file with this file type,
	 * return FALSE.
	 */
	if (file_type_subtype < 0 || file_type_subtype >= wtap_num_file_types_subtypes
	    || dump_open_table[file_type_subtype].writing_must_seek)
		return FALSE;

	return TRUE;
}
#else
gboolean wtap_dump_can_compress(int file_type_subtype _U_)
{
	return FALSE;
}
#endif

gboolean wtap_dump_has_name_resolution(int file_type_subtype)
{
	if (file_type_subtype < 0 || file_type_subtype >= wtap_num_file_types_subtypes
	    || dump_open_table[file_type_subtype].has_name_resolution == FALSE)
		return FALSE;

	return TRUE;
}

gboolean wtap_dump_supports_comment_types(int file_type_subtype, guint32 comment_types)
{
	guint32 supported_comment_types;

	if (file_type_subtype < 0 || file_type_subtype >= wtap_num_file_types_subtypes)
		return FALSE;

	supported_comment_types = dump_open_table[file_type_subtype].supported_comment_types;

	if ((comment_types & supported_comment_types) == comment_types)
		return TRUE;
	return FALSE;
}

static gboolean wtap_dump_open_check(int file_type_subtype, int encap, gboolean comressed, int *err);
static wtap_dumper* wtap_dump_alloc_wdh(int file_type_subtype, int encap, int snaplen,
					gboolean compressed, int *err);
static gboolean wtap_dump_open_finish(wtap_dumper *wdh, int file_type_subtype, gboolean compressed, int *err);

static WFILE_T wtap_dump_file_open(wtap_dumper *wdh, const char *filename);
static WFILE_T wtap_dump_file_fdopen(wtap_dumper *wdh, int fd);
static int wtap_dump_file_close(wtap_dumper *wdh);

wtap_dumper* wtap_dump_open(const char *filename, int file_type_subtype, int encap,
				int snaplen, gboolean compressed, int *err)
{
	return wtap_dump_open_ng(filename, file_type_subtype, encap,snaplen, compressed, NULL, NULL, err);
}

static wtap_dumper *
wtap_dump_init_dumper(int file_type_subtype, int encap, int snaplen, gboolean compressed,
    wtapng_section_t *shb_hdr, wtapng_iface_descriptions_t *idb_inf, int *err)
{
	wtap_dumper *wdh;

	/* Allocate a data structure for the output stream. */
	wdh = wtap_dump_alloc_wdh(file_type_subtype, encap, snaplen, compressed, err);
	if (wdh == NULL)
		return NULL;	/* couldn't allocate it */

	/* Set Section Header Block data */
	wdh->shb_hdr = shb_hdr;
	/* Set Interface Description Block data */
	if ((idb_inf != NULL) && (idb_inf->number_of_interfaces > 0)) {
		wdh->number_of_interfaces = idb_inf->number_of_interfaces;
		wdh->interface_data = idb_inf->interface_data;
	} else {
		wtapng_if_descr_t descr;

		descr.wtap_encap = encap;
		descr.time_units_per_second = 1000000; /* default microsecond resolution */
		descr.link_type = wtap_wtap_encap_to_pcap_encap(encap);
		descr.snap_len = snaplen;
		descr.opt_comment = NULL;
		descr.if_name = g_strdup("Unknown/not available in original file format(libpcap)");
		descr.if_description = NULL;
		descr.if_speed = 0;
		descr.if_tsresol = 6;
		descr.if_filter_str= NULL;
		descr.bpf_filter_len= 0;
		descr.if_filter_bpf_bytes= NULL;
		descr.if_os = NULL;
		descr.if_fcslen = -1;
		descr.num_stat_entries = 0;          /* Number of ISB:s */
		descr.interface_statistics = NULL;
		wdh->number_of_interfaces= 1;
		wdh->interface_data= g_array_new(FALSE, FALSE, sizeof(wtapng_if_descr_t));
		g_array_append_val(wdh->interface_data, descr);
	}
	return wdh;
}

wtap_dumper* wtap_dump_open_ng(const char *filename, int file_type_subtype, int encap,
				int snaplen, gboolean compressed, wtapng_section_t *shb_hdr, wtapng_iface_descriptions_t *idb_inf, int *err)
{
	wtap_dumper *wdh;
	WFILE_T fh;

	/* Check whether we can open a capture file with that file type
	   and that encapsulation. */
	if (!wtap_dump_open_check(file_type_subtype, encap, compressed, err))
		return NULL;

	/* Allocate and initialize a data structure for the output stream. */
	wdh = wtap_dump_init_dumper(file_type_subtype, encap, snaplen, compressed,
	    shb_hdr, idb_inf, err);
	if (wdh == NULL)
		return NULL;

	/* "-" means stdout */
	if (strcmp(filename, "-") == 0) {
		if (compressed) {
			*err = EINVAL;	/* XXX - return a Wiretap error code for this */
			g_free(wdh);
			return NULL;	/* compress won't work on stdout */
		}
#ifdef _WIN32
		if (_setmode(fileno(stdout), O_BINARY) == -1) {
			/* "Should not happen" */
			*err = errno;
			g_free(wdh);
			return NULL;	/* couldn't put standard output in binary mode */
		}
#endif
		wdh->fh = stdout;
	} else {
		/* In case "fopen()" fails but doesn't set "errno", set "errno"
		   to a generic "the open failed" error. */
		errno = WTAP_ERR_CANT_OPEN;
		fh = wtap_dump_file_open(wdh, filename);
		if (fh == NULL) {
			*err = errno;
			g_free(wdh);
			return NULL;	/* can't create file */
		}
		wdh->fh = fh;
	}

	if (!wtap_dump_open_finish(wdh, file_type_subtype, compressed, err)) {
		/* Get rid of the file we created; we couldn't finish
		   opening it. */
		if (wdh->fh != stdout) {
			wtap_dump_file_close(wdh);
			ws_unlink(filename);
		}
		g_free(wdh);
		return NULL;
	}
	return wdh;
}

wtap_dumper* wtap_dump_fdopen(int fd, int file_type_subtype, int encap, int snaplen,
				gboolean compressed, int *err)
{
	return wtap_dump_fdopen_ng(fd, file_type_subtype, encap, snaplen, compressed, NULL, NULL, err);
}

wtap_dumper* wtap_dump_fdopen_ng(int fd, int file_type_subtype, int encap, int snaplen,
				gboolean compressed, wtapng_section_t *shb_hdr, wtapng_iface_descriptions_t *idb_inf, int *err)
{
	wtap_dumper *wdh;
	WFILE_T fh;

	/* Check whether we can open a capture file with that file type
	   and that encapsulation. */
	if (!wtap_dump_open_check(file_type_subtype, encap, compressed, err))
		return NULL;

	/* Allocate and initialize a data structure for the output stream. */
	wdh = wtap_dump_init_dumper(file_type_subtype, encap, snaplen, compressed,
	    shb_hdr, idb_inf, err);
	if (wdh == NULL)
		return NULL;

#ifdef _WIN32
	if (fd == 1) {
		if (_setmode(fileno(stdout), O_BINARY) == -1) {
			/* "Should not happen" */
			*err = errno;
			g_free(wdh);
			return NULL;	/* couldn't put standard output in binary mode */
		}
	}
#endif

	/* In case "fopen()" fails but doesn't set "errno", set "errno"
	   to a generic "the open failed" error. */
	errno = WTAP_ERR_CANT_OPEN;
	fh = wtap_dump_file_fdopen(wdh, fd);
	if (fh == NULL) {
		*err = errno;
		g_free(wdh);
		return NULL;	/* can't create standard I/O stream */
	}
	wdh->fh = fh;

	if (!wtap_dump_open_finish(wdh, file_type_subtype, compressed, err)) {
		wtap_dump_file_close(wdh);
		g_free(wdh);
		return NULL;
	}
	return wdh;
}

static gboolean wtap_dump_open_check(int file_type_subtype, int encap, gboolean compressed, int *err)
{
	if (!wtap_dump_can_open(file_type_subtype)) {
		/* Invalid type, or type we don't know how to write. */
		*err = WTAP_ERR_UNSUPPORTED_FILE_TYPE;
		return FALSE;
	}

	/* OK, we know how to write that type; can we write the specified
	   encapsulation type? */
	*err = (*dump_open_table[file_type_subtype].can_write_encap)(encap);
	if (*err != 0)
		return FALSE;

	/* if compression is wanted, do we support this for this file_type_subtype? */
	if(compressed && !wtap_dump_can_compress(file_type_subtype)) {
		*err = WTAP_ERR_COMPRESSION_NOT_SUPPORTED;
		return FALSE;
	}

	*err = (*dump_open_table[file_type_subtype].can_write_encap)(encap);
	if (*err != 0)
		return FALSE;

	/* All systems go! */
	return TRUE;
}

static wtap_dumper* wtap_dump_alloc_wdh(int file_type_subtype, int encap, int snaplen,
					gboolean compressed, int *err)
{
	wtap_dumper *wdh;

	wdh = (wtap_dumper *)g_malloc0(sizeof (wtap_dumper));
	if (wdh == NULL) {
		*err = errno;
		return NULL;
	}

	wdh->file_type_subtype = file_type_subtype;
	wdh->snaplen = snaplen;
	wdh->encap = encap;
	wdh->compressed = compressed;
	return wdh;
}

static gboolean wtap_dump_open_finish(wtap_dumper *wdh, int file_type_subtype, gboolean compressed, int *err)
{
	int fd;
	gboolean cant_seek;

	/* Can we do a seek on the file descriptor?
	   If not, note that fact. */
	if(compressed) {
		cant_seek = TRUE;
	} else {
		fd = fileno((FILE *)wdh->fh);
		if (lseek(fd, 1, SEEK_CUR) == -1)
			cant_seek = TRUE;
		else {
			/* Undo the seek. */
			lseek(fd, 0, SEEK_SET);
			cant_seek = FALSE;
		}
	}

	/* If this file type requires seeking, and we can't seek, fail. */
	if (dump_open_table[file_type_subtype].writing_must_seek && cant_seek) {
		*err = WTAP_ERR_CANT_WRITE_TO_PIPE;
		return FALSE;
	}

	/* Now try to open the file for writing. */
	if (!(*dump_open_table[file_type_subtype].dump_open)(wdh, err)) {
		return FALSE;
	}

	return TRUE;	/* success! */
}

gboolean wtap_dump(wtap_dumper *wdh, const struct wtap_pkthdr *phdr,
		   const guint8 *pd, int *err)
{
	return (wdh->subtype_write)(wdh, phdr, pd, err);
}

void wtap_dump_flush(wtap_dumper *wdh)
{
#ifdef HAVE_LIBZ
	if(wdh->compressed) {
		gzwfile_flush((GZWFILE_T)wdh->fh);
	} else
#endif
	{
		fflush((FILE *)wdh->fh);
	}
}

gboolean wtap_dump_close(wtap_dumper *wdh, int *err)
{
	gboolean ret = TRUE;

	if (wdh->subtype_close != NULL) {
		/* There's a close routine for this dump stream. */
		if (!(wdh->subtype_close)(wdh, err))
			ret = FALSE;
	}
	errno = WTAP_ERR_CANT_CLOSE;
	/* Don't close stdout */
	if (wdh->fh != stdout) {
		if (wtap_dump_file_close(wdh) == EOF) {
			if (ret) {
				/* The per-format close function succeeded,
				   but the fclose didn't.  Save the reason
				   why, if our caller asked for it. */
				if (err != NULL)
					*err = errno;
			}
			ret = FALSE;
		}
	} else {
		/* as we don't close stdout, at least try to flush it */
		wtap_dump_flush(wdh);
	}
	if (wdh->priv != NULL)
		g_free(wdh->priv);
	g_free(wdh);
	return ret;
}

gint64 wtap_get_bytes_dumped(wtap_dumper *wdh)
{
	return wdh->bytes_dumped;
}

void wtap_set_bytes_dumped(wtap_dumper *wdh, gint64 bytes_dumped)
{
	wdh->bytes_dumped = bytes_dumped;
}

gboolean wtap_dump_set_addrinfo_list(wtap_dumper *wdh, addrinfo_lists_t *addrinfo_lists)
{
	if (!wdh || wdh->file_type_subtype < 0 || wdh->file_type_subtype >= wtap_num_file_types_subtypes
		|| dump_open_table[wdh->file_type_subtype].has_name_resolution == FALSE)
			return FALSE;
	wdh->addrinfo_lists = addrinfo_lists;
	return TRUE;
}

/* internally open a file for writing (compressed or not) */
#ifdef HAVE_LIBZ
static WFILE_T wtap_dump_file_open(wtap_dumper *wdh, const char *filename)
{
	if(wdh->compressed) {
		return gzwfile_open(filename);
	} else {
		return ws_fopen(filename, "wb");
	}
}
#else
static WFILE_T wtap_dump_file_open(wtap_dumper *wdh _U_, const char *filename)
{
	return ws_fopen(filename, "wb");
}
#endif

/* internally open a file for writing (compressed or not) */
#ifdef HAVE_LIBZ
static WFILE_T wtap_dump_file_fdopen(wtap_dumper *wdh, int fd)
{
	if(wdh->compressed) {
		return gzwfile_fdopen(fd);
	} else {
		return fdopen(fd, "wb");
	}
}
#else
static WFILE_T wtap_dump_file_fdopen(wtap_dumper *wdh _U_, int fd)
{
	return fdopen(fd, "wb");
}
#endif

/* internally writing raw bytes (compressed or not) */
gboolean wtap_dump_file_write(wtap_dumper *wdh, const void *buf, size_t bufsize,
		     int *err)
{
	size_t nwritten;

#ifdef HAVE_LIBZ
	if (wdh->compressed) {
		nwritten = gzwfile_write((GZWFILE_T)wdh->fh, buf, (unsigned) bufsize);
		/*
		 * gzwfile_write() returns 0 on error.
		 */
		if (nwritten == 0) {
			*err = gzwfile_geterr((GZWFILE_T)wdh->fh);
			return FALSE;
		}
	} else
#endif
	{
		nwritten = fwrite(buf, 1, bufsize, (FILE *)wdh->fh);
		/*
		 * At least according to the Mac OS X man page,
		 * this can return a short count on an error.
		 */
		if (nwritten != bufsize) {
			if (ferror((FILE *)wdh->fh))
				*err = errno;
			else
				*err = WTAP_ERR_SHORT_WRITE;
			return FALSE;
		}
	}
	return TRUE;
}

/* internally close a file for writing (compressed or not) */
static int wtap_dump_file_close(wtap_dumper *wdh)
{
#ifdef HAVE_LIBZ
	if(wdh->compressed) {
		return gzwfile_close((GZWFILE_T)wdh->fh);
	} else
#endif
	{
		return fclose((FILE *)wdh->fh);
	}
}

gint64 wtap_dump_file_seek(wtap_dumper *wdh, gint64 offset, int whence, int *err)
{
#ifdef HAVE_LIBZ
	if(wdh->compressed) {
		*err = WTAP_ERR_CANT_SEEK_COMPRESSED;
		return -1;
	} else
#endif
	{
		if (-1 == fseek((FILE *)wdh->fh, (long)offset, whence)) {
			*err = errno;
			return -1;
		} else
		{
			return 0;
		}
	}
}
gint64 wtap_dump_file_tell(wtap_dumper *wdh, int *err)
{
	gint64 rval;
#ifdef HAVE_LIBZ
	if(wdh->compressed) {
		*err = WTAP_ERR_CANT_SEEK_COMPRESSED;
		return -1;
	} else
#endif
	{
		if (-1 == (rval = ftell((FILE *)wdh->fh))) {
			*err = errno;
			return -1;
		} else
		{
			return rval;
		}
	}
}
