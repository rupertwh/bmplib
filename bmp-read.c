/* bmplib - bmp-read.c
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
#include <stdbool.h>
#include <stdarg.h>

#define BMPLIB_LIB

#include "config.h"
#include "bmplib.h"
#include "logging.h"
#include "bmp-common.h"
#include "bmp-read-icons.h"
#include "bmp-read.h"


const char* s_compression_name(int compression);



/*****************************************************************************
 * 	bmpread_new
 *****************************************************************************/

API BMPHANDLE bmpread_new(FILE *file)
{
	BMPREAD rp = NULL;

	if (!(rp = malloc(sizeof *rp))) {
		goto abort;
	}

	memset(rp, 0, sizeof *rp);
	rp->c.magic = HMAGIC_READ;
	rp->undefined_mode = BMP_UNDEFINED_TO_ALPHA;
	rp->orientation    = BMP_ORIENT_BOTTOMUP;
	rp->conv64         = BMP_CONV64_SRGB;
	rp->result_format  = BMP_FORMAT_INT;
	rp->read_state     = RS_INIT;

	if (!(rp->c.log = logcreate()))
		goto abort;

	if (!file)
		goto abort;

	rp->file = file;

	if (!(rp->fh = malloc(sizeof *rp->fh)))
		goto abort;
	memset(rp->fh, 0, sizeof *rp->fh);

	if (!(rp->ih = malloc(sizeof *rp->ih)))
		goto abort;
	memset(rp->ih, 0, sizeof *rp->ih);

	rp->insanity_limit = INSANITY_LIMIT << 20;

	return (BMPHANDLE)(void*)rp;

abort:
	if (rp)
		br_free(rp);
	return NULL;
}



/*****************************************************************************
 * 	bmpread_load_info
 *****************************************************************************/
static bool s_read_file_header(BMPREAD_R rp);
static bool s_read_info_header(BMPREAD_R rp);
static bool s_is_bmptype_supported(BMPREAD_R rp);
static struct Palette* s_read_palette(BMPREAD_R rp);
static bool s_read_colormasks(BMPREAD_R rp);

API BMPRESULT bmpread_load_info(BMPHANDLE h)
{
	BMPREAD rp;

	if (!(rp = cm_read_handle(h)))
		return BMP_RESULT_ERROR;

	if (rp->read_state >= RS_HEADER_OK)
		return rp->getinfo_return;

	if (!s_read_file_header(rp))
		goto abort;

	long     pos;
	unsigned type = rp->fh->type;

	switch (rp->fh->type) {
	case BMPFILE_BM:
		/* ok */
		break;

	case BMPFILE_CI:
	case BMPFILE_CP:
	case BMPFILE_IC:
	case BMPFILE_PT:
		if (rp->read_state != RS_EXPECT_ICON_MASK) {
			pos = icon_load_masks(rp);
			if (pos < 0)
				goto abort;

			/* re-read the file header, because for color icons/pointers
			 * we started with the monochrome headers, and icon_load_masks()
			 * returned the pos of the actual color headers.
			 */
			rp->bytes_read = 0;
			if (fseek(rp->file, pos, SEEK_SET)) {
				logsyserr(rp->c.log, "Setting file position");
				goto abort;
			}

			if (!s_read_file_header(rp))
				goto abort;

			if (rp->fh->type != type) {
				logerr(rp->c.log, "Filetype mismatch: have 0x%04x, expected 0x%04x",
				                                   (unsigned)rp->fh->type, type);
				goto abort;
			}

			rp->is_icon = true;
			if (type == BMPFILE_CI || type == BMPFILE_CP)
				rp->icon_is_mono = false;
			else
				rp->icon_is_mono = true;

			rp->undefined_mode = BMP_UNDEFINED_LEAVE;
		}

		/* otherwise, state is RS_EXPECT_ICON_MASK, which means that
		 * icon_load_masks() is reading the AND/XOR masks (under a
		 * separate BMPHANDLE). We don't have to do anything special,
		 * just treat it as a normal BMP image, not as an icon/pointer
		 */

		break;

	case BMPFILE_BA:
		if (rp->is_arrayimg) {
			logerr(rp->c.log, "Invalid nested bitmap array");
			goto abort;
		}
		if (!icon_read_array(rp)) {
			logerr(rp->c.log, "Failed to read icon array index");
			goto abort;
		}

		rp->read_state     = RS_ARRAY;
		rp->getinfo_return = BMP_RESULT_ARRAY;
		return rp->getinfo_return;

	default:
		logerr(rp->c.log, "Unkown BMP type 0x%04x\n", (unsigned int) rp->fh->type);
		rp->lasterr = BMP_ERR_UNSUPPORTED;
		goto abort;
	}

#if ( LONG_MAX <= 0x7fffffffL )
	if (rp->fh->offbits > (unsigned long)LONG_MAX) {
		logerr(rp->c.log, "Invalid offset to image data: %lu", (unsigned long)rp->fh->offbits);
		goto abort;
	}
#endif

	if (!s_read_info_header(rp))
		goto abort;

	rp->width  = (int) rp->ih->width;

	/* negative height flips the image vertically */
	if (rp->ih->height < 0) {
		if (rp->is_icon) {
			logerr(rp->c.log, "Top-down orientation incompatible with icons/pointers");
			rp->lasterr = BMP_ERR_HEADER;
			goto abort;
		}
		if (rp->ih->height == INT_MIN) {
			logerr(rp->c.log, "Unsupported image height %ld\n", (long) rp->ih->height);
			rp->lasterr = BMP_ERR_UNSUPPORTED;
			goto abort;
		}
		rp->orientation = BMP_ORIENT_TOPDOWN;
		rp->height = -rp->ih->height;
	} else {
		rp->height = rp->ih->height;
	}

	if (rp->is_icon && rp->icon_is_mono)
		rp->height /= 2;

	if (rp->ih->compression == BI_RLE4 ||
	    rp->ih->compression == BI_RLE8 ||
	    rp->ih->compression == BI_OS2_RLE24) {
		rp->rle = true;
	}

	if (rp->ih->compression == BI_JPEG || rp->ih->compression == BI_PNG) {
		if (!cm_gobble_up(rp, rp->fh->offbits - rp->bytes_read)) {
			logerr(rp->c.log, "while seeking to start of jpeg/png data");
			goto abort;
		}
		if (rp->ih->compression == BI_JPEG) {
			rp->jpeg = true;
			rp->getinfo_return = BMP_RESULT_JPEG;
			logerr(rp->c.log, "embedded JPEG data");
			rp->lasterr = BMP_ERR_JPEG;
			return BMP_RESULT_JPEG;
		} else {
			rp->png = true;
			rp->getinfo_return = BMP_RESULT_PNG;
			logerr(rp->c.log, "embedded PNG data");
			rp->lasterr = BMP_ERR_PNG;
			return BMP_RESULT_PNG;
		}
	}

	if (!s_is_bmptype_supported(rp))
		goto abort;

	rp->result_channels = 3;
	if (rp->ih->bitcount <= 8) { /* indexed */
		if (!(rp->palette = s_read_palette(rp)))
			goto abort;
	} else if (!rp->rle) {  /* RGB  */
		if (!s_read_colormasks(rp))
			goto abort;

		if (rp->cmask.mask.alpha)
			rp->result_channels = 4;
	}

	/* add alpha channel for undefined pixels in RLE bitmaps */
	if (rp->rle) {
		rp->result_channels = (rp->undefined_mode == BMP_UNDEFINED_TO_ALPHA) ? 4 : 3;
	}
	if (rp->is_icon)
		rp->result_channels = 4;

	if (!br_set_resultbits(rp))
		goto abort;

	if (rp->insanity_limit && rp->result_size > rp->insanity_limit) {
		logerr(rp->c.log, "file is insanely large");
		rp->lasterr = BMP_ERR_INSANE;
		rp->getinfo_return = BMP_RESULT_INSANE;
	} else {
		rp->getinfo_return = BMP_RESULT_OK;
	}
	rp->read_state = RS_HEADER_OK;
	return rp->getinfo_return;

abort:
	rp->read_state = RS_FATAL;
	rp->getinfo_return = BMP_RESULT_ERROR;
	return BMP_RESULT_ERROR;
}


/*****************************************************************************
 * 	bmpread_image_type
 *****************************************************************************/

API BMPIMAGETYPE bmpread_image_type(BMPHANDLE h)
{
	BMPREAD rp;

	if (!(rp = cm_read_handle(h)))
		return BMP_IMAGETYPE_NONE;

	if (rp->read_state < RS_HEADER_OK) {
		logerr(rp->c.log, "Must load info, first.");
		return BMP_IMAGETYPE_NONE;
	}

	switch (rp->fh->type) {
	case BMPFILE_BM: return BMP_IMAGETYPE_BM;
	case BMPFILE_BA: return BMP_IMAGETYPE_BA;
	case BMPFILE_IC: return BMP_IMAGETYPE_IC;
	case BMPFILE_PT: return BMP_IMAGETYPE_PT;
	case BMPFILE_CI: return BMP_IMAGETYPE_CI;
	case BMPFILE_CP: return BMP_IMAGETYPE_CP;
	default:
		break;
	}
	return BMP_IMAGETYPE_NONE;
}


/*****************************************************************************
 * 	bmpread_set_64bit_conv
 *****************************************************************************/

API BMPRESULT bmpread_set_64bit_conv(BMPHANDLE h, enum Bmpconv64 conv)
{
	BMPREAD rp;

	if (!(rp = cm_read_handle(h)))
		return BMP_RESULT_ERROR;

	if (rp->read_state >= RS_LOAD_STARTED) {
		logerr(rp->c.log, "Too late to set 64bit conversion");
		return BMP_RESULT_ERROR;
	}

	switch (conv) {
	case BMP_CONV64_SRGB:
        case BMP_CONV64_LINEAR:
        	rp->conv64 = conv;
        	rp->conv64_explicit = true;
        	break;

        case BMP_CONV64_NONE:
        	if (rp->result_format_explicit && rp->result_format != BMP_FORMAT_S2_13) {
        		logerr(rp->c.log, "64-bit conversion %s imcompatible with chosen number format %s.\n",
        		                cm_conv64_name(conv), cm_format_name(rp->result_format));
        		rp->lasterr = BMP_ERR_CONV64;
        		return BMP_RESULT_ERROR;
        	} else {
        		rp->result_format = BMP_FORMAT_S2_13;
        		rp->result_format_explicit = true;
        	}
        	rp->conv64 = BMP_CONV64_LINEAR;
        	rp->conv64_explicit = true;
        	break;
        default:
        	logerr(rp->c.log, "Unknown 64-bit conversion %s (%d)", cm_conv64_name(conv), (int) conv);
        	rp->lasterr = BMP_ERR_CONV64;
        	return BMP_RESULT_ERROR;
	}
	return BMP_RESULT_OK;
}



/*****************************************************************************
 * 	bmpread_is_64bit
 *****************************************************************************/

API int bmpread_is_64bit(BMPHANDLE h)
{
	BMPREAD rp;

	if (!(rp = cm_read_handle(h)))
		return 0;

	if (rp->read_state < RS_HEADER_OK || rp->read_state >= RS_ARRAY)
		return 0;

	if (rp->ih->bitcount == 64)
		return 1;
	return 0;
}



/*****************************************************************************
 * 	bmpread_iccprofile_size
 *****************************************************************************/

API size_t bmpread_iccprofile_size(BMPHANDLE h)
{
	BMPREAD rp;

	if (!(rp = cm_read_handle(h)))
		return 0;

	if (rp->read_state < RS_HEADER_OK || rp->read_state >= RS_ARRAY)
 		return 0;

	if (rp->ih->cstype == PROFILE_EMBEDDED && rp->ih->profilesize <= MAX_ICCPROFILE_SIZE) {
		rp->iccprofile_size_queried = true;
		return (size_t)rp->ih->profilesize;
	}

	return 0;
}

/********************************************************
 * 	bmpread_load_iccprofile
 *******************************************************/

API BMPRESULT bmpread_load_iccprofile(BMPHANDLE h, unsigned char **profile)
{
	BMPREAD rp;
	size_t  memsize;
	long    pos;
	bool    we_allocated   = false;
	bool    file_messed_up = false;

	if (!(rp = cm_read_handle(h)))
		goto abort;

	if (rp->read_state < RS_HEADER_OK || rp->read_state >= RS_ARRAY) {
		logerr(rp->c.log, "Must load info before loading ICC profile");
		goto abort;
	}

	if (!rp->iccprofile_size_queried) {
		logerr(rp->c.log, "Must query profile size before loading ICC profile");
		goto abort;
	}

	if (rp->ih->cstype != PROFILE_EMBEDDED) {
		logerr(rp->c.log, "Image has no ICC profile");
		goto abort;
	}

	if (rp->ih->profilesize > MAX_ICCPROFILE_SIZE) {
		logerr(rp->c.log, "ICC profile is too large (%lu). Max is %lu",
		                  (unsigned long) rp->ih->profilesize,
		                  (unsigned long) MAX_ICCPROFILE_SIZE);
		goto abort;
	}

	if (!profile) {
		logerr(rp->c.log, "profile is NULL");
		goto abort;
	}

	memsize = rp->ih->profilesize;
	if (!*profile) {
		if (!(*profile = malloc(memsize))) {
			logsyserr(rp->c.log, "allocating ICC profile");
			goto abort;
		}
		we_allocated = true;
	}
	memset(*profile, 0, memsize);

	if (-1 == (pos = ftell(rp->file))) {
		logsyserr(rp->c.log, "reading current file position");
		goto abort;
	}

	if (fseek(rp->file, rp->ih->profiledata + 14, SEEK_SET)) {
		logsyserr(rp->c.log, "seeking ICC profile in file");
		goto abort;
	}

	/* Any failure from here on out cannot be reasonably recovered from, as
	 * the file position will be messed up! */

	file_messed_up = true;

	if (memsize != fread(*profile, 1, memsize, rp->file)) {
		if (feof(rp->file))
			logerr(rp->c.log, "EOF while reading ICC profile");
		else
			logsyserr(rp->c.log, "reading ICC profile");
		goto abort;
	}

	if (fseek(rp->file, pos, SEEK_SET)) {
		logsyserr(rp->c.log, "failed to reset file position after reading ICC profile");
		goto abort;
	}

	return BMP_RESULT_OK;

abort:
	if (profile && *profile && we_allocated) {
		free(*profile);
		*profile = NULL;
	}
	if (file_messed_up)
		rp->read_state = RS_FATAL;

	return BMP_RESULT_ERROR;
}


/*****************************************************************************
 * 	bmpread_dimensions
 *****************************************************************************/

API BMPRESULT bmpread_dimensions(BMPHANDLE h, int* restrict width,
                                              int* restrict height,
                                              int* restrict channels,
                                              int* restrict bitsperchannel,
                                   enum BmpOrient* restrict orientation)
{
	BMPREAD rp;

	if (!(rp = cm_read_handle(h)))
		return BMP_RESULT_ERROR;

	if (rp->read_state < RS_HEADER_OK)
		bmpread_load_info((BMPHANDLE)(void*)rp);

	if (rp->read_state < RS_HEADER_OK || rp->read_state >= RS_ARRAY)
		return BMP_RESULT_ERROR;

	if (width) {
		*width = rp->width;
		rp->dim_queried_width = true;
	}
	if (height) {
		*height = rp->height;
		rp->dim_queried_height = true;
	}
	if (channels) {
		*channels = rp->result_channels;
		rp->dim_queried_channels = true;
	}
	if (bitsperchannel) {
		*bitsperchannel = rp->result_bitsperchannel;
		rp->dim_queried_bitsperchannel = true;
	}
	if (orientation) {
		*orientation = rp->orientation;
	}

	if (rp->dim_queried_width    && rp->dim_queried_height &&
	    rp->dim_queried_channels && rp->dim_queried_bitsperchannel) {
		rp->read_state = MAX(RS_DIMENSIONS_QUERIED, rp->read_state);
	}

	return rp->getinfo_return;
}



/*****************************************************************************
 * 	br_set_number_format
 *****************************************************************************/

BMPRESULT br_set_number_format(BMPREAD_R rp, enum BmpFormat format)
{

	if (rp->result_format == format) {
		rp->result_format_explicit = true;
		return BMP_RESULT_OK;
	}

	if (rp->read_state >= RS_ARRAY)
		return BMP_RESULT_ERROR;

	switch (format) {
	case BMP_FORMAT_INT:
		/* always ok */
		break;

	case BMP_FORMAT_FLOAT:
	case BMP_FORMAT_S2_13:
		if (rp->result_indexed) {
			logerr(rp->c.log, "Cannot load color index as float or s2.13");
			rp->lasterr = BMP_ERR_FORMAT;
			return BMP_RESULT_ERROR;
		}
		if (rp->is_icon) {
			logerr(rp->c.log, "Cannot load icons/pointers as float or s2.13");
			rp->lasterr = BMP_ERR_FORMAT;
			return BMP_RESULT_ERROR;
		}
		break;

	default:
		logerr(rp->c.log, "Invalid number format (%d) specified", (int) format);
		rp->lasterr = BMP_ERR_FORMAT;
		return BMP_RESULT_ERROR;
	}

	rp->result_format = format;
	rp->result_format_explicit = true;

	if (!br_set_resultbits(rp)) {
		rp->read_state = RS_FATAL;
		return BMP_RESULT_ERROR;
	}
	return BMP_RESULT_OK;

}


/*****************************************************************************
 * 	bmpread_* getters for single dimension values
 *****************************************************************************/

enum Dimint {
	DIM_WIDTH = 1,
	DIM_HEIGHT,
	DIM_CHANNELS,
	DIM_BITS_PER_CHANNEL,
	DIM_ORIENTATION,
	DIM_XDPI,
	DIM_YDPI,
};

static int s_single_dim_val(BMPHANDLE h, enum Dimint dim);

API int bmpread_width(BMPHANDLE h)
{ return s_single_dim_val(h, DIM_WIDTH); }

API int bmpread_height(BMPHANDLE h)
{ return s_single_dim_val(h, DIM_HEIGHT); }

API int bmpread_channels(BMPHANDLE h)
{ return s_single_dim_val(h, DIM_CHANNELS); }

API int bmpread_bitsperchannel(BMPHANDLE h)
{ return s_single_dim_val(h, DIM_BITS_PER_CHANNEL); }

API int bmpread_bits_per_channel(BMPHANDLE h)
{ return s_single_dim_val(h, DIM_BITS_PER_CHANNEL); }

API int bmpread_topdown(BMPHANDLE h)
{ return s_single_dim_val(h, DIM_ORIENTATION); }

API enum BmpOrient bmpread_orientation(BMPHANDLE h)
{ return (enum BmpOrient) s_single_dim_val(h, DIM_ORIENTATION); }

API int bmpread_resolution_xdpi(BMPHANDLE h)
{ return s_single_dim_val(h, DIM_XDPI); }

API int bmpread_resolution_ydpi(BMPHANDLE h)
{ return s_single_dim_val(h, DIM_YDPI); }



/*****************************************************************************
 * 	s_single_dim_val
 *****************************************************************************/

static int s_single_dim_val(BMPHANDLE h, enum Dimint dim)
{
	BMPREAD rp;
	int     ret;

	if (!(rp = cm_read_handle(h)))
		return 0;

	if (rp->read_state < RS_HEADER_OK || rp->read_state >= RS_ARRAY)
		return 0;

	switch (dim) {
	case DIM_WIDTH:
		rp->dim_queried_width = true;
		ret = rp->width;
		break;
	case DIM_HEIGHT:
		rp->dim_queried_height = true;
		ret = rp->height;
		break;
	case DIM_CHANNELS:
		rp->dim_queried_channels = true;
		ret = rp->result_channels;
		break;
	case DIM_BITS_PER_CHANNEL:
		rp->dim_queried_bitsperchannel = true;
		ret = rp->result_bitsperchannel;
		break;
	case DIM_ORIENTATION:
		ret = (int) rp->orientation;
		break;
	case DIM_XDPI:
		ret = (int) (rp->ih->xpelspermeter / (100.0 / 2.54) + 0.5);
		break;
	case DIM_YDPI:
		ret = (int) (rp->ih->ypelspermeter / (100.0 / 2.54) + 0.5);
		break;
	default:
		return 0;
	}
	if (rp->dim_queried_width && rp->dim_queried_height &&
	    rp->dim_queried_channels && rp->dim_queried_bitsperchannel) {
		rp->read_state = MAX(RS_DIMENSIONS_QUERIED, rp->read_state);
	}
	return ret;
}



/*****************************************************************************
 * 	bmpread_buffersize
 *****************************************************************************/

API size_t bmpread_buffersize(BMPHANDLE h)
{
	BMPREAD rp;

	if (!(rp = cm_read_handle(h)))
		return 0;

	if (rp->read_state < RS_HEADER_OK || rp->read_state >= RS_ARRAY)
		return 0;

	rp->read_state = MAX(RS_DIMENSIONS_QUERIED, rp->read_state);
	return rp->result_size;

}



/*****************************************************************************
 * 	bmpread_set_insanity_limit
 *****************************************************************************/

API void bmpread_set_insanity_limit(BMPHANDLE h, size_t limit)
{
	BMPREAD rp;

	if (!(rp = cm_read_handle(h)))
		return;

	rp->insanity_limit = limit;

	if (rp->read_state < RS_HEADER_OK)
		return;

	if (rp->getinfo_return == BMP_RESULT_INSANE) {
		if (limit == 0 || limit >= rp->result_size)
			rp->getinfo_return = BMP_RESULT_OK;
	} else if (rp->getinfo_return == BMP_RESULT_OK) {
		if (limit > 0 && rp->result_size > limit)
			rp->getinfo_return = BMP_RESULT_INSANE;
	}
}



/*****************************************************************************
 * 	bmpread_set_undefined
 *****************************************************************************/
API void bmpread_set_undefined_to_alpha(BMPHANDLE h, int mode)
{ bmpread_set_undefined(h, (enum BmpUndefined) mode); }

API void bmpread_set_undefined(BMPHANDLE h, enum BmpUndefined mode)
{
	BMPREAD rp;

	if (!(rp = cm_read_handle(h)))
		return;

	if (rp->is_icon && mode != BMP_UNDEFINED_LEAVE) {
		logerr(rp->c.log, "For icons/pointers, only BMP_UNDEFINED_LEAVE is valid.");
		rp->lasterr = BMP_ERR_UNDEFMODE;
		mode = BMP_UNDEFINED_LEAVE;
	}

	if (mode == rp->undefined_mode)
		return;

	if (mode != BMP_UNDEFINED_TO_ALPHA && mode != BMP_UNDEFINED_LEAVE) {
		logerr(rp->c.log, "Invalid undefined-mode selected");
		rp->lasterr = BMP_ERR_UNDEFMODE;
		return;
	}

	rp->undefined_mode = mode;

	/* we are changing the setting after dimensions have */
	/* been established. Only relevant for RLE-encoding  */

	if (!rp->rle)
		return;

	rp->result_channels = (mode == BMP_UNDEFINED_TO_ALPHA) ?  4 :  3;

	rp->read_state = MIN(RS_HEADER_OK, rp->read_state);
	rp->dim_queried_channels = false;

	if (!br_set_resultbits(rp)) {
		rp->lasterr = BMP_ERR_DIMENSIONS;
		rp->read_state = RS_FATAL;
	}
}



/*****************************************************************************
 * 	br_free
 *****************************************************************************/

void br_free(BMPREAD rp)
{
	rp->c.magic = 0;

	if (rp->is_arrayimg)
		return;

	if (rp->arrayimgs) {
		for (int i = 0; i < rp->narrayimgs; i++) {
			((BMPREAD)rp->arrayimgs[i].handle)->is_arrayimg = false;
			br_free((BMPREAD)rp->arrayimgs[i].handle);
		}
		free(rp->arrayimgs);
	}
	if (rp->icon_mono_and)
		free(rp->icon_mono_and);
	if (rp->icon_mono_xor)
		free(rp->icon_mono_xor);
	if (rp->palette)
		free(rp->palette);
	if (rp->ih)
		free(rp->ih);
	if (rp->fh)
		free(rp->fh);
	if (rp->c.log)
		logfree(rp->c.log);
	free(rp);
}



/*****************************************************************************
 * 	s_is_bmptype_supported
 *****************************************************************************/
static bool s_is_bmptype_supported_rgb(BMPREAD_R rp);
static bool s_is_bmptype_supported_indexed(BMPREAD_R rp);

static bool s_is_bmptype_supported(BMPREAD_R rp)
{
	if (rp->ih->planes != 1) {
		logerr(rp->c.log, "Unsupported number of planes (%d). Must be 1.", (int) rp->ih->planes);
		rp->lasterr = BMP_ERR_UNSUPPORTED;
		return false;
	}

	if (rp->is_icon) {
		if (rp->ih->compression != BI_RGB &&
		    rp->ih->compression != BI_RLE4 &&
		    rp->ih->compression != BI_RLE8 &&
		    rp->ih->compression != BI_OS2_RLE24) {
			logerr(rp->c.log, "Unsupported compression %s for icon/pointer",
			                                         s_compression_name(rp->ih->compression));
			rp->lasterr = BMP_ERR_UNSUPPORTED;
			return false;
		}
		if (rp->ih->bitcount > 32) {
			logerr(rp->c.log, "Unsupported bitcount %d for icon/pointer",
			                                         (int) rp->ih->bitcount);
			rp->lasterr = BMP_ERR_UNSUPPORTED;
			return false;
		}
		if (rp->ih->version > BMPINFO_OS22) {
			logerr(rp->c.log, "Unsupported header version %s for icon/pointer",
			                                         cm_infoheader_name(rp->ih->version));
			rp->lasterr = BMP_ERR_UNSUPPORTED;
			return false;
		}
		if (rp->result_format != BMP_FORMAT_INT) {
			logerr(rp->c.log, "Chosen number format %s is incompatible with icon/pointer",
			                                         cm_format_name(rp->result_format));
			rp->lasterr = BMP_ERR_UNSUPPORTED;
			return false;
		}
	}
	if (rp->ih->bitcount <= 8)
		return s_is_bmptype_supported_indexed(rp);
	else
		return s_is_bmptype_supported_rgb(rp);

	return true;
}



/*****************************************************************************
 * 	s_is_bmptype_supported_rgb
 *****************************************************************************/

static bool s_is_bmptype_supported_rgb(BMPREAD_R rp)
{
	switch (rp->ih->bitcount) {
	case 16:
	case 24:
	case 32:
	case 64:
		/*  ok */
		break;
	default:
		logerr(rp->c.log, "Invalid bitcount %d for RGB image", (int) rp->ih->bitcount);
		rp->lasterr = BMP_ERR_HEADER;
		return false;
	}

	switch (rp->ih->compression) {
	case BI_RGB:
		/* ok */
		break;

	case BI_BITFIELDS:
	case BI_ALPHABITFIELDS:
		if (rp->ih->bitcount == 64) {
			logerr(rp->c.log, "Invalid bitcount %d for BITFIELDS", (int) rp->ih->bitcount);
			rp->lasterr = BMP_ERR_HEADER;
			return false;
		}
		break;

	case BI_OS2_RLE24:
		if (rp->ih->bitcount != 24) {
			logerr(rp->c.log, "Invalid bitcount %d for RLE24 compression", (int) rp->ih->bitcount);
			rp->lasterr = BMP_ERR_HEADER;
			return false;
		}
		break;

	default:
		logerr(rp->c.log, "Unsupported compression %s for RGB image",
		                 s_compression_name(rp->ih->compression));
		rp->lasterr = BMP_ERR_UNSUPPORTED;
		return false;
	}

	return true;
}



/*****************************************************************************
 * 	s_is_bmptype_supported_indexed
 *****************************************************************************/

static bool s_is_bmptype_supported_indexed(BMPREAD_R rp)
{
	switch (rp->ih->bitcount) {
	case 1:
	case 2:
	case 4:
	case 8:
		/*  ok */
		break;

	default:
		logerr(rp->c.log, "Invalid bitcount %d for indexed image",
		                               (int) rp->ih->bitcount);
		rp->lasterr = BMP_ERR_HEADER;
		return false;
	}

	switch (rp->ih->compression) {
	case BI_RGB:
	case BI_RLE4:
	case BI_RLE8:
	case BI_OS2_HUFFMAN:
		if ( (rp->ih->compression == BI_RLE4 && rp->ih->bitcount != 4) ||
		     (rp->ih->compression == BI_RLE8 && rp->ih->bitcount != 8) ||
		     (rp->ih->compression == BI_OS2_HUFFMAN && rp->ih->bitcount != 1)) {
			logerr(rp->c.log, "Unsupported compression %s for %d-bit data",
			                  s_compression_name(rp->ih->compression),
			                  (int) rp->ih->bitcount);
			rp->lasterr = BMP_ERR_UNSUPPORTED;
			return false;
		}
		/*  ok */
		break;

	default:
		logerr(rp->c.log, "Unsupported compression %s for indexed image",
		                   s_compression_name(rp->ih->compression));
		rp->lasterr = BMP_ERR_UNSUPPORTED;
		return false;
	}

	return true;
}



/*****************************************************************************
 * 	s_read_palette
 *****************************************************************************/

static struct Palette* s_read_palette(BMPREAD_R rp)
{
	int             i, r,g,b;
	struct Palette *palette;
	size_t          memsize;
	int             bytes_per_entry;
	int             colors_in_file;
	int             max_colors_in_file;
	int             colors_full_palette;
	int             colors_ignore = 0;


	if (rp->ih->clrused > INT_MAX || rp->ih->clrimportant > rp->ih->clrused) {
		logerr(rp->c.log, "Unreasonable color numbers for palette (%lu/%lu)",
					  (unsigned long) rp->ih->clrused,
					  (unsigned long) rp->ih->clrimportant);
		rp->lasterr = BMP_ERR_INVALID;
		return NULL;
	}
	if (rp->fh->offbits - rp->bytes_read > INT_MAX) {
		logerr(rp->c.log, "gap to pixeldata too big (%lu)",
				(unsigned long) rp->fh->offbits - rp->bytes_read);
		rp->lasterr = BMP_ERR_INVALID;
		return NULL;
	}
	if (rp->fh->offbits < rp->bytes_read) {
		logerr(rp->c.log, "Invalid offset to pixel data");
		rp->lasterr = BMP_ERR_INVALID;
		return NULL;
	}

	bytes_per_entry = rp->ih->version == BMPINFO_CORE_OS21 ? 3 : 4;

	max_colors_in_file = (rp->fh->offbits - rp->bytes_read) / bytes_per_entry;

	colors_full_palette = 1 << rp->ih->bitcount;

	if (0 == (colors_in_file = rp->ih->clrused)) {
		colors_in_file = MIN(colors_full_palette, max_colors_in_file);
	} else if (colors_in_file > max_colors_in_file) {
		logerr(rp->c.log, "given palette size (%d) too large for available data (%d)",
			        colors_in_file, max_colors_in_file);
		rp->lasterr = BMP_ERR_INVALID;
		return NULL;
	}

	if (colors_in_file > colors_full_palette)
		colors_ignore = colors_in_file - colors_full_palette;

	memsize = sizeof *palette +
	                 (colors_in_file - colors_ignore) * sizeof palette->color[0];
	if (!(palette = malloc(memsize))) {
		logsyserr(rp->c.log, "Allocating mem for palette");
		rp->lasterr = BMP_ERR_MEMORY;
		return NULL;
	}

	memset(palette, 0, memsize);
	palette->numcolors = colors_in_file - colors_ignore;
	for (i = 0; i < palette->numcolors; i++) {
		if (EOF == (b = getc(rp->file)) ||
		    EOF == (g = getc(rp->file)) ||
		    EOF == (r = getc(rp->file)) ||
		    ((bytes_per_entry == 4) && (EOF == getc(rp->file))) ) {
			if (feof(rp->file)) {
				logerr(rp->c.log, "file ended reading palette entries");
				rp->lasterr = BMP_ERR_TRUNCATED;
			} else {
				logsyserr(rp->c.log, "reading palette entries");
				rp->lasterr = BMP_ERR_FILEIO;
			}
			free (palette);
			return NULL;
		}
		rp->bytes_read += bytes_per_entry;
		palette->color[i].red   = r;
		palette->color[i].green = g;
		palette->color[i].blue  = b;
	}

	for (i = 0; i < colors_ignore; i++) {
		if (!cm_gobble_up(rp, bytes_per_entry)) {
			logerr(rp->c.log, "reading superfluous palette entries");
			free(palette);
			return NULL;
		}
		rp->bytes_read += bytes_per_entry;
	}

	return palette;
}



/*****************************************************************************
 * 	br_set_resultbits
 *****************************************************************************/
static bool s_check_dimensions(BMPREAD_R rp);

bool br_set_resultbits(BMPREAD_R rp)
{
	int newbits, max_bits = 0, i;

	if (!rp->ih->bitcount)
		return true;

	switch (rp->result_format) {
	case BMP_FORMAT_FLOAT:
		if (rp->result_indexed) {
			logerr(rp->c.log, "Float is invalid number format for indexed image\n");
			rp->lasterr = BMP_ERR_FORMAT;
			return false;
		}
		newbits = 8 * sizeof (float);
		break;

	case BMP_FORMAT_S2_13:
		if (rp->result_indexed) {
			logerr(rp->c.log, "s2.13 is invalid number format for indexed image\n");
			rp->lasterr = BMP_ERR_FORMAT;
			return false;
		}
		newbits = 16;
		break;

	case BMP_FORMAT_INT:
		if (rp->ih->bitcount <= 8 || rp->rle)
			newbits = 8;
		else { /* RGB */
			for (i = 0; i < 4; i++) {
				max_bits = MAX(max_bits, rp->cmask.bits.value[i]);
			}
			newbits = 8;
			while (newbits < max_bits && newbits < 32) {
				newbits *= 2;
			}
		}
		break;
	default:
		logerr(rp->c.log, "Invalid number format %d\n", rp->result_format);
		rp->lasterr = BMP_ERR_FORMAT;
		return false;

	}

	if (newbits != rp->result_bitsperchannel) {
		rp->dim_queried_bitsperchannel = false;
		rp->read_state = MIN(RS_HEADER_OK, rp->read_state);
	}
	rp->result_bitsperchannel = newbits;
	rp->result_bits_per_pixel = rp->result_bitsperchannel * rp->result_channels;
	rp->result_bytes_per_pixel = rp->result_bits_per_pixel / 8;

	if (!s_check_dimensions(rp))
		return false;

	rp->result_size = (size_t) rp->width * rp->height * rp->result_bytes_per_pixel;

	if (rp->read_state >= RS_HEADER_OK) {
		if (rp->insanity_limit && rp->result_size > rp->insanity_limit) {
		 	if (rp->getinfo_return == BMP_RESULT_OK) {
				logerr(rp->c.log, "file is insanely large");
				rp->lasterr = BMP_ERR_INSANE;
				rp->getinfo_return = BMP_RESULT_INSANE;
			}
		} else {
			if (rp->getinfo_return == BMP_RESULT_INSANE)
				rp->getinfo_return = BMP_RESULT_OK;
		}
	}
	return true;
}



/*****************************************************************************
 * 	s_check_dimensions
 *****************************************************************************/

static bool s_check_dimensions(BMPREAD_R rp)
{
	uint64_t npixels;
	size_t   maxpixels;

	npixels   = (uint64_t) rp->width * rp->height;
	maxpixels = SIZE_MAX / rp->result_bytes_per_pixel;

	if (npixels > maxpixels || rp->width  < 1 || rp->height < 1) {
		logerr(rp->c.log, "Invalid BMP dimensions (%dx%d)", rp->width, rp->height);
		rp->lasterr = BMP_ERR_DIMENSIONS;
		rp->read_state = RS_FATAL;
		return false;
	}
	return true;
}



/*****************************************************************************
 * 	s_read_colormasks
 *****************************************************************************/
static bool s_read_masks_from_bitfields(BMPREAD_R rp);
static bool s_create_implicit_colormasks(BMPREAD_R rp);
static inline int s_calc_bits_for_mask(unsigned long long mask);
static inline int s_calc_shift_for_mask(unsigned long long mask);

static bool s_read_colormasks(BMPREAD_R rp)
{
	int i, max_bits = 0, sum_bits = 0;

	switch (rp->ih->compression) {
	case BI_BITFIELDS:
	case BI_ALPHABITFIELDS:
		if (!s_read_masks_from_bitfields(rp))
			return false;
		break;

	case BI_RGB:
		if (!s_create_implicit_colormasks(rp))
			return false;
		break;

	default:
		logerr(rp->c.log, "Invalid compression (%s)",
		                s_compression_name(rp->ih->compression));
		rp->lasterr = BMP_ERR_INVALID;
		return false;
	}

	if (rp->cmask.mask.alpha) {
		rp->has_alpha = true;
		rp->result_channels = 4;
	} else {
		rp->has_alpha = false;
		rp->result_channels = 3;
	}

	for (i = 0; i < 4; i++) {
		max_bits = MAX(max_bits, rp->cmask.bits.value[i]);
		sum_bits += rp->cmask.bits.value[i];
	}
	if (max_bits > MIN(rp->ih->bitcount, 32) || sum_bits > rp->ih->bitcount) {
		logerr(rp->c.log, "Invalid mask bitcount (max=%d, sum=%d)",
		                                     max_bits, sum_bits);
		rp->lasterr = BMP_ERR_INVALID;
		return false;
	}
	if (!(rp->cmask.mask.red | rp->cmask.mask.green | rp->cmask.mask.blue)) {
		logerr(rp->c.log, "Empty color masks. Corrupt BMP?");
		rp->lasterr = BMP_ERR_INVALID;
		return false;
	}
	if (rp->cmask.mask.red  & rp->cmask.mask.green &
	    rp->cmask.mask.blue & rp->cmask.mask.alpha) {
		logerr(rp->c.log, "Overlapping color masks. Corrupt BMP?");
		rp->lasterr = BMP_ERR_INVALID;
		return false;
	}

	return true;
}



/*****************************************************************************
 * 	s_read_masks_from_bitfields
 *****************************************************************************/

static bool s_read_masks_from_bitfields(BMPREAD_R rp)
{
	uint32_t r,g,b,a;
	int      i;

	if (!(rp->ih->bitcount == 16 || rp->ih->bitcount == 32)) {
		logerr(rp->c.log, "Invalid bitcount (%d) for BI_BITFIELDS."
		                "Must be 16 or 32", (int) rp->ih->bitcount);
		rp->lasterr = BMP_ERR_INVALID;
		return false;
	}

	if (rp->ih->version < BMPINFO_V3_ADOBE1) {
		if (!(read_u32_le(rp->file, &r) &&
		      read_u32_le(rp->file, &g) &&
		      read_u32_le(rp->file, &b))) {
			if (feof(rp->file)) {
				logerr(rp->c.log, "File ended reading color masks");
				rp->lasterr = BMP_ERR_TRUNCATED;
			} else {
				logsyserr(rp->c.log, "Reading BMP color masks");
				rp->lasterr = BMP_ERR_FILEIO;
			}
			return false;
		}
		rp->bytes_read += 12;
		rp->cmask.mask.red   = r;
		rp->cmask.mask.green = g;
		rp->cmask.mask.blue  = b;
		if (rp->ih->compression == BI_ALPHABITFIELDS) {
			if (!read_u32_le(rp->file, &a)) {
				if (feof(rp->file)) {
					logerr(rp->c.log, "File ended reading color masks");
					rp->lasterr = BMP_ERR_TRUNCATED;
				} else {
					logsyserr(rp->c.log, "Reading BMP color masks");
					rp->lasterr = BMP_ERR_FILEIO;
				}
				return false;
			}
			rp->bytes_read += 4;
			rp->cmask.mask.alpha = a;
		}

	} else {
		rp->cmask.mask.red   = rp->ih->redmask;
		rp->cmask.mask.green = rp->ih->greenmask;
		rp->cmask.mask.blue  = rp->ih->bluemask;
		if (rp->ih->version >= BMPINFO_V3_ADOBE2)
			rp->cmask.mask.alpha = rp->ih->alphamask;
	}

	for (i = 0; i < (rp->cmask.mask.alpha ? 4 : 3); i++) {
		rp->cmask.bits.value[i]  = s_calc_bits_for_mask(rp->cmask.mask.value[i]);
		rp->cmask.shift.value[i] = s_calc_shift_for_mask(rp->cmask.mask.value[i]);
	}

	return true;
}



/*****************************************************************************
 * 	s_create_implicit_colormasks
 *****************************************************************************/

static bool s_create_implicit_colormasks(BMPREAD_R rp)
{
	int i, bitsperchannel;

	switch (rp->ih->bitcount) {
	case 16:
		bitsperchannel = 5;
		break;

	case 24:
	case 32:
		bitsperchannel = 8;
		break;

	case 64:
		bitsperchannel = 16;
		break;
	default:
		logerr(rp->c.log, "Invalid bitcount for BMP (%d)", (int) rp->ih->bitcount);
		rp->lasterr = BMP_ERR_INVALID;
		return false;
	}


	for (i = 0; i < 3; i++) {
		rp->cmask.shift.value[i] = (2-i) * bitsperchannel;
		rp->cmask.mask.value[i]  =
			   ((1ULL<<bitsperchannel)-1) << rp->cmask.shift.value[i];
		rp->cmask.bits.value[i]  = s_calc_bits_for_mask(rp->cmask.mask.value[i]);
	}

	if (rp->ih->bitcount == 64) {
		rp->cmask.shift.alpha = 3 * bitsperchannel;
		rp->cmask.mask.alpha  =
			   ((1ULL<<bitsperchannel)-1) << rp->cmask.shift.alpha;
		rp->cmask.bits.alpha  = s_calc_bits_for_mask(rp->cmask.mask.alpha);

	}

	return true;
}



/*****************************************************************************
 * 	s_calc_bits_for_mask
 *****************************************************************************/

static inline int s_calc_bits_for_mask(unsigned long long mask)
{
	int bits = 0;

	if (!mask)
		return 0;

	while (0 == (mask & 0x01))
		mask >>= 1;

	while (1 == (mask & 0x01)) {
		bits++;
		mask >>= 1;
	}

	return bits;
}



/*****************************************************************************
 * 	s_calc_shift_for_mask
 *****************************************************************************/

static inline int s_calc_shift_for_mask(unsigned long long mask)
{
	int shift = 0;

	if (!mask)
		return 0;

	while (0 == (mask & 0x01)) {
		shift++;
		mask >>= 1;
	}

	return shift;
}



/*****************************************************************************
 * 	s_read_file_header
 *****************************************************************************/
static bool s_read_file_header(BMPREAD_R rp)
{
	if (read_u16_le(rp->file, &rp->fh->type)      &&
	    read_u32_le(rp->file, &rp->fh->size)      &&
	    read_u16_le(rp->file, &rp->fh->reserved1) &&
	    read_u16_le(rp->file, &rp->fh->reserved2) &&
	    read_u32_le(rp->file, &rp->fh->offbits)    ) {

		rp->bytes_read += 14;
		return true;
	}

	if (feof(rp->file)) {
		logerr(rp->c.log, "unexpected end-of-file while reading "
				"file header");
		rp->lasterr = BMP_ERR_TRUNCATED;
	} else {
		logsyserr(rp->c.log, "error reading file header");
		rp->lasterr = BMP_ERR_FILEIO;
	}

	return false;
}



/*****************************************************************************
 * 	s_read_info_header
 *****************************************************************************/
static void s_detect_os2_header(BMPREAD_R rp);

static bool s_read_info_header(BMPREAD_R rp)
{
	int           skip, i, filepos;
	unsigned char buf[124];
	size_t        read_size;

	filepos = (int) rp->bytes_read;

	if (!read_u32_le(rp->file, &rp->ih->size))
		goto abort_file_err;
	rp->bytes_read += 4;

	if (rp->ih->size > INT_MAX) {
		logerr(rp->c.log, "Ridiculous info header size (%lu)", (unsigned long) rp->ih->size);
		return false;
	}

	switch (rp->ih->size) {
	case  12: rp->ih->version = BMPINFO_CORE_OS21; break;
	case  16:
	case  20:
	case  24:
	case  28:
	case  32:
	case  36:
	case  42:
	case  44:
	case  46:
	case  48:
	case  60:
	case  64: rp->ih->version = BMPINFO_OS22;      break;
	case  40: rp->ih->version = BMPINFO_V3;        break;
	case  52: rp->ih->version = BMPINFO_V3_ADOBE1; break;
	case  56: rp->ih->version = BMPINFO_V3_ADOBE2; break;
	case 108: rp->ih->version = BMPINFO_V4;        break;
	case 124: rp->ih->version = BMPINFO_V5;        break;
	default:
		if (rp->ih->size > 124)
			rp->ih->version = BMPINFO_FUTURE;
		else {
			logerr(rp->c.log, "Invalid info header size (%lu)",
			                   (unsigned long) rp->ih->size);
			rp->lasterr = BMP_ERR_HEADER;
			return false;
		}
		break;
	}

	memset(buf, 0, sizeof buf);
	read_size = MIN(sizeof buf - 4, rp->ih->size - 4);
	if (fread(buf + 4, 1, read_size, rp->file) != read_size)
		goto abort_file_err;

	rp->bytes_read += read_size;

	if (rp->ih->version == BMPINFO_CORE_OS21) {
		rp->ih->width    = u16_from_le(buf +  4);
		rp->ih->height   = u16_from_le(buf +  6);
		rp->ih->planes   = u16_from_le(buf +  8);
		rp->ih->bitcount = u16_from_le(buf + 10);
		goto header_done;
	}

	rp->ih->width    = s32_from_le(buf +  4);
	rp->ih->height   = s32_from_le(buf +  8);
	rp->ih->planes   = u16_from_le(buf + 12);
	rp->ih->bitcount = u16_from_le(buf + 14);

	if (rp->ih->size == 16) { /* 16-byte BMPINFO_OS22 */
		goto header_done;
	}

	rp->ih->compression   = u32_from_le(buf + 16);
	rp->ih->sizeimage     = u32_from_le(buf + 20);
	rp->ih->xpelspermeter = s32_from_le(buf + 24);
	rp->ih->ypelspermeter = s32_from_le(buf + 28);
	rp->ih->clrused       = u32_from_le(buf + 32);
	rp->ih->clrimportant  = u32_from_le(buf + 36);

	if (rp->ih->version == BMPINFO_OS22) {
		rp->ih->resolution     = u16_from_le(buf + 40);
		rp->ih->orientation    = u16_from_le(buf + 44);
		rp->ih->halftone_alg   = u16_from_le(buf + 46);
		rp->ih->halftone_parm1 = u32_from_le(buf + 48);
		rp->ih->halftone_parm2 = u32_from_le(buf + 52);
		rp->ih->color_encoding = u32_from_le(buf + 56);
		rp->ih->app_id         = u32_from_le(buf + 60);
		goto header_done;
	}

	/* BITMAPV4HEADER */
	rp->ih->redmask     = u32_from_le(buf +  40);
	rp->ih->greenmask   = u32_from_le(buf +  44);
	rp->ih->bluemask    = u32_from_le(buf +  48);
	rp->ih->alphamask   = u32_from_le(buf +  52);
	rp->ih->cstype      = u32_from_le(buf +  56);
	rp->ih->redX        = s32_from_le(buf +  60);
	rp->ih->redY        = s32_from_le(buf +  64);
	rp->ih->redZ        = s32_from_le(buf +  68);
	rp->ih->greenX      = s32_from_le(buf +  72);
	rp->ih->greenY      = s32_from_le(buf +  76);
	rp->ih->greenZ      = s32_from_le(buf +  80);
	rp->ih->blueX       = s32_from_le(buf +  84);
	rp->ih->blueY       = s32_from_le(buf +  88);
	rp->ih->blueZ       = s32_from_le(buf +  92);
	rp->ih->gammared    = u32_from_le(buf +  96);
	rp->ih->gammagreen  = u32_from_le(buf + 100);
	rp->ih->gammablue   = u32_from_le(buf + 104);

	/* BITMAPV5HEADER */
	rp->ih->intent      = u32_from_le(buf + 108);
	rp->ih->profiledata = u32_from_le(buf + 112);
	rp->ih->profilesize = u32_from_le(buf + 116);
	rp->ih->reserved    = u32_from_le(buf + 120);


header_done:
	/* read past bigger info headers: */
	skip = (int) rp->ih->size - ((int) rp->bytes_read - filepos);

	for (i = 0; i < skip; i++) {
		if (EOF == getc(rp->file))
			goto abort_file_err;
		rp->bytes_read++;
	}

	s_detect_os2_header(rp);

	return true;

abort_file_err:
	if (feof(rp->file)) {
		logerr(rp->c.log, "Unexpected end of file while reading BMP info header");
		rp->lasterr = BMP_ERR_TRUNCATED;
	} else {
		logsyserr(rp->c.log, "While reading BMP info header");
		rp->lasterr = BMP_ERR_FILEIO;
	}
	return false;

}



/*****************************************************************************
 * 	s_detect_os2_header
 *****************************************************************************/

static void s_detect_os2_header(BMPREAD_R rp)
{
	if (rp->ih->version == BMPINFO_V3) {
		/* might actually be a 40-byte OS/2 header */
		if (rp->fh->size == 54 ||
		    (rp->ih->compression == BI_OS2_HUFFMAN_DUP && rp->ih->bitcount == 1) ||
		    (rp->ih->compression == BI_OS2_RLE24_DUP && rp->ih->bitcount == 24)) {
			rp->ih->version = BMPINFO_OS22;
		} else if (rp->fh->type != BMPFILE_BM) {
			/* arrays, icons, and pointers are always OS/2 */
			rp->ih->version = BMPINFO_OS22;
		}
	}

	if (rp->ih->version <= BMPINFO_OS22) {
		if (rp->ih->compression == BI_OS2_HUFFMAN_DUP)
			rp->ih->compression = BI_OS2_HUFFMAN;
		else if (rp->ih->compression == BI_OS2_RLE24_DUP)
			rp->ih->compression = BI_OS2_RLE24;
	}
}



/*****************************************************************************
 * 	bmpread_info_*  int
 *****************************************************************************/

enum Infoint {
	INFO_INT_HEADER_VERSION,
	INFO_INT_HEADER_SIZE,
	INFO_INT_COMPRESSION,
	INFO_INT_BITCOUNT,
};

static int s_info_int(BMPHANDLE h, enum Infoint info);

API enum BmpInfoVer bmpread_info_header_version(BMPHANDLE h)
{ return (enum BmpInfoVer) s_info_int(h, INFO_INT_HEADER_VERSION); }
API int bmpread_info_header_size(BMPHANDLE h)
{ return s_info_int(h, INFO_INT_HEADER_SIZE); }
API int bmpread_info_compression(BMPHANDLE h)
{ return s_info_int(h, INFO_INT_COMPRESSION); }
API int bmpread_info_bitcount(BMPHANDLE h)
{ return s_info_int(h, INFO_INT_BITCOUNT); }


/*****************************************************************************
 * 	s_info_int
 *****************************************************************************/
static int s_info_int(BMPHANDLE h, enum Infoint info)
{
	BMPREAD rp;

	if (!(rp = cm_read_handle(h)))
		return 0;

	if (rp->read_state < RS_HEADER_OK || rp->read_state >= RS_ARRAY)
		return 0;

	switch (info) {
	case INFO_INT_HEADER_VERSION:
		return (int) rp->ih->version;
	case INFO_INT_HEADER_SIZE:
		return (int) rp->ih->size;
	case INFO_INT_COMPRESSION:
		return (int) rp->ih->compression;
	case INFO_INT_BITCOUNT:
		return (int) rp->ih->bitcount;
	}
	return 0;
}



/*****************************************************************************
 * 	bmpread_info_*  str
 *****************************************************************************/

enum Infostr {
	INFO_STR_HEADER_NAME,
	INFO_STR_COMPRESSION_NAME,
};

static const char* s_info_str(BMPHANDLE h, enum Infostr info);

API const char* bmpread_info_header_name(BMPHANDLE h)
{ return s_info_str(h, INFO_STR_HEADER_NAME); }
API const char* bmpread_info_compression_name(BMPHANDLE h)
{ return s_info_str(h, INFO_STR_COMPRESSION_NAME); }


/*****************************************************************************
 * 	s_info_str
 *****************************************************************************/
static const char* s_info_str(BMPHANDLE h, enum Infostr info)
{
	BMPREAD rp;

	if (!(rp = cm_read_handle(h)))
		return "";

	if (rp->read_state < RS_HEADER_OK || rp->read_state >= RS_FATAL)
		return "";

	switch (info) {
	case INFO_STR_HEADER_NAME:
		return cm_infoheader_name(rp->ih->version);
	case INFO_STR_COMPRESSION_NAME:
		return s_compression_name(rp->ih->compression);

	}
	return "";
}



/*****************************************************************************
 * 	bmpread_info_channel_bits
 *****************************************************************************/

API BMPRESULT bmpread_info_channel_bits(BMPHANDLE h, int *r, int *g, int *b, int *a)
{
	BMPREAD rp;

	if (!(rp = cm_read_handle(h)))
		return BMP_RESULT_ERROR;

	if (rp->read_state < RS_HEADER_OK || rp->read_state >= RS_ARRAY)
		return BMP_RESULT_ERROR;

	if (rp->ih->compression == BI_OS2_RLE24) {
		*r = *g = *b = 8;
		*a = 0;

	} else if (rp->ih->bitcount <= 8) {
		*r = *g = *b = *a = 0;

	} else {
		*r = rp->cmask.bits.red;
		*g = rp->cmask.bits.green;
		*b = rp->cmask.bits.blue;
		*a = rp->cmask.bits.alpha;
	}

	return BMP_RESULT_OK;
}



/*****************************************************************************
 * 	s_compression_name
 *****************************************************************************/

const char* s_compression_name(int compression)
{
	static char	buff[100];

	switch (compression) {
	case BI_RGB            : return "BI_RGB";
	case BI_RLE8           : return "BI_RLE8";
	case BI_RLE4           : return "BI_RLE4";
	case BI_OS2_HUFFMAN    : return "BI_OS2_HUFFMAN";
	case BI_OS2_RLE24      : return "BI_OS2_RLE24";
	case BI_BITFIELDS      : return "BI_BITFIELDS";
	case BI_JPEG           : return "BI_JPEG";
	case BI_PNG            : return "BI_PNG";
	case BI_ALPHABITFIELDS : return "BI_ALPHABITFIELDS";
	case BI_CMYK           : return "BI_CMYK";
	case BI_CMYKRLE8       : return "BI_CMYKRLE8";
	case BI_CMYKRLE4       : return "BI_CMYKRLE4";
	default:
		snprintf(buff, sizeof buff, "unknown (%d)", compression);
	}
	return buff;
}
