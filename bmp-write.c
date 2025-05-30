/* bmplib - bmp-write.c
 *
 * Copyright (c) 2024, 2025, Rupert Weber.
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
#include <stdbool.h>
#include <assert.h>
#include <math.h>

#define BMPLIB_LIB

#include "config.h"
#include "bmplib.h"
#include "logging.h"
#include "bmp-common.h"
#include "huffman.h"
#include "bmp-write.h"

static bool s_decide_outformat(BMPWRITE_R wp);
static bool s_write_palette(BMPWRITE_R wp);
static bool s_write_bmp_file_header(BMPWRITE_R wp);
static bool s_write_bmp_info_header(BMPWRITE_R wp);
static bool s_write_iccprofile(BMPWRITE_R wp);
static inline int s_write_one_byte(int byte, BMPWRITE_R wp);
static bool s_save_header(BMPWRITE_R wp);
static bool s_try_saving_image_size(BMPWRITE_R wp, uint64_t file_size, uint64_t image_size);
static bool s_finalize_file(BMPWRITE_R wp);
static int s_calc_mask_values(BMPWRITE_R wp);
static bool s_is_setting_compatible(BMPWRITE_R wp, const char *setting, ...);
static bool s_check_already_saved(BMPWRITE_R wp);
static bool s_check_save_started(BMPWRITE_R wp);
static bool s_ready_to_save(BMPWRITE_R wp);




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
	wp->c.magic = HMAGIC_WRITE;

	wp->rle_requested  = BMP_RLE_NONE;
	wp->outorientation = BMP_ORIENT_BOTTOMUP;
	wp->source_format  = BMP_FORMAT_INT;
	wp->huffman_fg_idx = 1;
	wp->write_state    = WS_INIT;

	if (!(wp->c.log = logcreate()))
		goto abort;

	if (!file) {
		logerr(wp->c.log, "Must supply file handle");
		goto abort;
	}

	wp->file = file;

	if (!(wp->fh = malloc(sizeof *wp->fh))) {
		logsyserr(wp->c.log, "allocating bmp file header");
		goto abort;
	}
	memset(wp->fh, 0, sizeof *wp->fh);

	if (!(wp->ih = malloc(sizeof *wp->ih))) {
		logsyserr(wp->c.log, "allocating bmp info header");
		goto abort;
	}
	memset(wp->ih, 0, sizeof *wp->ih);
	/* In case we need to write V4/V5 header: */
	wp->ih->cstype = LCS_WINDOWS_COLOR_SPACE;

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

	if (!(wp = cm_write_handle(h)))
		return BMP_RESULT_ERROR;

	if (s_check_save_started(wp))
		return BMP_RESULT_ERROR;

	if (!(s_is_setting_compatible(wp, "srcchannels", source_channels) &&
	      s_is_setting_compatible(wp, "srcbits", source_bitsperchannel))) {
	      	wp->write_state = WS_INIT;
		return BMP_RESULT_ERROR;
	}

	if (!cm_is_one_of(3, source_bitsperchannel, 8, 16, 32)) {
		logerr(wp->c.log, "Invalid number of bits per channel: %d",
		                                 (int) source_bitsperchannel);
	      	wp->write_state = WS_INIT;
		return BMP_RESULT_ERROR;
	}

	if (!cm_is_one_of(4, source_channels, 3, 4, 1, 2)) {
		logerr(wp->c.log, "Invalid number of channels: %d", (int) source_channels);
	      	wp->write_state = WS_INIT;
		return BMP_RESULT_ERROR;
	}

	wp->source_bytes_per_pixel = source_bitsperchannel / 8 * source_channels;

	if (width > INT32_MAX || height > INT32_MAX ||
	    width < 1 || height < 1 ||
	    (uint64_t) width * height > SIZE_MAX / wp->source_bytes_per_pixel) {
		logerr(wp->c.log, "Invalid dimensions %ux%ux%u @ %ubits",
		                          width, height, source_channels,
		                          source_bitsperchannel);
	      	wp->write_state = WS_INIT;
		return BMP_RESULT_ERROR;
	}

	wp->width = (int) width;
	wp->height = (int) height;
	wp->source_channels = (int) source_channels;
	wp->source_bitsperchannel = (int) source_bitsperchannel;
	wp->write_state = WS_DIMENSIONS_SET;

	return BMP_RESULT_OK;
}



/*****************************************************************************
 * 	bw_set_number_format
 *****************************************************************************/

BMPRESULT bw_set_number_format(BMPWRITE_R wp, enum BmpFormat format)
{
	if (format == wp->source_format)
		return BMP_RESULT_OK;

	if (s_check_save_started(wp))
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

	if (!(wp = cm_write_handle(h)))
		return BMP_RESULT_ERROR;

	if (s_check_save_started(wp))
		return BMP_RESULT_ERROR;

	if (!s_is_setting_compatible(wp, "outbits")) {
		wp->outbits_set = false;
		return BMP_RESULT_ERROR;
	}

	if (!(cm_all_positive_int(4, red, green, blue, alpha) &&
	      cm_all_lessoreq_int(32, 4, red, green, blue, alpha) &&
	      red + green + blue > 0 &&
	      red + green + blue + alpha <= 32 )) {
		logerr(wp->c.log, "Invalid output bit depths specified: %d-%d-%d - %d",
		                              red, green, blue, alpha);
		wp->outbits_set = false;
		return BMP_RESULT_ERROR;
	}

	wp->cmask.bits.red   = red;
	wp->cmask.bits.green = green;
	wp->cmask.bits.blue  = blue;
	wp->cmask.bits.alpha = alpha;

	wp->outbits_set = true;

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

	if (!(wp = cm_write_handle(h)))
		return BMP_RESULT_ERROR;

	if (s_check_save_started(wp))
		return BMP_RESULT_ERROR;

	if (wp->palette) {
		logerr(wp->c.log, "Palette already set. Cannot set twice");
		return BMP_RESULT_ERROR;
	}

	if (!s_is_setting_compatible(wp, "indexed"))
		return BMP_RESULT_ERROR;

	if (numcolors < 2 || numcolors > 256) {
		logerr(wp->c.log, "Invalid number of colors for palette (%d)",
		                                          numcolors);
		return BMP_RESULT_ERROR;
	}

	memsize = sizeof *wp->palette + numcolors * sizeof wp->palette->color[0];
	if (!(wp->palette = malloc(memsize))) {
		logsyserr(wp->c.log, "Allocating palette");
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
 * 	bmpwrite_set_iccprofile
 *****************************************************************************/

API BMPRESULT bmpwrite_set_iccprofile(BMPHANDLE h, size_t size,
                                      const unsigned char *iccprofile)
{
	BMPWRITE wp;

	static_assert(MAX_ICCPROFILE_SIZE < INT_MAX);

	if (!(wp = cm_write_handle(h)))
		return BMP_RESULT_ERROR;

	if (s_check_save_started(wp))
		return BMP_RESULT_ERROR;

	if (wp->iccprofile) {
		free(wp->iccprofile);
		wp->iccprofile = NULL;
		wp->iccprofile_size = 0;
		wp->ih->profilesize = 0;
		wp->ih->cstype = LCS_WINDOWS_COLOR_SPACE;
	}

	if (size > MAX_ICCPROFILE_SIZE) {
		logerr(wp->c.log, "ICC profile is too large (%zuMB). Max is %luMB.",
		                  size >> 20, (unsigned long)(MAX_ICCPROFILE_SIZE >> 20));
		return BMP_RESULT_ERROR;
	}

	if (!(wp->iccprofile = malloc(size))) {
		logsyserr(wp->c.log, "Allocating ICC profile");
		return BMP_RESULT_ERROR;
	}
	memcpy(wp->iccprofile, iccprofile, size);
	wp->iccprofile_size = (int)size;
	wp->ih->profilesize = size;
	wp->ih->cstype      = PROFILE_EMBEDDED;

	return BMP_RESULT_OK;
}



/*****************************************************************************
 * 	bmpwrite_set_rendering_intent
 *****************************************************************************/

API BMPRESULT bmpwrite_set_rendering_intent(BMPHANDLE h, BMPINTENT intent)
{
	BMPWRITE wp;

	if (!(wp = cm_write_handle(h)))
		return BMP_RESULT_ERROR;

	if (s_check_save_started(wp))
		return BMP_RESULT_ERROR;

	switch(intent) {
	case BMP_INTENT_NONE:
		wp->ih->intent = 0;
		break;
	case BMP_INTENT_BUSINESS:
		wp->ih->intent = LCS_GM_BUSINESS;
		break;
	case BMP_INTENT_GRAPHICS:
		wp->ih->intent = LCS_GM_GRAPHICS;
		break;
	case BMP_INTENT_IMAGES:
		wp->ih->intent = LCS_GM_IMAGES;
		break;
	case BMP_INTENT_ABS_COLORIMETRIC:
		wp->ih->intent = LCS_GM_ABS_COLORIMETRIC;
		break;
	default:
		logerr(wp->c.log, "Invalid redering intent: %d", (int) intent);
		return BMP_RESULT_ERROR;
	}
	return BMP_RESULT_OK;
}



/*****************************************************************************
 * 	bmpwrite_set_orientation
 *****************************************************************************/

API BMPRESULT bmpwrite_set_orientation(BMPHANDLE h, enum BmpOrient orientation)
{
	BMPWRITE wp;

	if (!(wp = cm_write_handle(h)))
		return BMP_RESULT_ERROR;

	if (s_check_save_started(wp))
		return BMP_RESULT_ERROR;

	switch (orientation) {
	case BMP_ORIENT_TOPDOWN:
		if (wp->rle_requested != BMP_RLE_NONE) {
			logerr(wp->c.log, "Topdown is invalid with RLE BMPs");
			return BMP_RESULT_ERROR;
		}
		break;

	case BMP_ORIENT_BOTTOMUP:
		/* always ok */
		break;

	default:
		logerr(wp->c.log, "Invalid orientation (%d)", (int) orientation);
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

	if (!(wp = cm_write_handle(h)))
		return BMP_RESULT_ERROR;

	if (s_check_save_started(wp))
		return BMP_RESULT_ERROR;

	if (!s_is_setting_compatible(wp, "rle", type))
		return BMP_RESULT_ERROR;

	if (!cm_is_one_of(3, (int) type, (int) BMP_RLE_NONE, (int) BMP_RLE_AUTO, (int) BMP_RLE_RLE8)) {
		logerr(wp->c.log, "Invalid RLE type specified (%d)", (int) type);
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

	if (!(wp = cm_write_handle(h)))
		return BMP_RESULT_ERROR;

	if (s_check_save_started(wp))
		return BMP_RESULT_ERROR;

	wp->ih->xpelspermeter = (int32_t) (100.0 / 2.54 * xdpi + 0.5);
	wp->ih->ypelspermeter = (int32_t) (100.0 / 2.54 * ydpi + 0.5);

	return BMP_RESULT_OK;
}



/*****************************************************************************
 * 	bmpwrite_allow_2bit
 *****************************************************************************/

API BMPRESULT bmpwrite_allow_2bit(BMPHANDLE h)
{
	BMPWRITE wp;

	if (!(wp = cm_write_handle(h)))
		return BMP_RESULT_ERROR;

	if (s_check_save_started(wp))
		return BMP_RESULT_ERROR;

	wp->allow_2bit = true;

	return BMP_RESULT_OK;
}



/*****************************************************************************
 * 	bmpwrite_allow_huffman
 *****************************************************************************/

API BMPRESULT bmpwrite_allow_huffman(BMPHANDLE h)
{
	BMPWRITE wp;

	if (!(wp = cm_write_handle(h)))
		return BMP_RESULT_ERROR;

	if (s_check_save_started(wp))
		return BMP_RESULT_ERROR;

	wp->allow_huffman = true;

	return BMP_RESULT_OK;
}



/*****************************************************************************
 * 	bmpwrite_allow_rle24
 *****************************************************************************/

API BMPRESULT bmpwrite_allow_rle24(BMPHANDLE h)
{
	BMPWRITE wp;

	if (!(wp = cm_write_handle(h)))
		return BMP_RESULT_ERROR;

	if (s_check_save_started(wp))
		return BMP_RESULT_ERROR;

	wp->allow_rle24 = true;

	return BMP_RESULT_OK;
}



/*****************************************************************************
 * 	bmpwrite_set_64bit
 *****************************************************************************/

API BMPRESULT bmpwrite_set_64bit(BMPHANDLE h)
{
	BMPWRITE wp;

	if (!(wp = cm_write_handle(h)))
		return BMP_RESULT_ERROR;

	if (s_check_save_started(wp))
		return BMP_RESULT_ERROR;

	if (!s_is_setting_compatible(wp, "64bit"))
		return BMP_RESULT_ERROR;

	wp->out64bit = true;
	return BMP_RESULT_OK;
}


/*****************************************************************************
 * 	bmpwrite_set_huffman_img_fg_idx
 *****************************************************************************/

API BMPRESULT bmpwrite_set_huffman_img_fg_idx(BMPHANDLE h, int idx)
{
	BMPWRITE wp;

	if (!(wp = cm_write_handle(h)))
		return BMP_RESULT_ERROR;

	if (s_check_save_started(wp))
		return BMP_RESULT_ERROR;

	wp->huffman_fg_idx = !!idx;

	return BMP_RESULT_OK;
}


/*****************************************************************************
 * 	s_check_already_saved
 *****************************************************************************/

static bool s_check_already_saved(BMPWRITE_R wp)
{
	if (wp->write_state >= WS_SAVE_DONE) {
		logerr(wp->c.log, "Image already saved.");
		return true;
	}
	return false;
}


/*****************************************************************************
 * 	s_check_save_started
 *****************************************************************************/

static bool s_check_save_started(BMPWRITE_R wp)
{
	if (wp->write_state >= WS_SAVE_STARTED) {
		logerr(wp->c.log, "Image save already started.");
		return true;
	}
	return false;
}



/*****************************************************************************
 * 	s_is_setting_compatible
 *
 * setting: "outbits", "srcbits", "srcchannels", "indexed",
 *          "format", "indexed", "64bit", "rle"
 *****************************************************************************/

static bool s_is_setting_compatible(BMPWRITE_R wp, const char *setting, ...)
{
	int             channels, bits;
	enum BmpFormat  format;
	enum BmpRLEtype rle;
	enum BmpOrient  orientation;
	int             ret = true;
	va_list         args;

	va_start(args, setting);

	if (!strcmp(setting, "outbits")) {
		if (wp->palette || wp->out64bit || (wp->rle_requested != BMP_RLE_NONE)) {
			logerr(wp->c.log, "output bits cannot be set with indexed, RLE, "
			       "or 64bit BMPs");
			ret = false;
		}
	} else if (!strcmp(setting, "srcbits")) {
		bits = va_arg(args, int);
		if (wp->palette && bits != 8) {
			logerr(wp->c.log, "indexed images must be 8 bits (not %d)", bits);
			ret = false;
		} else if (wp->source_format == BMP_FORMAT_FLOAT && bits != 32) {
			logerr(wp->c.log, "float images must be 32 bits per channel (not %d)", bits);
			ret = false;
		} else if (wp->source_format == BMP_FORMAT_S2_13 && bits != 16) {
			logerr(wp->c.log, "s2.13 images must be 16 bits per channel (not %d)", bits);
			ret = false;
		}
	} else if (!strcmp(setting, "srcchannels")) {
		channels = va_arg(args, int);
		if (wp->palette && (channels != 1)) {
			logerr(wp->c.log, "Indexed images must have 1 channel (not %d)", channels);
			ret = false;
		}
		if (wp->out64bit && (channels != 3 && channels != 4)) {
			logerr(wp->c.log, "64bit images must have 3 or 4 channels (not %d)", channels);
			ret = false;
		}
	} else if (!strcmp(setting, "indexed")) {
		if (wp->out64bit) {
			logerr(wp->c.log, "64bit BMPs cannot be indexed");
			ret = false;
		}
		if (wp->outbits_set) {
			logerr(wp->c.log, "BMPs with specified channel bits cannot be indexed");
			ret = false;
		}
		if (wp->source_format != BMP_FORMAT_INT) {
			logerr(wp->c.log, "Indexed image must have INT format (not %s)",
			             cm_format_name(wp->source_format));
			ret = false;
		}
		if (wp->write_state >= WS_DIMENSIONS_SET) {
			if (!(wp->source_channels == 1 && wp->source_bitsperchannel == 8)) {
				logerr (wp->c.log, "Indexed images must be 1 channel, 8 bits");
				ret = false;
			}
		}
	} else if (!strcmp(setting, "format")) {
		format = va_arg(args, enum BmpFormat);
		switch (format) {
		case BMP_FORMAT_FLOAT:
			if (wp->write_state >= WS_DIMENSIONS_SET && wp->source_bitsperchannel != 32) {
				logerr(wp->c.log, "float cannot be %d bits per pixel",
				                           wp->source_bitsperchannel);
				ret = false;
			}
			if (wp->palette) {
				logerr(wp->c.log, "float cannot be used for indexed images");
				ret = false;
			}
			break;
		case BMP_FORMAT_S2_13:
			if (wp->write_state >= WS_DIMENSIONS_SET && wp->source_bitsperchannel != 16) {
				logerr(wp->c.log, "s2.13 cannot be %d bits per pixel",
				                           wp->source_bitsperchannel);
				ret = false;
			}
			if (wp->palette) {
				logerr(wp->c.log, "s2.13 cannot be used for indexed images");
				ret = false;
			}
			break;
		default:
			/* INT is ok with everything */
			break;
		}
	} else if (!strcmp(setting, "rle")) {
		rle = va_arg(args, enum BmpRLEtype);
		if (rle != BMP_RLE_NONE) {
			if (wp->outorientation != BMP_ORIENT_BOTTOMUP) {
				logerr(wp->c.log, "RLE is invalid with top-down BMPs");
				ret = false;
			}
		}

	} else if (!strcmp(setting, "orientation")) {
		orientation = va_arg(args, enum BmpOrient);
		if (orientation == BMP_ORIENT_TOPDOWN) {
			if (wp->rle_requested != BMP_RLE_NONE) {
				logerr(wp->c.log, "RLE is invalid with top-down BMPs");
				ret = false;
			}
		}
	} else if (!strcmp(setting, "64bit")) {
		if (wp->palette) {
			logerr(wp->c.log, "Indexed images cannot be 64bit");
			ret = false;
		}
		if (wp->outbits_set) {
			logerr(wp->c.log, "BMPs with specified channel bits cannot be 64bit");
			ret = false;
		}
	} else {
		logerr(wp->c.log, "Panic, invalid setting check for '%s'", setting);
		ret = false;
	}

	va_end(args);
	return ret;
}



/*****************************************************************************
 * 	s_infoheader_size
 *****************************************************************************/

static uint32_t s_infoheader_size(enum BmpInfoVer version)
{
	switch(version) {
		case BMPINFO_CORE_OS21: return  12;
		case BMPINFO_OS22:      return  64;
		case BMPINFO_V3:        return  40;
		case BMPINFO_V3_ADOBE1: return  52;
		case BMPINFO_V3_ADOBE2: return  56;
		case BMPINFO_V4:        return 108;
		case BMPINFO_V5:        return 124;
		default:
			return  -1;
	}
}

/*****************************************************************************
 * 	s_decide_outformat
 *****************************************************************************/

static bool s_decide_outformat(BMPWRITE_R wp)
{
	int             bitsum = 0;
	uint64_t        bitmapsize, filesize, bytes_per_line;
	enum BmpInfoVer version = BMPINFO_OS22, maxversion = BMPINFO_V5;

	if (wp->iccprofile || (wp->ih->intent != 0))
		version = MAX(BMPINFO_V5, version);

	if (wp->source_channels == 4 || wp->source_channels == 2)
		wp->source_has_alpha = true;
	else
		wp->source_has_alpha = false;

	if (!wp->outbits_set && !wp->palette) {
		if (wp->out64bit) {
			wp->cmask.bits.red   = 16;
			wp->cmask.bits.green = 16;
			wp->cmask.bits.blue  = 16;
			wp->cmask.bits.alpha = 16; /* 64bit always has alpha channel */
			version = MAX(BMPINFO_V3, version);
		} else {
			wp->cmask.bits.red = wp->cmask.bits.green = wp->cmask.bits.blue = 8;
			if (wp->source_has_alpha)
				wp->cmask.bits.alpha = 8;
		}
	}

	if (wp->palette) {
		if (wp->source_channels > 1) {
			logerr(wp->c.log, "Panic! Palette set with %d source channels",
			                  wp->source_channels);
			return false;
		}
		if (wp->rle_requested != BMP_RLE_NONE) {
			if (wp->palette->numcolors > 16 || wp->rle_requested == BMP_RLE_RLE8) {
				wp->rle = 8;
				wp->ih->compression = BI_RLE8;
				wp->ih->bitcount    = 8;
				version = MAX(BMPINFO_V3, version);

			} else if (wp->palette->numcolors > 2 || !wp->allow_huffman || version > BMPINFO_OS22) {
				wp->rle = 4;
				wp->ih->compression = BI_RLE4;
				wp->ih->bitcount    = 4;
				version = MAX(BMPINFO_V3, version);

			} else {
				wp->rle = 1;
				wp->ih->compression = BI_OS2_HUFFMAN;
				wp->ih->bitcount    = 1;
				version = MAX(BMPINFO_OS22, version);
				maxversion = BMPINFO_OS22;
			}

		} else {
			wp->ih->compression = BI_RGB;
			wp->ih->bitcount = 1;
			while ((1<<wp->ih->bitcount) < wp->palette->numcolors)
				wp->ih->bitcount *= 2;
			if (wp->ih->bitcount == 2 && !wp->allow_2bit)
				wp->ih->bitcount = 4;
		}

	} else if (wp->allow_rle24 && wp->source_channels == 3 && wp->source_bitsperchannel == 8 &&
	           wp->rle_requested == BMP_RLE_AUTO && version <= BMPINFO_OS22) {
		wp->rle = 24;
		wp->ih->compression = BI_OS2_RLE24;
		wp->ih->bitcount    = 24;
		version = MAX(BMPINFO_OS22, version);
		maxversion = BMPINFO_OS22;

	} else {
		/* RGB */

		bitsum = s_calc_mask_values(wp);

		if (bitsum < 64 && (!cm_all_equal_int(3, (int) wp->cmask.bits.red,
		                                         (int) wp->cmask.bits.green,
		                                         (int) wp->cmask.bits.blue) ||
		                     wp->source_has_alpha ||
		                     (wp->cmask.bits.red > 0  &&
		                      wp->cmask.bits.red != 5 &&
		                      wp->cmask.bits.red != 8    )    )) {
			/* we need BI_BITFIELDS if any of the following is true and we are not
			 * writing a 64bit BMP:
			 *    - not all RGB-components have the same bitlength
			 *    - we are writing an alpha-channel
			 *    - bits per component are not either 5 or 8 (which have
			 *      known RI_RGB representation)
			 */

			version = MAX(BMPINFO_V4, version);
			wp->ih->compression = BI_BITFIELDS;

			if (bitsum <= 16)
				wp->ih->bitcount = 16;
			else
				wp->ih->bitcount = 32;
		} else {
			/* otherwise, use BI_RGB with either 5 or 8 bits per component
			 * resulting in bitcount of 16 or 24, or a 64bit BMP with 16 bits/comp.
			 */
			wp->ih->compression = BI_RGB;
			wp->ih->bitcount    = (bitsum + 7) / 8 * 8;
		}
	}

	if (version > maxversion) {
		logerr(wp->c.log, "Panic! Info header version conflict. Have %s, need %s",
		                  cm_infoheader_name(version), cm_infoheader_name(maxversion));
		return false;
	}

	/* always use at least V3, unless a smaller version is required */
	version = MAX(MIN(BMPINFO_V3, maxversion), version);

	wp->ih->version = version;
	wp->ih->size    = s_infoheader_size(version);

	if (wp->palette) {
		wp->ih->clrused = wp->palette->numcolors;
	} else {
		wp->outbytes_per_pixel = wp->ih->bitcount / 8;

		if (wp->ih->version >= BMPINFO_V4 && !wp->out64bit) {
			wp->ih->redmask   = (uint32_t) (wp->cmask.mask.red   << wp->cmask.shift.red);
			wp->ih->greenmask = (uint32_t) (wp->cmask.mask.green << wp->cmask.shift.green);
			wp->ih->bluemask  = (uint32_t) (wp->cmask.mask.blue  << wp->cmask.shift.blue);
			wp->ih->alphamask = (uint32_t) (wp->cmask.mask.alpha << wp->cmask.shift.alpha);
		}
	}

	bytes_per_line = ((uint64_t) wp->width * wp->ih->bitcount + 7) / 8;
	wp->padding    = cm_align4padding(bytes_per_line);
	bitmapsize     = (bytes_per_line + wp->padding) * wp->height;
	filesize       = bitmapsize + BMPFHSIZE + wp->ih->size + wp->palette_size + wp->iccprofile_size;

	wp->fh->type = BMPFILE_BM;
	wp->fh->size = (uint32_t) ((wp->rle || filesize > UINT32_MAX) ? 0 : filesize);
	wp->fh->offbits = BMPFHSIZE + wp->ih->size + wp->palette_size;

	wp->ih->width = wp->width;
	if (wp->outorientation == BMP_ORIENT_BOTTOMUP)
		wp->ih->height = wp->height;
	else
		wp->ih->height = -wp->height;
	wp->ih->planes = 1;
	wp->ih->sizeimage = (uint32_t) ((wp->rle || bitmapsize > UINT32_MAX) ? 0 : bitmapsize);

	if (wp->iccprofile) {
		uint64_t profileoffset = bitmapsize + wp->ih->size + wp->palette_size;
		wp->ih->profiledata = (uint32_t) ((wp->rle || profileoffset > UINT32_MAX) ? 0 : profileoffset);
	}

	return true;
}


/*****************************************************************************
 * 	bmpwrite_save_image
 *****************************************************************************/
static bool s_save_line(BMPWRITE_R wp, const unsigned char *line);

API BMPRESULT bmpwrite_save_image(BMPHANDLE h, const unsigned char *image)
{
	BMPWRITE wp;
	size_t   linesize, real_y;
	int      y;

	if (!(wp = cm_write_handle(h)))
		return BMP_RESULT_ERROR;

	if (s_check_already_saved(wp))
		return BMP_RESULT_ERROR;

	if (s_check_save_started(wp))
		return BMP_RESULT_ERROR;

	if (!s_ready_to_save(wp))
		return BMP_RESULT_ERROR;

	linesize = (size_t)wp->width * wp->source_bytes_per_pixel;
	for (y = 0; y < wp->height; y++) {
		real_y = (size_t) ((wp->outorientation == BMP_ORIENT_TOPDOWN) ? y : wp->height - y - 1);

		if (!s_save_line(wp, image + real_y * linesize)) {
			wp->write_state = WS_FATAL;
			return BMP_RESULT_ERROR;
		}
	}

	return BMP_RESULT_OK;
}


/*****************************************************************************
 * 	bmpwrite_save_line
 *****************************************************************************/

API BMPRESULT bmpwrite_save_line(BMPHANDLE h, const unsigned char *line)
{
	BMPWRITE wp;

	if (!(wp = cm_write_handle(h)))
		return BMP_RESULT_ERROR;

	if (s_check_already_saved(wp))
		return BMP_RESULT_ERROR;

	if (!s_ready_to_save(wp))
		return BMP_RESULT_ERROR;

	if (!s_save_line(wp, line))
		return BMP_RESULT_ERROR;

	return BMP_RESULT_OK;
}


/*****************************************************************************
 * 	s_save_line
 *****************************************************************************/
static bool s_save_line_rgb(BMPWRITE_R wp, const unsigned char *line);
static bool s_save_line_rle(BMPWRITE_R wp, const unsigned char *line);
static bool s_save_line_huff(BMPWRITE_R wp, const unsigned char *line);

static bool s_save_line(BMPWRITE_R wp, const unsigned char *line)
{
	bool     res;

	if (wp->write_state < WS_SAVE_STARTED) {
		if  (!s_save_header(wp))
			goto fatal;

		wp->write_state = WS_SAVE_STARTED;
		wp->bytes_written_before_bitdata = wp->bytes_written;
	}

	switch (wp->rle) {
	case 4:
	case 8:
	case 24:
		res = s_save_line_rle(wp, line);
		break;
	case 1:
		res = s_save_line_huff(wp, line);
		break;
	default:
		res = s_save_line_rgb(wp, line);
		break;
	}

	if (!res)
		goto fatal;

	if (++wp->lbl_y >= wp->height) {
		if (wp->rle) {
			if (wp->rle > 1) {
				if (EOF == s_write_one_byte(0, wp) ||
				    EOF == s_write_one_byte(1, wp)) {
					logsyserr(wp->c.log, "Writing RLE end-of-file marker");
					goto fatal;
				}
			} else {
				if (!(huff_encode_rtc(wp) && huff_flush(wp))) {
					logsyserr(wp->c.log, "Writing RTC end-of-file marker");
					goto fatal;
				}
			}
		}

		if (!s_finalize_file(wp))
			goto fatal;

		wp->write_state = WS_SAVE_DONE;
	}
	return true;

fatal:
	wp->write_state = WS_FATAL;
	return false;
}



/*****************************************************************************
 * 	s_ready_to_save
 *****************************************************************************/

static bool s_ready_to_save(BMPWRITE_R wp)
{
	if (wp->write_state < WS_DIMENSIONS_SET) {
		logerr(wp->c.log, "Must set dimensions before saving");
		return false;
	}
	return true;
}

/*****************************************************************************
 * 	s_save_header
 *****************************************************************************/

static bool s_save_header(BMPWRITE_R wp)
{
	if (!s_decide_outformat(wp))
		return false;

	if (!s_write_bmp_file_header(wp)) {
		logsyserr(wp->c.log, "Writing BMP file header");
		return false;
	}

	if (!s_write_bmp_info_header(wp)) {
		logsyserr(wp->c.log, "Writing BMP info header");
		return false;
	}

	if (wp->palette) {
		if (!s_write_palette(wp)) {
			logsyserr(wp->c.log, "Couldn't write palette");
			return false;
		}
	}

	return true;
}



/*****************************************************************************
 * s_finalize_file
 *
 * write anything that has to be written after the bitdata has been written
 *****************************************************************************/

static bool s_finalize_file(BMPWRITE_R wp)
{
	uint64_t file_size, img_size;

	file_size = wp->bytes_written;
	img_size  = file_size - wp->bytes_written_before_bitdata;

	if (wp->iccprofile) {
		if (!s_write_iccprofile(wp))
			return false;
		file_size += wp->iccprofile_size;
	}

	if (wp->rle) {
		if (!s_try_saving_image_size(wp, file_size, img_size))
			return false;
	}

	return true;
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
 * when they are too big for the respective fields.
 *****************************************************************************/

static bool s_try_saving_image_size(BMPWRITE_R wp, uint64_t file_size, uint64_t image_size)
{
	if (fseek(wp->file, 2, SEEK_SET))        /* file header -> bfSize */
		return false;
	if (file_size <= UINT32_MAX && !write_u32_le(wp->file, (uint32_t) file_size))
		return false;
	if (fseek(wp->file, 14 + 20, SEEK_SET))  /* info header -> biSizeImage */
		return false;
	if (image_size <= UINT32_MAX && !write_u32_le(wp->file, (uint32_t) image_size))
		return false;
	return true;
}



/*****************************************************************************
 * 	s_save_line_rgb
 *****************************************************************************/
static inline unsigned long long s_imgrgb_to_outbytes(BMPWRITE_R wp,
	                                   const unsigned char *restrict imgpx);

static bool s_save_line_rgb(BMPWRITE_R wp, const unsigned char *line)
{
	size_t             offs;
	unsigned long long bytes = 0;
	int                i, x, bits_used = 0;

	for (x = 0; x < wp->width; x++) {
		offs = (size_t) x * (size_t) wp->source_bytes_per_pixel;
		if (wp->palette) {
			bytes <<= wp->ih->bitcount;
			bytes |= line[offs];
			bits_used += wp->ih->bitcount;
			if (bits_used == 8) {
				if (EOF == s_write_one_byte((int)bytes, wp)) {
					logsyserr(wp->c.log, "Writing image to BMP file");
					return false;
				}
				bytes = 0;
				bits_used = 0;
			}
		} else {
			bytes = s_imgrgb_to_outbytes(wp, line + offs);
			if (bytes == (unsigned long long)-1)
				return BMP_RESULT_ERROR;

			for (i = 0; i < wp->outbytes_per_pixel; i++) {
				if (EOF == s_write_one_byte((bytes >> (8*i)) & 0xff, wp)) {
					logsyserr(wp->c.log, "Writing image to BMP file");
					return false;
				}
			}
		}
	}

	if (wp->palette && bits_used != 0) {
		bytes <<= 8 - bits_used;
		if (EOF == s_write_one_byte((int)bytes, wp)) {
			logsyserr(wp->c.log, "Writing image to BMP file");
			return false;
		}
		bits_used = 0;
	}

	for (i = 0; i < wp->padding; i++) {
		if (EOF == s_write_one_byte(0, wp)) {
			logsyserr(wp->c.log, "Writing padding bytes to BMP file");
			return false;
		}
	}
	return true;
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

static inline int s_length_of_runs(BMPWRITE_R wp, int group, int minlen)
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
 * 	s_save_line_rle
 *****************************************************************************/

static bool s_save_line_rle(BMPWRITE_R wp, const unsigned char *line)
{
	int  i, j, k, x, l, dx, outbyte = 0;
	bool even;
	int  small_number = 0, minlen = 0;

	switch (wp->rle) {
	case 4:
		small_number = 7;
		minlen = 3;
		break;
	case 8:
		small_number = 5;
		minlen = 2;
		break;
	case 24:
		small_number = 2;
		minlen = 2;
		break;
	default:
		assert(0);
		break;
	}

	if (!wp->group) {
		if (!(wp->group = malloc(wp->width * sizeof *wp->group))) {
			logsyserr(wp->c.log, "allocating RLE buffer");
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

		if (wp->group[wp->group_count] == 255) {
			wp->group_count++;
			continue;
		}

		if (x == wp->width - 1) {
			wp->group_count++;
			break;
		}

		if (wp->rle == 4) {
			if (wp->group[wp->group_count] > 1 && line[x-1] != line[x+1])
				wp->group_count++;

		} else if (wp->rle == 8) {
			if (line[x] != line[x+1])
				wp->group_count++;

		} else if (wp->rle == 24) {
			for (int c = 0; c < 3; c++) {
				if (line[3*x + c] != line[3*(x+1) + c]) {
					wp->group_count++;
					break;
				}
			}
		}
	}

	x = 0;
	for (i = 0; i < wp->group_count; i++) {
		l = 0;  /* l counts the number of groups in this literal run */
		dx = 0; /* dx counts the number of pixels in this literal run */
		while (i+l < wp->group_count && wp->group[i+l] < minlen && (dx + wp->group[i+l]) < 255) {
			/* start/continue a literal run */
			dx += wp->group[i+l];
			l++;

			/* if only a small number of repeated pixels comes up, include
			 * those in the literal run instead of switching to repeat-run.
			 * Not perfect, but already much better than interrupting a literal
			 * run for e.g. two repeated pixels and then restarting the literal
			 * run at a cost of 2-4 bytes (depending on padding)
			 */
			if (i+l < wp->group_count && s_length_of_runs(wp, i+l, minlen) <= small_number) {
				while (i+l < wp->group_count && wp->group[i+l] > (minlen-1) && dx + wp->group[i+l] < 255) {
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
			even = true;
			for (j = 0; j < l; j++) {
				for (k = 0; k < wp->group[i+j]; k++) {
					if (wp->rle == 4) {
						if (even)
							outbyte = (line[x++] << 4) & 0xf0;
						else {
							outbyte |= line[x++] & 0x0f;
							if (EOF == s_write_one_byte(outbyte, wp)) {
								goto abort;
							}
						}
						even = !even;
					} else if (wp->rle == 8) {
						if (EOF == s_write_one_byte(line[x++], wp)) {
							goto abort;
						}
					} else if (wp->rle == 24) {
						if (EOF == s_write_one_byte(line[3*x+2], wp) ||
						    EOF == s_write_one_byte(line[3*x+1], wp) ||
						    EOF == s_write_one_byte(line[3*x+0], wp)) {
							goto abort;
						}
						x++;
						even = !even;
					}

				}
			}
			if (wp->rle == 4 && !even) {
				/* write last nibble for RLE4 */
				if (EOF == s_write_one_byte(outbyte, wp)) {
					goto abort;
				}
			}
			/* padding, if neccessary */
			if ((wp->rle == 4 && (dx+1)%4 > 1) ||
			    (wp->rle == 8 && (dx & 0x01)) ||
			    (wp->rle == 24 && (dx & 0x01))) {
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

		if (wp->rle == 4) {
			outbyte = (line[x] << 4) & 0xf0;
			if (wp->group[i] > 1)
				outbyte |= line[x+1] & 0x0f;
			if (EOF == s_write_one_byte(outbyte, wp)) {
				goto abort;
			}
		} else if (wp->rle == 8) {
			if (EOF == s_write_one_byte(line[x], wp)) {
				goto abort;
			}
		} else if (wp->rle == 24) {
			if (EOF == s_write_one_byte(line[3*x+2], wp) ||
			    EOF == s_write_one_byte(line[3*x+1], wp) ||
			    EOF == s_write_one_byte(line[3*x+0], wp)) {
				goto abort;
			}
		}
		x += wp->group[i];
	}

	if (EOF == s_write_one_byte(0, wp) || EOF == s_write_one_byte(0, wp)) {  /* EOL */
		goto abort;
	}

	return true;
abort:
	if (wp->group) {
		free(wp->group);
		wp->group = NULL;
		wp->group_count = 0;
	}
	logsyserr(wp->c.log, "Writing RLE data to BMP file");
	return false;
}



/*****************************************************************************
 * 	s_save_line_huff
 *****************************************************************************/

static bool s_save_line_huff(BMPWRITE_R wp, const unsigned char *line)
{
	int  x = 0, len;
	bool black = false, flipbits;

	flipbits = !wp->huffman_fg_idx ^ wp->c.huffman_black_is_zero;

	if (!huff_encode_eol(wp)) /* each line starts with eol */
		goto abort;

	while (x < wp->width) {
		len = 0;
		while ((len < wp->width - x) && ((!!line[x + len]) == (black ^ flipbits)))
			len++;
		if (!huff_encode(wp, len, black))
			goto abort;
		black = !black;
		x += len;
	}
	return true;

abort:
	return false;
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
 * 	s_imgrgb_to_outbytes
 *****************************************************************************/

static inline uint16_t s_float_to_2_13(double d);

static inline unsigned long long s_imgrgb_to_outbytes(BMPWRITE_R wp,
	                                   const unsigned char *restrict imgpx)
{
	unsigned long long bytes;
	unsigned long      comp[4];
	int                i, alpha_offs = 0, outchannels;
	bool               rgb = true;
	double             source_max, dcomp[4];

	if (wp->source_channels < 3)
		rgb = false; /* grayscale */

	if (wp->source_has_alpha) {
		alpha_offs = rgb ? 3 : 1;
		outchannels = 4;
	} else
		outchannels = 3; /* includes 64bit RGBA when no source alpha given */

	switch (wp->source_format) {
	case BMP_FORMAT_INT:
		source_max = (double) ((1ULL<<wp->source_bitsperchannel) - 1);
		switch(wp->source_bitsperchannel) {
		case 8:
			comp[0] =       imgpx[0];
			comp[1] = rgb ? imgpx[1] : comp[0];
			comp[2] = rgb ? imgpx[2] : comp[0];
			if (wp->source_has_alpha)
				comp[3] = imgpx[alpha_offs];
			break;
		case 16:
			comp[0] =       ((const uint16_t*)imgpx)[0];
			comp[1] = rgb ? ((const uint16_t*)imgpx)[1] : comp[0];
			comp[2] = rgb ? ((const uint16_t*)imgpx)[2] : comp[0];
			if (wp->source_has_alpha)
				comp[3] = ((const uint16_t*)imgpx)[alpha_offs];
			break;

		case 32:
			comp[0] =       ((const uint32_t*)imgpx)[0];
			comp[1] = rgb ? ((const uint32_t*)imgpx)[1] : comp[0];
			comp[2] = rgb ? ((const uint32_t*)imgpx)[2] : comp[0];
			if (wp->source_has_alpha)
				comp[3] = ((const uint32_t*)imgpx)[alpha_offs];
			break;

		default:
			logerr(wp->c.log, "Panic! Bitdepth (%d) other than 8/16/32",
				                     (int) wp->source_bitsperchannel);
			return (unsigned long long)-1;
		}
		for (i = 0; i < outchannels; i++) {
			comp[i] = (unsigned long) (comp[i] * (wp->out64bit ? 8192.0 :
				                   wp->cmask.maxval.val[i]) / source_max + 0.5);
		}
		break;

	case BMP_FORMAT_FLOAT:

		dcomp[0] =       ((const float*)imgpx)[0];
		dcomp[1] = rgb ? ((const float*)imgpx)[1] : dcomp[0];
		dcomp[2] = rgb ? ((const float*)imgpx)[2] : dcomp[0];
		if (wp->source_has_alpha)
			dcomp[3] = ((const float*)imgpx)[alpha_offs];

		if (wp->out64bit) {
			for (i = 0; i < outchannels; i++) {
				comp[i] = s_float_to_2_13(dcomp[i]);
			}
		} else {
			for (i = 0; i < outchannels; i++) {
				if (dcomp[i] < 0.0)
					comp[i] = 0;
				else if (dcomp[i] > 1.0)
					comp[i] = (unsigned long) wp->cmask.mask.value[i];
				else
					comp[i] = (unsigned long) (dcomp[i] * wp->cmask.maxval.val[i] + 0.5);
			}
		}
		break;

	case BMP_FORMAT_S2_13:
		comp[0] =       ((const uint16_t*)imgpx)[0];
		comp[1] = rgb ? ((const uint16_t*)imgpx)[1] : comp[0];
		comp[2] = rgb ? ((const uint16_t*)imgpx)[2] : comp[0];
		if (wp->source_has_alpha)
			comp[3] = ((const uint16_t*)imgpx)[alpha_offs];

		if (wp->out64bit) {
			/* pass through s2.13 */
			;
		} else {
			for (i = 0; i < outchannels; i++) {
				if (comp[i] & 0x8000)
					comp[i] = 0;
				else if (comp[i] > 0x2000)
					comp[i] = (unsigned long) wp->cmask.mask.value[i];
				else
					comp[i] = (unsigned long) (comp[i] / 8192.0 * wp->cmask.maxval.val[i] + 0.5);
			}
		}
		break;

	default:
		logerr(wp->c.log, "Panic, invalid source number format %d", (int) wp->source_format);
		return (unsigned long long) -1;
	}

	for (i = 0, bytes = 0; i < outchannels; i++) {
		bytes |= ((unsigned long long)comp[i] & wp->cmask.mask.value[i]) << wp->cmask.shift.value[i];
	}
	if (!wp->source_has_alpha && wp->out64bit)
		bytes |= 8192ULL << wp->cmask.shift.alpha;

	return bytes;
}



/*****************************************************************************
 * 	s_float_to_2_13
 * 	(duplicate from bmp-read-loadimage.c)
 *****************************************************************************/

static inline uint16_t s_float_to_2_13(double d)
{
	d = MIN(d, 3.99987793);
	d = MAX(-4.0, d);
	return (uint16_t) ((int)round(d * 8192.0) & 0xffff);
}



/*****************************************************************************
 * 	s_write_palette
 *****************************************************************************/

static bool s_write_palette(BMPWRITE_R wp)
{
	int  i, c;
	bool reverse = false;

	if (wp->rle == 1)
		reverse = !wp->huffman_fg_idx;

	for (i = 0; i < wp->palette->numcolors; i++) {
		int idx = reverse ? wp->palette->numcolors - i - 1 : i;
		for (c = 0; c < 3; c++) {
			if (EOF == s_write_one_byte(wp->palette->color[idx].value[2-c], wp))
				return false;
		}
		if (EOF == s_write_one_byte(0, wp))
			return false;
	}
	return true;
}



/*****************************************************************************
 * 	s_write_bmp_file_header
 *****************************************************************************/

static bool s_write_bmp_file_header(BMPWRITE_R wp)
{
	if (!(write_u16_le(wp->file, wp->fh->type) &&
	      write_u32_le(wp->file, wp->fh->size) &&
	      write_u16_le(wp->file, wp->fh->reserved1) &&
	      write_u16_le(wp->file, wp->fh->reserved2) &&
	      write_u32_le(wp->file, wp->fh->offbits))) {
		return false;
	}
	wp->bytes_written += 14;
	return true;
}



/*****************************************************************************
 * 	s_write_bmp_info_header
 *****************************************************************************/

static bool s_write_bmp_info_header(BMPWRITE_R wp)
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
		return false;
	}
	wp->bytes_written += 40;

	if (wp->ih->version == BMPINFO_V3)
		return true;

	if (wp->ih->version == BMPINFO_OS22) {
#ifdef DEBUG
		if (wp->ih->size < 40) {
			logerr(wp->c.log, "Panic! Invalid header size %d", (int) wp->ih->size);
			return false;
		}
#endif
		for (int i = 0; (uint32_t) i < wp->ih->size - 40; i++) {
			if (EOF == putc(0, wp->file))
				return false;
			wp->bytes_written++;
		}
		return true;
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
		return false;
	}
	wp->bytes_written += 68;

	if (wp->ih->version == BMPINFO_V4)
		return true;

	if (!(write_u32_le(wp->file, wp->ih->intent) &&
	      write_u32_le(wp->file, wp->ih->profiledata) &&
	      write_u32_le(wp->file, wp->ih->profilesize) &&
	      write_u32_le(wp->file, wp->ih->reserved)))
		return false;

	wp->bytes_written += 16;

	return true;
}



/*****************************************************************************
 * 	s_write_iccprofile
 *****************************************************************************/

static bool s_write_iccprofile(BMPWRITE_R wp)
{
	if (wp->ih->version < BMPINFO_V5 || !wp->iccprofile)
		return false;

	uint64_t pos = wp->bytes_written;

	if (wp->iccprofile_size != (int) fwrite(wp->iccprofile, 1, wp->iccprofile_size, wp->file)) {
		logsyserr(wp->c.log, "Error writing ICC profile to file");
		return false;
	}

	wp->bytes_written += wp->iccprofile_size;

	if (wp->rle) {
		if (fseek(wp->file, IH_PROFILEDATA_OFFSET, SEEK_SET)) {
			logsyserr(wp->c.log, "Error writing ICC profile to file");
			return false;
		}

		if (!write_u32_le(wp->file, pos - 14))
			return false;

		if (wp->bytes_written < (uint64_t) LONG_MAX)
			fseek(wp->file, wp->bytes_written, SEEK_SET);
	}

	return true;
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
	wp->c.magic = 0;

	if (wp->group)
		free(wp->group);
	if (wp->palette)
		free(wp->palette);
	if (wp->iccprofile)
		free(wp->iccprofile);
	if (wp->ih)
		free(wp->ih);
	if (wp->fh)
		free(wp->fh);
	if (wp->c.log)
		logfree(wp->c.log);

	free(wp);
}
