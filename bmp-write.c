/* bmplib - bmp-write.c
 *
 * Copyright (c) 2024, Rupert Weber.
 *
 * This file is part of bmplib.
 * bmplib is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU Lesser General Public License as 
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. 
 * If not, see <https://www.gnu.org/licenses/>
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <stdarg.h>
#include <math.h>

#include "config.h"
#include "bmplib.h"
#include "logging.h"
#include "bmp-common.h"
#include "huffman.h"
#include "bmp-write.h"






static void s_decide_outformat(BMPWRITE_R wp);
static int s_write_palette(BMPWRITE_R wp);
static int s_save_line_rgb(BMPWRITE_R wp, const unsigned char *line);
static inline unsigned long long s_set_outpixel_rgb(BMPWRITE_R wp,
                            const unsigned char *restrict buffer, size_t offs);
static int s_write_bmp_file_header(BMPWRITE_R wp);
static int s_write_bmp_info_header(BMPWRITE_R wp);
static inline int s_write_one_byte(int byte, BMPWRITE_R wp);
static int s_save_header(BMPWRITE_R wp);
static int s_try_saving_image_size(BMPWRITE_R wp);
static int s_calc_mask_values(BMPWRITE_R wp);
static int s_is_setting_compatible(BMPWRITE_R wp, const char *setting, ...);
static int s_check_already_saved(BMPWRITE_R wp);





/*****************************************************************************
 * 	bmpwrite_new
 *****************************************************************************/

API BMPHANDLE bmpwrite_new(FILE *file)
{
	BMPWRITE wp = NULL;

	if (!(wp = malloc(sizeof *wp))) {
		goto abort;
	}
	memset(wp, 0, sizeof *wp);
	wp->magic = HMAGIC_WRITE;

	wp->rle_requested  = BMP_RLE_NONE;
	wp->outorientation = BMP_ORIENT_BOTTOMUP;
	wp->source_format  = BMP_FORMAT_INT;

	if (!(wp->log = logcreate()))
		goto abort;

	if (!file) {
		logerr(wp->log, "Must supply file handle");
		goto abort;
	}

	wp->file = file;

	if (!(wp->fh = malloc(sizeof *wp->fh))) {
		logerr(wp->log, "allocating bmp file header");
		goto abort;
	}
	memset(wp->fh, 0, sizeof *wp->fh);

	if (!(wp->ih = malloc(sizeof *wp->ih))) {
		logerr(wp->log, "allocating bmp info header");
		goto abort;
	}
	memset(wp->ih, 0, sizeof *wp->ih);

	return (BMPHANDLE)(void*)wp;

abort:
	if (wp)
		bw_free(wp);
	return NULL;
}



/*****************************************************************************
 * 	bmpwrite_set_dimensions
 *****************************************************************************/

API BMPRESULT bmpwrite_set_dimensions(BMPHANDLE h,
                                      unsigned  width,
                                      unsigned  height,
                                      unsigned  source_channels,
                                      unsigned  source_bitsperchannel)
{
	BMPWRITE wp;

	if (!cm_check_is_write_handle(h))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (s_check_already_saved(wp))
		return BMP_RESULT_ERROR;

	if (!(s_is_setting_compatible(wp, "srcchannels", source_channels) &&
	      s_is_setting_compatible(wp, "srcbits", source_bitsperchannel)))
		return BMP_RESULT_ERROR;

	if (!cm_is_one_of(source_bitsperchannel, 3, 8, 16, 32)) {
		logerr(wp->log, "Invalid number of bits per channel: %d",
		                                 (int) source_bitsperchannel);
		return BMP_RESULT_ERROR;
	}

	if (!cm_is_one_of(source_channels, 4, 3, 4, 1, 2)) {
		logerr(wp->log, "Invalid number of channels: %d", (int) source_channels);
		return BMP_RESULT_ERROR;
	}

	wp->source_bytes_per_pixel = source_bitsperchannel / 8 * source_channels;

	if (width > INT32_MAX || height > INT32_MAX ||
	    width < 1 || height < 1 ||
	    (uint64_t) width * height > SIZE_MAX / wp->source_bytes_per_pixel) {
		logerr(wp->log, "Invalid dimensions %ux%ux%u @ %ubits",
		                          width, height, source_channels,
		                          source_bitsperchannel);
		return BMP_RESULT_ERROR;
	}

	wp->width = (int) width;
	wp->height = (int) height;
	wp->source_channels = (int) source_channels;
	wp->source_bitsperchannel = (int) source_bitsperchannel;
	wp->dimensions_set = TRUE;

	return BMP_RESULT_OK;
}



/*****************************************************************************
 * 	bw_set_number_format
 *****************************************************************************/

BMPRESULT bw_set_number_format(BMPWRITE_R wp, enum BmpFormat format)
{
	if (format == wp->source_format)
		return BMP_RESULT_OK;

	if (s_check_already_saved(wp))
		return BMP_RESULT_ERROR;

	if (!s_is_setting_compatible(wp, "format", format))
		return BMP_RESULT_ERROR;

	wp->source_format = format;
	return BMP_RESULT_OK;
}



/*****************************************************************************
 * 	bmpwrite_set_output_bits
 *****************************************************************************/

API BMPRESULT bmpwrite_set_output_bits(BMPHANDLE h, int red, int green, int blue, int alpha)
{
	BMPWRITE wp;

	if (!cm_check_is_write_handle(h))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (s_check_already_saved(wp))
		return BMP_RESULT_ERROR;

	if (!s_is_setting_compatible(wp, "outbits"))
		return BMP_RESULT_ERROR;

	if (!(cm_all_positive_int(4, red, green, blue, alpha) &&
	      cm_all_lessoreq_int(32, 4, red, green, blue, alpha) &&
	      red + green + blue > 0 &&
	      red + green + blue + alpha <= 32 )) {
		logerr(wp->log, "Invalid output bit depths specified: %d-%d-%d - %d",
		                              red, green, blue, alpha);
		wp->outbits_set = FALSE;
		return BMP_RESULT_ERROR;
	}

	wp->cmask.bits.red   = red;
	wp->cmask.bits.green = green;
	wp->cmask.bits.blue  = blue;
	wp->cmask.bits.alpha = alpha;

	wp->outbits_set = TRUE;

	return BMP_RESULT_OK;
}



/*****************************************************************************
 * 	bmpwrite_set_palette
 *****************************************************************************/

API BMPRESULT bmpwrite_set_palette(BMPHANDLE h, int numcolors,
                                   const unsigned char *palette)
{
	BMPWRITE wp;
	int      i, c;
	size_t   memsize;

	if (!cm_check_is_write_handle(h))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (s_check_already_saved(wp))
		return BMP_RESULT_ERROR;

	if (wp->palette) {
		logerr(wp->log, "Palette already set. Cannot set twice");
		return BMP_RESULT_ERROR;
	}

	if (!s_is_setting_compatible(wp, "indexed"))
		return BMP_RESULT_ERROR;

	if (numcolors < 1 || numcolors > 256) {
		logerr(wp->log, "Invalid number of colors for palette (%d)",
		                                          numcolors);
		return BMP_RESULT_ERROR;
	}

	memsize = sizeof *wp->palette + numcolors * sizeof wp->palette->color[0];
	if (!(wp->palette = malloc(memsize))) {
		logsyserr(wp->log, "Allocating palette");
		return BMP_RESULT_ERROR;
	}
	memset(wp->palette, 0, memsize);

	wp->palette->numcolors = numcolors;
	for (i = 0; i < numcolors; i++) {
		for (c = 0; c < 3; c++) {
			wp->palette->color[i].value[c] = palette[4*i + c];
		}
	}
	wp->palette_size = 4 * numcolors;
	return BMP_RESULT_OK;
}



/*****************************************************************************
 * 	bmpwrite_set_orientation
 *****************************************************************************/

API BMPRESULT bmpwrite_set_orientation(BMPHANDLE h, enum BmpOrient orientation)
{
	BMPWRITE wp;

	if (!cm_check_is_write_handle(h))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (s_check_already_saved(wp))
		return BMP_RESULT_ERROR;

	switch (orientation) {
	case BMP_ORIENT_TOPDOWN:
		if (wp->rle_requested != BMP_RLE_NONE) {
			logerr(wp->log, "Topdown is invalid with RLE BMPs");
			return BMP_RESULT_ERROR;
		}
		break;

	case BMP_ORIENT_BOTTOMUP:
		/* always ok */
		break;

	default:
		logerr(wp->log, "Invalid orientation (%d)", (int) orientation);
		return BMP_RESULT_ERROR;
	}

	wp->outorientation = orientation;
	return BMP_RESULT_OK;
}



/*****************************************************************************
 * 	bmpwrite_set_rle
 *****************************************************************************/

API BMPRESULT bmpwrite_set_rle(BMPHANDLE h, enum BmpRLEtype type)
{
	BMPWRITE wp;

	if (!cm_check_is_write_handle(h))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (s_check_already_saved(wp))
		return BMP_RESULT_ERROR;

	if (!s_is_setting_compatible(wp, "rle", type))
		return BMP_RESULT_ERROR;

	if (!cm_is_one_of((int)type, 3, (int) BMP_RLE_NONE, (int) BMP_RLE_AUTO, (int) BMP_RLE_RLE8)) {
		logerr(wp->log, "Invalid RLE type specified (%d)", (int) type);
		return BMP_RESULT_ERROR;
	}

	wp->rle_requested = type;
	return BMP_RESULT_OK;
}



/*****************************************************************************
 * 	bmpwrite_set_resolution
 *****************************************************************************/

API BMPRESULT bmpwrite_set_resolution(BMPHANDLE h, int xdpi, int ydpi)
{
	BMPWRITE wp;

	if (!cm_check_is_write_handle(h))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (s_check_already_saved(wp))
		return BMP_RESULT_ERROR;

	wp->ih->xpelspermeter = 39.37 * xdpi + 0.5;
	wp->ih->ypelspermeter = 39.37 * ydpi + 0.5;

	return BMP_RESULT_OK;
}



/*****************************************************************************
 * 	bmpwrite_allow_2bit
 *****************************************************************************/

API BMPRESULT bmpwrite_allow_2bit(BMPHANDLE h)
{
	BMPWRITE wp;

	if (!cm_check_is_write_handle(h))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (s_check_already_saved(wp))
		return BMP_RESULT_ERROR;

	wp->allow_2bit = TRUE;

	return BMP_RESULT_OK;
}


/*****************************************************************************
 * 	bmpwrite_allow_huffman
 *****************************************************************************/

API BMPRESULT bmpwrite_allow_huffman(BMPHANDLE h)
{
	BMPWRITE wp;

	if (!cm_check_is_write_handle(h))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (s_check_already_saved(wp))
		return BMP_RESULT_ERROR;

	wp->allow_huffman = TRUE;

	return BMP_RESULT_OK;
}


/*****************************************************************************
 * 	bmpwrite_set_64bit
 *****************************************************************************/

API BMPRESULT bmpwrite_set_64bit(BMPHANDLE h)
{
	BMPWRITE wp;

	if (!cm_check_is_write_handle(h))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (s_check_already_saved(wp))
		return BMP_RESULT_ERROR;

	if (!s_is_setting_compatible(wp, "64bit"))
		return BMP_RESULT_ERROR;

	wp->out64bit = TRUE;
	return BMP_RESULT_OK;
}



/*****************************************************************************
 * 	s_check_already_saved
 *****************************************************************************/

static int s_check_already_saved(BMPWRITE_R wp)
{
	if (wp->saveimage_done) {
		logerr(wp->log, "Image already saved.");
		return TRUE;
	}
	return FALSE;
}



/*****************************************************************************
 * 	s_is_setting_compatible
 *
 * setting: "outbits", "srcbits", "srcchannels",
 *          "format", "indexed", "64bit", "rle"
 *****************************************************************************/

static int s_is_setting_compatible(BMPWRITE_R wp, const char *setting, ...)
{
	int            channels, bits;
	enum BmpFormat format;
	enum BmpRLEtype rle;
	int            ret = TRUE;
	va_list        args;

	va_start(args, setting);

	if (!strcmp(setting, "outbits")) {
		if (wp->palette || wp->out64bit || wp->rle_requested) {
			logerr(wp->log, "output bits cannot be set with indexed, RLE, "
			       "or 64bit BMPs");
			ret = FALSE;
		}
	} else if (!strcmp(setting, "srcbits")) {
		bits = va_arg(args, int);
		if (wp->palette && bits != 8) {
			logerr(wp->log, "indexed images must be 8 bits (not %d)", bits);
			ret = FALSE;
		} else if (wp->source_format == BMP_FORMAT_FLOAT && bits != 32) {
			logerr(wp->log, "float images must be 32 bits per channel (not %d)", bits);
			ret = FALSE;
		} else if (wp->source_format == BMP_FORMAT_S2_13 && bits != 16) {
			logerr(wp->log, "s2.13 images must be 16 bits per channel (not %d)", bits);
			ret = FALSE;
		}
	} else if (!strcmp(setting, "srcchannels")) {
		channels = va_arg(args, int);
		if (wp->palette && (channels != 1)) {
			logerr(wp->log, "Indexed images must have 1 channel (not %d)", channels);
			ret = FALSE;
		}
		if (wp->out64bit && (channels != 3 && channels != 4)) {
			logerr(wp->log, "64bit images must have 3 or 4 channels (not %d)", channels);
			ret = FALSE;
		}
	} else if (!strcmp(setting, "indexed")) {
		if (wp->out64bit) {
			logerr(wp->log, "64bit BMPs cannot be indexed");
			ret = FALSE;
		}
		if (wp->outbits_set) {
			logerr(wp->log, "BMPs with specified channel bits cannot be indexed");
			ret = FALSE;
		}
		if (wp->source_format != BMP_FORMAT_INT) {
			logerr(wp->log, "Indexed image must have INT format (not %s)",
			             cm_format_name(wp->source_format));
			ret = FALSE;
		}
		if (wp->dimensions_set) {
			if (!(wp->source_channels == 1 && wp->source_bitsperchannel == 8)) {
				logerr (wp->log, "Indexed images must be 1 channel, 8 bits");
				ret = FALSE;
			}
		}
	} else if (!strcmp(setting, "format")) {
		format = va_arg(args, enum BmpFormat);
		switch (format) {
		case BMP_FORMAT_FLOAT:
			if (wp->dimensions_set && wp->source_bitsperchannel != 32) {
				logerr(wp->log, "float cannot be %d bits per pixel",
				                           wp->source_bitsperchannel);
				ret = FALSE;
			}
			if (wp->palette) {
				logerr(wp->log, "float cannot be used for indexed images");
				ret = FALSE;
			}
			break;
		case BMP_FORMAT_S2_13:
			if (wp->dimensions_set && wp->source_bitsperchannel != 16) {
				logerr(wp->log, "s2.13 cannot be %d bits per pixel",
				                           wp->source_bitsperchannel);
				ret = FALSE;
			}
			if (wp->palette) {
				logerr(wp->log, "s2.13 cannot be used for indexed images");
				ret = FALSE;
			}
			break;
		default:
			/* INT is ok with everything */
			break;
		}
	} else if (!strcmp(setting, "rle")) {
		rle = va_arg(args, enum BmpRLEtype);
		if (rle == BMP_RLE_AUTO || rle == BMP_RLE_RLE8) {
			if (wp->outorientation != BMP_ORIENT_BOTTOMUP) {
				logerr(wp->log, "RLE is invalid with top-down BMPs");
				ret = FALSE;;
			}
		}
	} else if (!strcmp(setting, "64bit")) {
		if (wp->palette) {
			logerr(wp->log, "Indexed images cannot be 64bit");
			ret = FALSE;
		}
	}

	va_end(args);
	return ret;
}



/*****************************************************************************
 * 	s_decide_outformat
 *****************************************************************************/

static void s_decide_outformat(BMPWRITE_R wp)
{
	int      bitsum;
	uint64_t bitmapsize, filesize, bytes_per_line;

	if ((wp->source_channels == 4 || wp->source_channels == 2) &&
	    ((wp->outbits_set && wp->cmask.bits.alpha) || !wp->outbits_set) ) {
		wp->has_alpha = TRUE;
	} else {
		wp->cmask.bits.alpha = 0;
		wp->has_alpha = FALSE;
	}

	if (!wp->outbits_set) {
		if (wp->out64bit) {
			wp->cmask.bits.red   = 16;
			wp->cmask.bits.green = 16;
			wp->cmask.bits.blue  = 16;
			wp->cmask.bits.alpha = 16; /* 64bit always has alpha channel */
		} else {
			wp->cmask.bits.red = wp->cmask.bits.green = wp->cmask.bits.blue = 8;
			if (wp->has_alpha)
				wp->cmask.bits.alpha = 8;
		}
	}

	bitsum = s_calc_mask_values(wp);

	if (wp->palette) {
		wp->ih->version = BMPINFO_V3;
		wp->ih->size    = BMPIHSIZE_V3;
		if (wp->rle_requested != BMP_RLE_NONE) {
			if (wp->palette->numcolors > 16 ||
			    wp->rle_requested == BMP_RLE_RLE8) {
				wp->rle = 8;
				wp->ih->compression = BI_RLE8;
				wp->ih->bitcount = 8;
			} else if (wp->palette->numcolors > 2 ||
			           !wp->allow_huffman) {
				wp->rle = 4;
				wp->ih->compression = BI_RLE4;
				wp->ih->bitcount = 4;
			} else {
				wp->rle = 1;
				wp->ih->compression = BI_OS2_HUFFMAN;
				wp->ih->bitcount = 1;
				wp->ih->version = BMPINFO_OS22;
				wp->ih->size    = BMPIHSIZE_OS22;
			}
		} else {
			wp->ih->compression = BI_RGB;
			wp->ih->bitcount = 1;
			while ((1<<wp->ih->bitcount) < wp->palette->numcolors)
				wp->ih->bitcount *= 2;
			if (wp->ih->bitcount == 2 && !wp->allow_2bit)
				wp->ih->bitcount = 4;
		}
	}
	/* we need BI_BITFIELDS if any of the following is true and we are not
	 * writing a 64bit BMP:
	 *    - not all RGB-components have the same bitlength
	 *    - we are writing an alpha-channel
	 *    - bits per component are not either 5 or 8 (which have
	 *      known RI_RGB representation)
	 */
	else if (bitsum < 64 && (!cm_all_equal_int(3, (int) wp->cmask.bits.red,
	                                              (int) wp->cmask.bits.green,
	                                              (int) wp->cmask.bits.blue)
	                         || wp->has_alpha
                                 || (wp->cmask.bits.red > 0  &&
                                     wp->cmask.bits.red != 5 &&
                                     wp->cmask.bits.red != 8    )    )) {

		wp->ih->version     = BMPINFO_V4;
		wp->ih->size        = BMPIHSIZE_V4;
		wp->ih->compression = BI_BITFIELDS;  /* do we need BI_ALPHABITFIELDS when alpha is present */
		                                     /* or will that just confuse other readers?   !!!!!   */
		if (bitsum <= 16)
			wp->ih->bitcount = 16;
		else
			wp->ih->bitcount = 32;
	}
	/* otherwise, use BI_RGB with either 5 or 8 bits per component
	 * resulting in bitcount of 16 or 24, or a 64bit BMP with 16 bits/comp.
	 */
	else {
		wp->ih->version     = BMPINFO_V3;
		wp->ih->size        = BMPIHSIZE_V3;
		wp->ih->compression = BI_RGB;
		wp->ih->bitcount    = (bitsum + 7) / 8 * 8;
	}

	if (wp->palette) {
		wp->ih->clrused = wp->palette->numcolors;
	} else {
		wp->outbytes_per_pixel = wp->ih->bitcount / 8;

		if (wp->ih->version >= BMPINFO_V4 && !wp->out64bit) {
			wp->ih->redmask   = wp->cmask.mask.red   << wp->cmask.shift.red;
			wp->ih->greenmask = wp->cmask.mask.green << wp->cmask.shift.green;
			wp->ih->bluemask  = wp->cmask.mask.blue  << wp->cmask.shift.blue;
			wp->ih->alphamask = wp->cmask.mask.alpha << wp->cmask.shift.alpha;
		}
	}

	bytes_per_line = ((uint64_t) wp->width * wp->ih->bitcount + 7) / 8;
	wp->padding = cm_align4padding(bytes_per_line);
	bitmapsize = (bytes_per_line + wp->padding) * wp->height;
	filesize = bitmapsize + BMPFHSIZE + wp->ih->size + wp->palette_size;

	wp->fh->type = 0x4d42; /* "BM" */
	wp->fh->size = (wp->rle || filesize > UINT32_MAX) ? 0 : filesize;
	wp->fh->offbits = BMPFHSIZE + wp->ih->size + wp->palette_size;

	wp->ih->width = wp->width;
	if (wp->outorientation == BMP_ORIENT_BOTTOMUP)
		wp->ih->height = wp->height;
	else
		wp->ih->height = -wp->height;
	wp->ih->planes = 1;
	wp->ih->sizeimage = (wp->rle || bitmapsize > UINT32_MAX) ? 0 : bitmapsize;
}



/*****************************************************************************
 * 	bmpwrite_save_image
 *****************************************************************************/
static int s_save_line_rgb(BMPWRITE_R wp, const unsigned char *line);
static int s_save_line_rle8(BMPWRITE_R wp, const unsigned char *line);
static int s_save_line_rle4(BMPWRITE_R wp, const unsigned char *line);
static int s_save_line_huff(BMPWRITE_R wp, const unsigned char *line);

API BMPRESULT bmpwrite_save_image(BMPHANDLE h, const unsigned char *image)
{
	BMPWRITE wp;
	size_t   offs, linesize;
	int      y, real_y, res;

	if (!cm_check_is_write_handle(h))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (s_check_already_saved(wp))
		return BMP_RESULT_ERROR;

	if (wp->line_by_line) {
		logerr(wp->log, "Cannot switch from line-by-line to saving full image");
		return BMP_RESULT_ERROR;
	}

	if  (!s_save_header(wp))
		return BMP_RESULT_ERROR;

	wp->saveimage_done = TRUE;
	wp->bytes_written_before_bitdata = wp->bytes_written;

	if (wp->ih->compression == BI_OS2_HUFFMAN)
		huff_encode(wp, -1, 0); /* leading eol */

	linesize = (size_t) wp->width * (size_t) wp->source_bytes_per_pixel;
	for (y = 0; y < wp->height; y++) {
		real_y = (wp->outorientation == BMP_ORIENT_TOPDOWN) ? y : wp->height - y - 1;
		offs = (size_t) real_y * linesize;
		switch (wp->rle) {
		case 8:
			res = s_save_line_rle8(wp, image + offs);
			break;
		case 4:
			res = s_save_line_rle4(wp, image + offs);
			break;
		case 1:
			res = s_save_line_huff(wp, image + offs);
			break;
		default:
			res = s_save_line_rgb(wp, image + offs);
			break;
		}
		if (!res) {
			logerr(wp->log, "failed saving line %d", y);
			return BMP_RESULT_ERROR;
		}
	}
	if (wp->rle) {
		if (wp->rle > 1) {
			if (EOF == s_write_one_byte(0, wp) ||
			    EOF == s_write_one_byte(1, wp)) {
				logsyserr(wp->log, "Writing RLE end-of-file marker");
				return BMP_RESULT_ERROR;
			}
		}
		else {
			huff_encode(wp, -1, 0);
			huff_encode(wp, -1, 0);
			huff_encode(wp, -1, 0);
			huff_encode(wp, -1, 0);
			huff_encode(wp, -1, 0);
			huff_flush(wp);
		}

		s_try_saving_image_size(wp);
	}

	return BMP_RESULT_OK;
}



/*****************************************************************************
 * 	bmpwrite_save_line
 *****************************************************************************/

API BMPRESULT bmpwrite_save_line(BMPHANDLE h, const unsigned char *line)
{
	BMPWRITE wp;
	int      res;

	if (!cm_check_is_write_handle(h))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (s_check_already_saved(wp))
		return BMP_RESULT_ERROR;

	if (!wp->line_by_line) {  /* first line */
		if  (!s_save_header(wp))
			goto abort;
		wp->bytes_written_before_bitdata = wp->bytes_written;
		wp->line_by_line = TRUE;
		if (wp->ih->compression == BI_OS2_HUFFMAN)
			huff_encode(wp, -1, 0); /* leading eol */
	}

	switch (wp->rle) {
	case 8:
		res = s_save_line_rle8(wp, line);
		break;
	case 4:
		res = s_save_line_rle4(wp, line);
		break;
	case 1:
		res = s_save_line_huff(wp, line);
		break;
	default:
		res = s_save_line_rgb(wp, line);
		break;
	}

	if (!res)
		goto abort;

	if (++wp->lbl_y >= wp->height) {
		if (wp->rle) {
			if (wp->rle > 1) {
				if (EOF == s_write_one_byte(0, wp) ||
				    EOF == s_write_one_byte(1, wp)) {
					logsyserr(wp->log, "Writing RLE end-of-file marker");
					goto abort;
				}
			} else {
				huff_encode(wp, -1, 0);
				huff_encode(wp, -1, 0);
				huff_encode(wp, -1, 0);
				huff_encode(wp, -1, 0);
				huff_encode(wp, -1, 0);
				huff_flush(wp);
			}
			s_try_saving_image_size(wp);
		}
		wp->saveimage_done = TRUE;
	}

	return BMP_RESULT_OK;
abort:
	wp->saveimage_done = TRUE;
	return BMP_RESULT_ERROR;
}



/*****************************************************************************
 * 	s_save_header
 *****************************************************************************/

static int s_save_header(BMPWRITE_R wp)
{
	if (wp->saveimage_done || wp->line_by_line) {
		logerr(wp->log, "Image already saved.");
		return FALSE;
	}

	if (!wp->dimensions_set) {
		logerr(wp->log, "Must set dimensions before saving");
		return FALSE;
	}

	s_decide_outformat(wp);

	if (!s_write_bmp_file_header(wp)) {
		logsyserr(wp->log, "Writing BMP file header");
		return FALSE;
	}

	if (!s_write_bmp_info_header(wp)) {
		logsyserr(wp->log, "Writing BMP info header");
		return FALSE;
	}

	if (wp->palette) {
		if (!s_write_palette(wp)) {
			logsyserr(wp->log, "Couldn't write palette");
			return FALSE;
		}
	}

	return TRUE;
}



/*****************************************************************************
 * s_try_saving_image_size
 *
 * For RLE images, we must set the file/bitmap size in
 * file- and infoheader. But we don't know the size until
 * after the image is saved. Thus, we have to fseek()
 * back into the header, which will fail on unseekable
 * files like pipes etc.
 * We ignore any errors quietly, as there's nothing we
 * can do and most (all?) readers ignore those sizes in
 * the header, anyway. Same goes for file/bitmap sizes
 * which are too big for the respective fields.
 *****************************************************************************/

static int s_try_saving_image_size(BMPWRITE_R wp)
{
	uint64_t image_size, file_size;

	image_size = wp->bytes_written - wp->bytes_written_before_bitdata;
	file_size  = wp->bytes_written;

	if (fseek(wp->file, 2, SEEK_SET))        /* file header -> bfSize */
		return FALSE;
	if (file_size <= UINT32_MAX && !write_u32_le(wp->file, file_size))
		return FALSE;
	if (fseek(wp->file, 14 + 20, SEEK_SET))  /* info header -> biSizeImage */
		return FALSE;
	if (image_size <= UINT32_MAX && !write_u32_le(wp->file, image_size))
		return FALSE;
	return TRUE;
}



/*****************************************************************************
 * 	s_save_line_rgb
 *****************************************************************************/

static int s_save_line_rgb(BMPWRITE_R wp, const unsigned char *line)
{
	size_t        offs;
	unsigned long bytes = 0;
	int           i, x, bits_used = 0;

	for (x = 0; x < wp->width; x++) {
		offs = (size_t) x * (size_t) wp->source_channels;
		if (wp->palette) {
			bytes <<= wp->ih->bitcount;
			bytes |= line[offs];
			bits_used += wp->ih->bitcount;
			if (bits_used == 8) {
				if (EOF == s_write_one_byte((int)bytes, wp)) {
					logsyserr(wp->log, "Writing image to BMP file");
					return FALSE;
				}
				bytes = 0;
				bits_used = 0;
			}
		} else {
			bytes = s_set_outpixel_rgb(wp, line, offs);
			if (bytes == (unsigned long)-1)
				return BMP_RESULT_ERROR;

			for (i = 0; i < wp->outbytes_per_pixel; i++) {
				if (EOF == s_write_one_byte((bytes >> (8*i)) & 0xff, wp)) {
					logsyserr(wp->log, "Writing image to BMP file");
					return FALSE;
				}
			}
		}
	}

	if (wp->palette && bits_used != 0) {
		bytes <<= 8 - bits_used;
		if (EOF == s_write_one_byte((int)bytes, wp)) {
			logsyserr(wp->log, "Writing image to BMP file");
			return FALSE;
		}
		bits_used = 0;
	}

	for (i = 0; i < wp->padding; i++) {
		if (EOF == s_write_one_byte(0, wp)) {
			logsyserr(wp->log, "Writing padding bytes to BMP file");
			return FALSE;
		}
	}
	return TRUE;
}



/*****************************************************************************
 * 	s_length_of_runs
 *
 * 	for RLE-encoding, returns the number of
 *      pixels in contiguous upcoming groups with
 *      run-lengths >= minlen. Used to determine if it is
 *      worthwile to switch from literal run to
 *      repeat-run.
 *****************************************************************************/

static inline int s_length_of_runs(BMPWRITE_R wp, int x, int group, int minlen)
{
	int i, len = 0;

	for (i = group; i < wp->group_count; i++) {
		if (wp->group[i] < minlen)
			break;
		len += wp->group[i];
	}
	return len;
}



/*****************************************************************************
 * 	s_save_line_rle8
 *****************************************************************************/

static int s_save_line_rle8(BMPWRITE_R wp, const unsigned char *line)
{
	int i, j, k, x, l, dx;

	if (!wp->group) {
		if (!(wp->group = malloc(wp->width * sizeof *wp->group))) {
			logsyserr(wp->log, "allocating RLE buffer");
			goto abort;
		}
	}

	/* group identical contiguous pixels and keep a list
	 * of number of pixels/group in wp->group
	 * e.g. a pixel line abccaaadaaba would make a group list:
	 *                   112 3  12 11 = 1,1,2,3,1,2,1,1
	 */
	memset(wp->group, 0, wp->width * sizeof *wp->group);
	for (x = 0, wp->group_count = 0; x < wp->width; x++) {
		wp->group[wp->group_count]++;
		if (x == wp->width - 1 || line[x] != line[x+1])
			wp->group_count++;
	}

	x = 0;
	for (i = 0; i < wp->group_count; i++) {
		l = 0;  /* l counts the number of groups in this literal run */
		dx = 0; /* dx counts the number of pixels in this literal run */
		while (i+l < wp->group_count && wp->group[i+l] == 1 && dx < 254) {
			/* start/continue a literal run */
			dx++;
			l++;

			/* if only a small number of repeated pixels comes up, include
			 * those in the literal run instead of switching to repeat-run.
			 * Not perfect, but already much better than interrupting a literal
			 * run for e.g. two repeated pixels and then restarting the literal
			 * run at a cost of 2-4 bytes (depending on padding)
			 */
			const int small_number = 5;
			if (i+l < wp->group_count && s_length_of_runs(wp, x+dx, i+l, 2) <= small_number) {
				while (i+l < wp->group_count && wp->group[i+l] > 1 && dx + wp->group[i+l] < 255) {
					dx += wp->group[i+l];
					l++;
				}
			}
		}
		if (dx >= 3) {
			/* write literal run to file if it's at least 3 pixels long,
			 * otherwise fall through to repeat-run
			 */
			if (EOF == s_write_one_byte(0, wp) ||
			    EOF == s_write_one_byte(dx, wp)) {
				goto abort;
			}
			for (j = 0; j < l; j++) {
				for (k = 0; k < wp->group[i+j]; k++) {
					if (EOF == s_write_one_byte(line[x++], wp)) {
						goto abort;
					}
				}
			}
			if (dx & 0x01) {  /* pad odd-length literal run */
				if (EOF == s_write_one_byte(0, wp)) {
					goto abort;
				}
			}
			i += l-1;
			continue;
		}
		/* write repeat-run to file */
		if (EOF == s_write_one_byte(wp->group[i], wp)) {
			goto abort;
		}
		if (EOF == s_write_one_byte(line[x], wp)) {
			goto abort;
		}
		x += wp->group[i];
	}

	if (EOF == s_write_one_byte(0, wp) || EOF == s_write_one_byte(0, wp)) {  /* EOL */
		goto abort;
	}

	return TRUE;
abort:
	if (wp->group) {
		free(wp->group);
		wp->group = NULL;
		wp->group_count = 0;
	}
	logsyserr(wp->log, "Writing RLE8 data to BMP file");
	return FALSE;
}



/*****************************************************************************
 * 	s_save_line_rle4
 *****************************************************************************/

static int s_save_line_rle4(BMPWRITE_R wp, const unsigned char *line)
{
	int i, j, k, x, dx, l, even, outbyte;

	if (!wp->group) {
		if (!(wp->group = malloc(wp->width * sizeof *wp->group))) {
			logsyserr(wp->log, "allocating RLE buffer");
			goto abort;
		}
	}

	/* group contiguous alternating pixels and keep a list
	 * of number of pixels/group in wp->group. Unlike RLE8,
	 * there is not one unambiguos ways to group the pixels.
	 * (Of course, the two repeated 4-bit values could also
	 * be identical)
	 * e.g.,a pixel line abcbcabaddacacab coule be grouped as:
	 *                   14   3  2 5    1 = 1,4,3,2,5
	 *               or  2 3  3  2 5    1 = 2,3,3,2,5
	 * we'll use the second (greedy) one, which results in
	 * groups of at least 2 pixels (except for the last group
	 * in a row, which may be a 1-pixel group)
	 */
	memset(wp->group, 0, wp->width * sizeof *wp->group);
	for (x = 0, wp->group_count = 0; x < wp->width; x++) {
		wp->group[wp->group_count]++;
		if (x == wp->width - 1) {
			wp->group_count++;
			break;
		}
		if (wp->group[wp->group_count] > 1 && line[x-1] != line[x+1]) {
			wp->group_count++;
		}
	}

	x = 0;
	for (i = 0; i < wp->group_count; i++) {
		l = 0;  /* l counts the number of groups in this literal run */
		dx = 0; /* dx counts the number of pixels in this literal run */
		while (i+l < wp->group_count && wp->group[i+l] <= 2 && (dx + wp->group[i+l]) < 255) {
			/* start/continue a literal run */
			dx += wp->group[i+l];
			l++;

			const int small_number = 7;
			if (i+l < wp->group_count && s_length_of_runs(wp, x+dx, i+l, 3) <= small_number) {
				while (i+l < wp->group_count && wp->group[i+l] > 2 && dx + wp->group[i+l] < 255) {
					dx += wp->group[i+l];
					l++;
				}
			}
		}
		if (dx >= 3) {
			/* write literal run to file if it's at least 3 bytes long,
			 * otherwise fall through to repeat-run
			 */
			if (EOF == s_write_one_byte(0, wp) ||
			    EOF == s_write_one_byte(dx, wp)) {
				goto abort;
			}
			even = TRUE;
			for (j = 0; j < l; j++) {
				for (k = 0; k < wp->group[i+j]; k++) {
					if (even)
						outbyte = (line[x++] << 4) & 0xf0;
					else {
						outbyte |= line[x++] & 0x0f;
						if (EOF == s_write_one_byte(outbyte, wp)) {
							goto abort;
						}
					}
					even = !even;
				}
			}
			if (!even) {
				if (EOF == s_write_one_byte(outbyte, wp)) {
					goto abort;
				}
			}
			if ((dx+1)%4 > 1) {  /* pad literal run to 2 byte boundary */
				if (EOF == s_write_one_byte(0, wp)) {
					goto abort;
				}
			}
			i += l-1;
			continue;
		}

		/* write repeat-run to file */
		if (EOF == s_write_one_byte(wp->group[i], wp)) {
			goto abort;
		}
		outbyte = (line[x] << 4) & 0xf0;
		if (wp->group[i] > 1)
			outbyte |= line[x+1] & 0x0f;
		if (EOF == s_write_one_byte(outbyte, wp)) {
			goto abort;
		}
		x += wp->group[i];
	}
	if (EOF == s_write_one_byte(0, wp) || EOF == s_write_one_byte(0, wp)) {
		goto abort;
	}

	return TRUE;
abort:
	if (wp->group) {
		free(wp->group);
		wp->group = NULL;
		wp->group_count = 0;
	}
	logsyserr(wp->log, "Writing RLE8 data to BMP file");
	return FALSE;
}



/*****************************************************************************
 * 	s_save_line_huff
 *****************************************************************************/

static int s_save_line_huff(BMPWRITE_R wp, const unsigned char *line)
{
	int x, len, total = 0;
	int black = FALSE;

	x = 0;
	while (x < wp->width) {
		len = 0;
		while ((len < wp->width - x) && ((!!line[x + len]) == black))
			len++;
		if (!huff_encode(wp, len, black))
			goto abort;
		total += len;
		black = !black;
		x += len;
	}
	if (!huff_encode(wp, -1, 0)) /* eol */
		goto abort;

	return TRUE;
abort:
	logsyserr(wp->log, "Writing 1-D Huffman data to BMP file");
	return FALSE;
}



/*****************************************************************************
 * 	s_calc_mask_values
 *****************************************************************************/

static int s_calc_mask_values(BMPWRITE_R wp)
{
	int i;
	int shift = 0;

	for (i = 2; i >= 0; i--) {
		wp->cmask.shift.value[i] = shift;
		wp->cmask.mask.value[i]  = (1ULL << wp->cmask.bits.value[i]) - 1;
		wp->cmask.maxval.val[i]  = (double) wp->cmask.mask.value[i];
		shift += wp->cmask.bits.value[i];
	}
	if (wp->cmask.bits.alpha) {
		wp->cmask.shift.alpha  = shift;
		wp->cmask.mask.alpha   = (1ULL << wp->cmask.bits.alpha) - 1;
		wp->cmask.maxval.alpha = (double) wp->cmask.mask.alpha;
		shift += wp->cmask.bits.alpha;
	}
	return shift; /* == also sum of all bits */
}



/*****************************************************************************
 * 	s_set_outpixel_rgb
 *****************************************************************************/

static inline uint16_t float_to_s2_13(double d);

static inline unsigned long long s_set_outpixel_rgb(BMPWRITE_R wp,
	                      const unsigned char *restrict buffer, size_t offs)
{
	unsigned long long bytes;
	unsigned long      comp[4];
	int                i, alpha_offs = 0, outchannels, rgb = TRUE;
	double             source_max, dcomp[4];

	if (wp->source_channels < 3)
		rgb = FALSE; /* grayscale */

	if (wp->has_alpha) {
		alpha_offs = rgb ? 3 : 1;
		outchannels = 4;
	} else
		outchannels = 3; /* includes 64bit RGBA when no source alpha given */

	switch (wp->source_format) {
	case BMP_FORMAT_INT:
		source_max = (1ULL<<wp->source_bitsperchannel) - 1;
		switch(wp->source_bitsperchannel) {
		case 8:
			comp[0] =       buffer[offs];
			comp[1] = rgb ? buffer[offs+1] : comp[0];
			comp[2] = rgb ? buffer[offs+2] : comp[0];
			if (wp->has_alpha)
				comp[3] = buffer[offs+alpha_offs];
			break;
		case 16:
			comp[0] =       ((const uint16_t*)buffer)[offs];
			comp[1] = rgb ? ((const uint16_t*)buffer)[offs+1] : comp[0];
			comp[2] = rgb ? ((const uint16_t*)buffer)[offs+2] : comp[0];
			if (wp->has_alpha)
				comp[3] = ((const uint16_t*)buffer)[offs+alpha_offs];
			break;

		case 32:
			comp[0] =       ((const uint32_t*)buffer)[offs];
			comp[1] = rgb ? ((const uint32_t*)buffer)[offs+1] : comp[0];
			comp[2] = rgb ? ((const uint32_t*)buffer)[offs+2] : comp[0];
			if (wp->has_alpha)
				comp[3] = ((const uint32_t*)buffer)[offs+alpha_offs];
			break;

		default:
			logerr(wp->log, "Panic! Bitdepth (%d) other than 8/16/32",
				                     (int) wp->source_bitsperchannel);
			return (unsigned long long)-1;
		}
		for (i = 0; i < outchannels; i++) {
			comp[i] = comp[i] * (wp->out64bit ? 8192.0 : wp->cmask.maxval.val[i]) / source_max + 0.5;
		}
		break;

	case BMP_FORMAT_FLOAT:

		dcomp[0] =       ((const float*)buffer)[offs];
		dcomp[1] = rgb ? ((const float*)buffer)[offs+1] : dcomp[0];
		dcomp[2] = rgb ? ((const float*)buffer)[offs+2] : dcomp[0];
		if (wp->has_alpha)
			dcomp[3] = ((const float*)buffer)[offs+alpha_offs];

		if (wp->out64bit) {
			for (i = 0; i < outchannels; i++) {
				comp[i] = float_to_s2_13(dcomp[i]);
			}
		} else {
			for (i = 0; i < outchannels; i++) {
				if (dcomp[i] < 0.0)
					comp[i] = 0;
				else if (dcomp[i] > 1.0)
					comp[i] = wp->cmask.mask.value[i];
				else
					comp[i] = dcomp[i] * wp->cmask.maxval.val[i] + 0.5;
			}
		}
		break;

	case BMP_FORMAT_S2_13:
		comp[0] =       ((const uint16_t*)buffer)[offs];
		comp[1] = rgb ? ((const uint16_t*)buffer)[offs+1] : comp[0];
		comp[2] = rgb ? ((const uint16_t*)buffer)[offs+2] : comp[0];
		if (wp->has_alpha)
			comp[3] = ((const uint16_t*)buffer)[offs+alpha_offs];

		if (wp->out64bit) {
			/* pass through s2.13 */
			;
		} else {
			for (i = 0; i < outchannels; i++) {
				if (comp[i] & 0x8000)
					comp[i] = 0;
				else if (comp[i] > 0x2000)
					comp[i] = wp->cmask.mask.value[i];
				else
					comp[i] = comp[i] / 8192.0 * wp->cmask.maxval.val[i] + 0.5;
			}
		}
		break;

	default:
		logerr(wp->log, "Panic, invalid source number format %d", wp->source_format);
		return (unsigned long long) -1;
	}

	for (i = 0, bytes = 0; i < outchannels; i++) {
		bytes |= ((unsigned long long) (comp[i] & wp->cmask.mask.value[i])) << wp->cmask.shift.value[i];
	}
	if (!wp->has_alpha && wp->out64bit)
		bytes |= 8192ULL << wp->cmask.shift.alpha;

	return bytes;
}



/*****************************************************************************
 * 	float_to_s2_13
 *****************************************************************************/

static inline uint16_t float_to_s2_13(double d)
{
	uint16_t s2_13;

	d = round(d * 8192.0);

        if (d >= 32768.0)
                s2_13 = 0x7fff; /* max positive value */
        else if (d < -32768.0)
                s2_13 = 0x8000; /* min negative value */
        else
                s2_13 = (uint16_t) (0xffff & (int)d);


	return s2_13;
}



/*****************************************************************************
 * 	s_write_palette
 *****************************************************************************/

static int s_write_palette(BMPWRITE_R wp)
{
	int i, c;

	for (i = 0; i < wp->palette->numcolors; i++) {
		for (c = 0; c < 3; c++) {
			if (EOF == s_write_one_byte(wp->palette->color[i].value[2-c], wp))
				return FALSE;
		}
		if (EOF == s_write_one_byte(0, wp))
			return FALSE;
	}
	return TRUE;
}



/*****************************************************************************
 * 	s_write_bmp_file_header
 *****************************************************************************/

static int s_write_bmp_file_header(BMPWRITE_R wp)
{
	if (!(write_u16_le(wp->file, wp->fh->type) &&
	      write_u32_le(wp->file, wp->fh->size) &&
	      write_u16_le(wp->file, wp->fh->reserved1) &&
	      write_u16_le(wp->file, wp->fh->reserved2) &&
	      write_u32_le(wp->file, wp->fh->offbits))) {
		return FALSE;
	}
	wp->bytes_written += 14;
	return TRUE;
}



/*****************************************************************************
 * 	s_write_bmp_info_header
 *****************************************************************************/

static int s_write_bmp_info_header(BMPWRITE_R wp)
{
	int compression;

	switch (wp->ih->compression)
	{
	case BI_OS2_HUFFMAN:
		compression = BI_OS2_HUFFMAN_DUP;
		break;
	case BI_OS2_RLE24:
		compression = BI_OS2_RLE24_DUP;
		break;
	default:
		compression = wp->ih->compression;
		break;
	}

	if (!(write_u32_le(wp->file, wp->ih->size) &&
	      write_s32_le(wp->file, wp->ih->width) &&
	      write_s32_le(wp->file, wp->ih->height) &&
	      write_u16_le(wp->file, wp->ih->planes) &&
	      write_u16_le(wp->file, wp->ih->bitcount) &&
	      write_u32_le(wp->file, compression) &&
	      write_u32_le(wp->file, wp->ih->sizeimage) &&
	      write_s32_le(wp->file, wp->ih->xpelspermeter) &&
	      write_s32_le(wp->file, wp->ih->ypelspermeter) &&
	      write_u32_le(wp->file, wp->ih->clrused) &&
	      write_u32_le(wp->file, wp->ih->clrimportant) )) {
		return FALSE;
	}
	wp->bytes_written += 40;

	if (wp->ih->version == BMPINFO_V3)
		return TRUE;

	if (wp->ih->version == BMPINFO_OS22) {
#ifdef DEBUG
		if (wp->ih->size < 40) {
			logerr(wp->log, "Panic! Invalid header size %d", (int) wp->ih->size);
			return FALSE;
		}
#endif
		for (int i = 0; i < wp->ih->size - 40; i++) {
			if (EOF == putc(0, wp->file))
				return FALSE;
			wp->bytes_written++;
		}
		return TRUE;
	}

	if (!(write_u32_le(wp->file, wp->ih->redmask) &&
	      write_u32_le(wp->file, wp->ih->greenmask) &&
	      write_u32_le(wp->file, wp->ih->bluemask) &&
	      write_u32_le(wp->file, wp->ih->alphamask) &&
	      write_u32_le(wp->file, wp->ih->cstype) &&
	      write_s32_le(wp->file, wp->ih->redX) &&
	      write_s32_le(wp->file, wp->ih->redY) &&
	      write_s32_le(wp->file, wp->ih->redZ) &&
	      write_s32_le(wp->file, wp->ih->greenX) &&
	      write_s32_le(wp->file, wp->ih->greenY) &&
	      write_s32_le(wp->file, wp->ih->greenZ) &&
	      write_s32_le(wp->file, wp->ih->blueX) &&
	      write_s32_le(wp->file, wp->ih->blueY) &&
	      write_u32_le(wp->file, wp->ih->blueZ) &&
	      write_u32_le(wp->file, wp->ih->gammared) &&
	      write_u32_le(wp->file, wp->ih->gammagreen) &&
	      write_u32_le(wp->file, wp->ih->gammablue))) {
		return FALSE;
	}
	wp->bytes_written += 68;

	return TRUE;
}



/*****************************************************************************
 * 	s_write_one_byte
 *****************************************************************************/

static inline int s_write_one_byte(int byte, BMPWRITE_R wp)
{
	int ret;

	if (EOF != (ret = putc(byte, wp->file)))
		wp->bytes_written++;

	return ret;
}



/*****************************************************************************
 * 	bw_free
 *****************************************************************************/

void bw_free(BMPWRITE wp)
{
	if (wp->group)
		free(wp->group);
	if (wp->palette)
		free(wp->palette);
	if (wp->ih)
		free(wp->ih);
	if (wp->fh)
		free(wp->fh);
	if (wp->log)
		logfree(wp->log);

	free(wp);
}
