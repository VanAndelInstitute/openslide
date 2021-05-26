/*
 *  OpenSlide, a library for reading whole slide image files
 *
 *  Copyright (c) 2007-2015 Carnegie Mellon University
 *  Copyright (c) 2011 Google, Inc.
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

#include "openslide-decode-tiff.h"
#include "openslide-decode-tiff-remote.h"

#include <glib.h>
#include <gio/gio.h>
#include <tiffio.h>

struct associated_image {
  struct _openslide_associated_image base;
  TIFF *tiff;
  tdir_t directory;
};

#define SET_DIR_OR_FAIL(tiff, i)					\
  do {									\
    if (!_openslide_tiff_set_dir(tiff, i, err)) {			\
      return false;							\
    }									\
  } while (0)

#define GET_FIELD_OR_FAIL(tiff, tag, type, result)			\
  do {									\
    type tmp;								\
    if (!TIFFGetField(tiff, tag, &tmp)) {				\
      g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,		\
                  "Cannot get required TIFF tag: %d", tag);		\
      return false;							\
    }									\
    result = tmp;							\
  } while (0)
  

static bool tiff_read_region(TIFF *tiff,
                             uint32_t *dest,
                             int64_t x, int64_t y,
                             int32_t w, int32_t h,
                             GError **err) {
  TIFFRGBAImage img;
  char emsg[1024] = "unknown error";
  bool success = false;

  // init
  if (!TIFFRGBAImageOK(tiff, emsg)) {
    g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                "Failure in TIFFRGBAImageOK: %s", emsg);
    return false;
  }
  if (!TIFFRGBAImageBegin(&img, tiff, 1, emsg)) {
    g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                "Failure in TIFFRGBAImageBegin: %s", emsg);
    return false;
  }
  img.req_orientation = ORIENTATION_TOPLEFT;
  img.col_offset = x;
  img.row_offset = y;

  // draw it
  if (TIFFRGBAImageGet(&img, dest, w, h)) {
    // convert ABGR -> ARGB
    for (uint32_t *p = dest; p < dest + w * h; p++) {
      uint32_t val = GUINT32_SWAP_LE_BE(*p);
      *p = (val << 24) | (val >> 8);
    }
    success = true;
  } else {
    g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                "TIFFRGBAImageGet failed");
    memset(dest, 0, w * h * 4);
  }

  // done
  TIFFRGBAImageEnd(&img);
  return success;
}

static bool _get_associated_image_data(TIFF *tiff,
                                       struct associated_image *img,
                                       uint32_t *dest,
                                       GError **err) {
  int64_t width, height;

  // g_debug("read TIFF associated image: %d", img->directory);

  SET_DIR_OR_FAIL(tiff, img->directory);

  // ensure dimensions have not changed
  GET_FIELD_OR_FAIL(tiff, TIFFTAG_IMAGEWIDTH, uint32_t, width);
  GET_FIELD_OR_FAIL(tiff, TIFFTAG_IMAGELENGTH, uint32_t, height);
  if (img->base.w != width || img->base.h != height) {
    g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                "Unexpected associated image size: "
                "expected %"PRId64"x%"PRId64", got %"PRId64"x%"PRId64,
                img->base.w, img->base.h, width, height);
    return false;
  }

  // load the image
  return tiff_read_region(tiff, dest, 0, 0, width, height, err);
}

static bool get_associated_image_data(struct _openslide_associated_image *_img,
                                      uint32_t *dest,
                                      GError **err) {
  struct associated_image *img = (struct associated_image *) _img;
  TIFF *tiff = img->tiff;
  bool success = false;
  if (tiff) {
    success = _get_associated_image_data(tiff, img, dest, err);
  }
  return success;
}

static void destroy_associated_image(struct _openslide_associated_image *_img) {
  struct associated_image *img = (struct associated_image *) _img;

  g_slice_free(struct associated_image, img);
}

static const struct _openslide_associated_image_ops tiff_associated_ops = {
  .get_argb_data = get_associated_image_data,
  .destroy = destroy_associated_image,
};

static bool _add_associated_image(openslide_t *osr,
                                  const char *name,
                                  tdir_t dir,
                                  TIFF *tiff,
                                  GError **err) {
  // set directory
  SET_DIR_OR_FAIL(tiff, dir);

  // get the dimensions
  int64_t w, h;
  GET_FIELD_OR_FAIL(tiff, TIFFTAG_IMAGEWIDTH, uint32_t, w);
  GET_FIELD_OR_FAIL(tiff, TIFFTAG_IMAGELENGTH, uint32_t, h);

  // check compression
  uint16_t compression;
  GET_FIELD_OR_FAIL(tiff, TIFFTAG_COMPRESSION, uint16_t, compression);
  if (!TIFFIsCODECConfigured(compression)) {
    g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                "Unsupported TIFF compression: %u", compression);
    return false;
  }

  // load into struct
  struct associated_image *img = g_slice_new0(struct associated_image);
  img->base.ops = &tiff_associated_ops;
  img->base.w = w;
  img->base.h = h;
  img->tiff = tiff;
  img->directory = dir;

  // save
  g_hash_table_insert(osr->associated_images, g_strdup(name), img);

  return true;
}

bool _openslide_tiff_add_associated_image_remote(
  openslide_t *osr,
  const char *name,
  TIFF *tiff,
  tdir_t dir,
  GError **err) {
  bool ret = false;
  if (tiff) {
    ret = _add_associated_image(osr, name, dir, tiff, err);
  }

  // safe even if successful
  g_prefix_error(err, "Can't read %s associated image: ", name);
  return ret;
}

static tsize_t tiff_do_read(thandle_t th, tdata_t buf, tsize_t size)
{
  GError *err;
  return g_input_stream_read( (GInputStream *) th, buf, size, NULL, &err );
}

static tsize_t tiff_do_write(thandle_t th G_GNUC_UNUSED, tdata_t buf G_GNUC_UNUSED, tsize_t size G_GNUC_UNUSED)
{
  // fail
  return 0;
}

static toff_t tiff_do_seek(thandle_t th, toff_t offset, int whence)
{
  GError *err;
  if( g_seekable_seek( (GSeekable *) th, offset, whence, NULL, &err ) )
    return (toff_t) g_seekable_tell( (GSeekable *) th );
  else
    return (toff_t) -1;
}

static int tiff_do_close(thandle_t th)
{
  GError *err;
  gboolean result = g_input_stream_close( (GInputStream *) th, NULL, &err );
  g_object_unref(th);
  return result ? 0 : -1;
}

static toff_t tiff_do_size(thandle_t th)
{
  GError *err;
  GFileInputStream *base_stream = (GFileInputStream*)
      g_filter_input_stream_get_base_stream((GFilterInputStream*)th);
  GFileInfo *info = g_file_input_stream_query_info(base_stream, G_FILE_ATTRIBUTE_STANDARD_SIZE, NULL, &err);
  return (toff_t)g_file_info_get_size(info);
}

#undef TIFFClientOpen
TIFF *_openslide_tiff_open(const char *uri, GError **err) {
  // open
  GFile *file = g_file_new_for_uri(uri);
  if (file == NULL) {
    _openslide_io_error(err, "Couldn't open %s", uri);
    return NULL;
	}
  GInputStream *base_stream = (GInputStream*)g_file_read (file, NULL, err);
  g_object_unref(file);
  if (base_stream == NULL) {
    return NULL;
	}
  GDataInputStream *stream = g_data_input_stream_new(base_stream);
  if (stream == NULL) {
    g_input_stream_close(base_stream, NULL, err);
    g_object_unref(base_stream);
    return NULL;
	}
  g_filter_input_stream_set_close_base_stream((GFilterInputStream*)stream, TRUE);

  // read magic
  guchar byte_order = g_data_input_stream_read_byte(stream, NULL, err);
  if (g_data_input_stream_read_byte(stream, NULL, err) != byte_order) {
      g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                  "Not a TIFF file: %s", uri);
      g_input_stream_close((GInputStream*)stream, NULL, err);
      g_object_unref(stream);
      return NULL;
  }
  switch(byte_order) {
    case 'M':
      g_data_input_stream_set_byte_order(stream, G_DATA_STREAM_BYTE_ORDER_BIG_ENDIAN);
      break;
    case 'I':
      g_data_input_stream_set_byte_order(stream, G_DATA_STREAM_BYTE_ORDER_LITTLE_ENDIAN);
      break;
    case 0:
      g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                  "Couldn't read TIFF magic number for %s", uri);
      g_input_stream_close((GInputStream*)stream, NULL, err);
      g_object_unref(stream);
      return NULL;
    default:
      g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                  "Not a TIFF file: %s", uri);
      g_input_stream_close((GInputStream*)stream, NULL, err);
      g_object_unref(stream);
      return NULL;
  }

  guint16 version = g_data_input_stream_read_uint16(stream, NULL, err);
  if (version != 42 && version != 43) {
    g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                "Not a TIFF file: %s", uri);
    g_input_stream_close((GInputStream*)stream, NULL, err);
    g_object_unref(stream);
    return NULL;
  }

  // TIFFOpen
  // mode: m disables mmap to avoid sigbus and other mmap fragility
  TIFF *tiff = TIFFClientOpen(uri, "rm", (thandle_t) stream,
                              tiff_do_read, tiff_do_write, tiff_do_seek,
                              tiff_do_close, tiff_do_size, NULL, NULL);
  if( tiff == NULL ) {
    g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                "Invalid TIFF: %s", uri);
    g_input_stream_close((GInputStream*)stream, NULL, err);
    g_object_unref(stream);
  }
      
	return tiff;
}
