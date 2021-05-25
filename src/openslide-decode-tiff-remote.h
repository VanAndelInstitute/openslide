/*
 *  OpenSlide, a library for reading whole slide image files
 *
 *  Copyright (c) 2007-2013 Carnegie Mellon University
 *  All rights reserved.
 *
 *  OpenSlide is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation, version 2.1.
 *
 *  OpenSlide is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with OpenSlide. If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#ifndef OPENSLIDE_OPENSLIDE_DECODE_TIFF_REMOTE_H_
#define OPENSLIDE_OPENSLIDE_DECODE_TIFF_REMOTE_H_

#include "openslide-private.h"

#include <stdint.h>
#include <glib.h>
#include <tiffio.h>


struct _openslide_tiffcache;

/* TIFF handles are not thread-safe, so we have a handle cache for
   multithreaded access */
TIFF *_openslide_tiffcache_get_remote(struct _openslide_tiffcache *tc, GError **err);

#endif
