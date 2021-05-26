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

#include <glib.h>
#include <tiffio.h>

TIFF *_openslide_tiff_open(const char *uri, GError **err);

bool _openslide_tiff_add_associated_image_remote(
  openslide_t *osr,
  const char *name,
  TIFF *tiff,
  tdir_t dir,
  GError **err);

#endif
