/* ber.h
 *
 * Basic Encoding Rules (BER) file reading
 *
 * $Id: ber.h 47992 2013-03-01 23:53:11Z rbalint $
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
 *
 */

#ifndef __BER_H__
#define __BER_H__
#include <glib.h>
#include "ws_symbol_export.h"

int ber_open(wtap *wth, int *err, gchar **err_info);

#endif
