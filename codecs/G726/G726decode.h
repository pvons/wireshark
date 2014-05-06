/* G726decode.h
 * Definitions for A-law G.722 codec
 *
 * $Id: G726decode.h 53866 2013-12-08 20:00:19Z darkjames $
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

#ifndef __CODECS_G726DECODE_H__
#define __CODECS_G726DECODE_H__

void
initG726_32(void);

int
decodeG726_32(void *input, int inputSizeBytes, void *output, int *outputSizeBytes);

#endif /* G726decode.h */
