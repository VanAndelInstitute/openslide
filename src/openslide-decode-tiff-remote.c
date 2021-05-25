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

#include "openslide-decode-tiff-remote.h"

#include <glib.h>
#include <gio/gio.h>
#include <tiffio.h>

#define HANDLE_CACHE_MAX 32

struct _openslide_tiffcache {
  char *filename;
  GQueue *cache;
  GMutex lock;
  int outstanding;
};

static tsize_t tiff_do_read(thandle_t th, tdata_t buf, tsize_t size)
{
  GError *err;
  return g_input_stream_read( (GDataInputStream *) th, buf, size, NULL, &err );
}

static tsize_t tiff_do_write(thandle_t th, tdata_t buf, tsize_t size)
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
  gboolean result = g_input_stream_close( (GDataInputStream *) th, NULL, &err );
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

static TIFF *tiff_open(const char *uri, GError **err) {
  // open
  GFile *file = g_file_new_for_uri(uri);
  if (file == NULL) {
    _openslide_io_error(err, "Couldn't open %s", uri);
    return NULL;
	}
  GFileInputStream *base_stream = g_file_read (file, NULL, err);
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
  g_filter_input_stream_set_close_base_stream(stream, TRUE);

  // read magic
  guchar byte_order = g_data_input_stream_read_byte(stream, NULL, err);
  if (g_data_input_stream_read_byte(stream, NULL, err) != byte_order) {
      g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                  "Not a TIFF file: %s", uri);
      g_input_stream_close(stream, NULL, err);
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
      g_input_stream_close(stream, NULL, err);
      g_object_unref(stream);
      return NULL;
    default:
      g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                  "Not a TIFF file: %s", uri);
      g_input_stream_close(stream, NULL, err);
      g_object_unref(stream);
      return NULL;
  }

  guint16 version = g_data_input_stream_read_uint16(stream, NULL, err);
  if (version != 42 && version != 43) {
    g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                "Not a TIFF file: %s", uri);
    g_input_stream_close(stream, NULL, err);
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
    g_input_stream_close(stream, NULL, err);
    g_object_unref(stream);
  }
      
	return tiff;
}

TIFF *_openslide_tiffcache_get_remote(struct _openslide_tiffcache *tc, GError **err) {
  //g_debug("get TIFF");
  g_mutex_lock(&tc->lock);
  tc->outstanding++;
  TIFF *tiff = g_queue_pop_head(tc->cache);
  g_mutex_unlock(&tc->lock);

  if (tiff == NULL) {
    //g_debug("create TIFF");
    // Does not check that we have the same file.  Then again, neither does
    // tiff_do_read.
    tiff = tiff_open(tc->filename, err);
  }
  if (tiff == NULL) {
    g_mutex_lock(&tc->lock);
    tc->outstanding--;
    g_mutex_unlock(&tc->lock);
  }
  return tiff;
}
