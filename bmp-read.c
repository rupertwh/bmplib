/* bmplib - bmp-read.c
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

#include "config.h"
#include "bmplib.h"
#include "logging.h"
#include "bmp-common.h"
#include "bmp-read.h"


const char* s_infoheader_name(int infoversion);
const char* s_compression_name(int compression);


/********************************************************
 * 	bmpread_new
 *******************************************************/

API BMPHANDLE bmpread_new(FILE *file)
{
	BMPREAD rp = NULL;

	if (!(rp = malloc(sizeof *rp))) {
		logerr(rp->log, "allocating bmpread struct");
		goto abort;
	}
	memset(rp, 0, sizeof *rp);
	rp->magic = HMAGIC_READ;
	rp->undefined_mode = BMP_UNDEFINED_TO_ALPHA;
	rp->orientation    = BMP_ORIENT_BOTTOMUP;
	rp->conv64         = BMP_CONV64_16BIT_SRGB;

	if (!(rp->log = logcreate()))
		goto abort;

	if (sizeof(int) < 4 || sizeof(unsigned int) < 4) {
		logerr(rp->log, "code doesn't work on %d-bit platforms!\n", (int) (8 * sizeof(int)));
		goto abort;
	}

	if (!file) {
		logerr(rp->log, "Must supply file handle");
		goto abort;
	}

	rp->file = file;

	if (!(rp->fh = malloc(sizeof *rp->fh))) {
		logerr(rp->log, "allocating bmp file header");
		goto abort;
	}
	memset(rp->fh, 0, sizeof *rp->fh);

	if (!(rp->ih = malloc(sizeof *rp->ih))) {
		logerr(rp->log, "allocating bmp info header");
		goto abort;
	}
	memset(rp->ih, 0, sizeof *rp->ih);

	rp->insanity_limit = INSANITY_LIMIT << 20;

	return (BMPHANDLE)(void*)rp;

abort:
	if (rp)
		br_free(rp);
	return NULL;
}



/********************************************************
 * 	bmpread_load_info
 *******************************************************/
static int s_read_file_header(BMPREAD_R rp);
static int s_read_info_header(BMPREAD_R rp);
static int s_is_bmptype_supported(BMPREAD_R rp);
static struct Palette* s_read_palette(BMPREAD_R rp);
static int s_read_colormasks(BMPREAD_R rp);
static int s_check_dimensions(BMPREAD_R rp);

API BMPRESULT bmpread_load_info(BMPHANDLE h)
{
	BMPREAD rp;

	if (!(h && cm_check_is_read_handle(h)))
		return BMP_RESULT_ERROR;
	rp = (BMPREAD)(void*)h;

	if (rp->getinfo_called)
		return rp->getinfo_return;
	rp->getinfo_called = TRUE;

	if (!s_read_file_header(rp))
		goto abort;

	switch (rp->fh->type) {
	case BMPFILE_BM:
		/* ok */
		break;

	case BMPFILE_CI:
	case BMPFILE_CP:
	case BMPFILE_IC:
	case BMPFILE_PT:
	case BMPFILE_BA:
		logerr(rp->log, "Bitmap array and icon/pointer files not supported");
		goto abort;

	default:
		logerr(rp->log, "Unkown BMP type 0x%04x\n", (unsigned int) rp->fh->type);
		goto abort;
	}

	if (!s_read_info_header(rp))
		goto abort;

	rp->width  = (int) rp->ih->width;
	rp->height = (int) rp->ih->height;

	/* negative height flips the image vertically */
	if (rp->height < 0) {
		rp->orientation = BMP_ORIENT_TOPDOWN;
		rp->height = -rp->height;
	}

	if (rp->ih->compression == BI_RLE4 ||
	    rp->ih->compression == BI_RLE8 ||
	    rp->ih->compression == BI_OS2_RLE24)
	    	rp->rle = TRUE;

	if (rp->ih->compression == BI_JPEG || rp->ih->compression == BI_PNG) {
		if (!cm_gobble_up(rp->file, rp->fh->offbits - rp->bytes_read, rp->log)) {
			logerr(rp->log, "while seeking to start of jpeg/png data");
			goto abort;
		}
		if (rp->ih->compression == BI_JPEG) {
			rp->jpeg = TRUE;
			rp->getinfo_return = BMP_RESULT_JPEG;
			logerr(rp->log, "embedded JPEG data");
			return BMP_RESULT_JPEG;
		}
		else {
			rp->png = TRUE;
			rp->getinfo_return = BMP_RESULT_PNG;
			logerr(rp->log, "embedded PNG data");
			return BMP_RESULT_PNG;
		}
	}

	if (!s_is_bmptype_supported(rp))
		goto abort;

	rp->result_channels = 3;
	if (rp->ih->bitcount <= 8) { /* indexed */
		if (!(rp->palette = s_read_palette(rp)))
			goto abort;
		rp->result_bits_per_pixel   = 24;
		rp->result_bytes_per_pixel  = 3;
		rp->result_bits_per_channel = 8;
	}
	else if (!rp->rle) {  /* RGB  */
		memset(&rp->colormask, 0, sizeof rp->colormask);
		if (!s_read_colormasks(rp))
			goto abort;
		/* result bitspp/bytespp/bitspc are set inside s_read_colormasks */

		if (rp->colormask.mask.alpha)
			rp->result_channels = 4;
	}

	/* add alpha channel for undefined pixels in RLE bitmaps */
	if (rp->rle) {
		rp->result_bits_per_pixel   = (rp->undefined_mode == BMP_UNDEFINED_TO_ALPHA) ? 32 : 24;
		rp->result_bytes_per_pixel  = (rp->undefined_mode == BMP_UNDEFINED_TO_ALPHA) ? 4 : 3;
		rp->result_bits_per_channel = 8;
		rp->result_channels         = (rp->undefined_mode == BMP_UNDEFINED_TO_ALPHA) ? 4 : 3;
	}

	if (!s_check_dimensions(rp))
		goto abort;

	rp->result_size = (size_t) rp->width *
                          (size_t) rp->height *
                          (size_t) rp->result_bytes_per_pixel;

	if (rp->insanity_limit &&
	    rp->result_size > rp->insanity_limit) {
		logerr(rp->log, "file is insanely large");
		rp->getinfo_return = BMP_RESULT_INSANE;
	}
	else {
		rp->getinfo_return = BMP_RESULT_OK;
	}

	return rp->getinfo_return;

abort:
	rp->getinfo_return = BMP_RESULT_ERROR;
	return BMP_RESULT_ERROR;
}



/********************************************************
 * 	bmpread_set_64bit_conv
 *******************************************************/

API BMPRESULT bmpread_set_64bit_conv(BMPHANDLE h, enum Bmpconv64 conv)
{
	BMPREAD rp;

	if (!(h && cm_check_is_read_handle(h)))
		return BMP_RESULT_ERROR;
	rp = (BMPREAD)(void*)h;

	switch (conv) {
	case BMP_CONV64_16BIT_SRGB:
        case BMP_CONV64_16BIT:
        case BMP_CONV64_NONE:
        	rp->conv64 = conv;
        	break;
        default:
        	logerr(rp->log, "Invalid 64-bit conversion (%d)", (int) conv);
        	return BMP_RESULT_ERROR;
	}
	return BMP_RESULT_OK;
}



/********************************************************
 * 	bmpread_is_64bit
 *******************************************************/

API int bmpread_is_64bit(BMPHANDLE h)
{
	BMPREAD rp;

	if (!(h && cm_check_is_read_handle(h)))
		return 0;
	rp = (BMPREAD)(void*)h;

	if (!rp->getinfo_called)
		bmpread_load_info((BMPHANDLE)(void*)rp);

	if (rp->getinfo_return != BMP_RESULT_OK && rp->getinfo_return != BMP_RESULT_INSANE) {
		return 0;
	}

	if (rp->ih->bitcount == 64)
		return 1;
	return 0;
}



/********************************************************
 * 	bmpread_dimensions
 *******************************************************/

API BMPRESULT bmpread_dimensions(BMPHANDLE h, int* restrict width,
                                              int* restrict height,
                                              int* restrict channels,
                                              int* restrict bitsperchannel,
                                   enum BmpOrient* restrict orientation)
{
	BMPREAD rp;

	if (!(h && cm_check_is_read_handle(h)))
		return BMP_RESULT_ERROR;
	rp = (BMPREAD)(void*)h;

	if (!rp->getinfo_called)
		bmpread_load_info((BMPHANDLE)(void*)rp);

	if (rp->getinfo_return != BMP_RESULT_OK && rp->getinfo_return != BMP_RESULT_INSANE) {
		return rp->getinfo_return;
	}

	if (width)          *width          = rp->width;
	if (height)         *height         = rp->height;
	if (channels)       *channels       = rp->result_channels;
	if (bitsperchannel) *bitsperchannel = rp->result_bits_per_channel;
	if (orientation)    *orientation    = rp->orientation;

	rp->dimensions_queried = TRUE;
	return rp->getinfo_return;
}



/********************************************************
 * 	bmpread_* getters for single dimension values
 *******************************************************/

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



/********************************************************
 * 	s_single_dim_val
 *******************************************************/

static int s_single_dim_val(BMPHANDLE h, enum Dimint dim)
{
	BMPREAD rp;

	if (!(h && cm_check_is_read_handle(h)))
		return 0;
	rp = (BMPREAD)(void*)h;

	if (!rp->getinfo_called)
		return 0;

	switch (dim) {
	case DIM_WIDTH:
		rp->dim_queried_width = TRUE;
		return rp->width;
	case DIM_HEIGHT:
		rp->dim_queried_height = TRUE;
		return rp->height;
	case DIM_CHANNELS:
		rp->dim_queried_channels = TRUE;
		return rp->result_channels;
	case DIM_BITS_PER_CHANNEL:
		rp->dim_queried_bits_per_channel = TRUE;
		return rp->result_bits_per_channel;
	case DIM_ORIENTATION:
		return (int) rp->orientation;
	case DIM_XDPI:
		return rp->ih->xpelspermeter / 39.37 + 0.5;
	case DIM_YDPI:
		return rp->ih->ypelspermeter / 39.37 + 0.5;
	}
	if (rp->dim_queried_width && rp->dim_queried_height &&
	    rp->dim_queried_channels &&
	    rp->dim_queried_bits_per_channel)
		rp->dimensions_queried = TRUE;
	return 0;
}



/********************************************************
 * 	bmpread_buffersize
 *******************************************************/

API size_t bmpread_buffersize(BMPHANDLE h)
{
	BMPREAD rp;

	if (!(h && cm_check_is_read_handle(h)))
		return 0;
	rp = (BMPREAD)(void*)h;

	if (!(rp->getinfo_called &&
                      (rp->getinfo_return == BMP_RESULT_OK ||
                       rp->getinfo_return == BMP_RESULT_INSANE)))
		return 0;

	rp->dimensions_queried = TRUE;
	return rp->result_size;

}



/********************************************************
 * 	bmpread_set_insanity_limit
 *******************************************************/

API void bmpread_set_insanity_limit(BMPHANDLE h, size_t limit)
{
	BMPREAD rp;

	if (!(h && cm_check_is_read_handle(h)))
		return;
	rp = (BMPREAD)(void*)h;

	rp->insanity_limit = limit;

	if (rp->getinfo_return == BMP_RESULT_INSANE &&
	    (limit == 0 || limit >= rp->result_size)) {
		rp->getinfo_return = BMP_RESULT_OK;
	}
}



/********************************************************
 * 	bmpread_set_undefined
 *******************************************************/
API void bmpread_set_undefined_to_alpha(BMPHANDLE h, int mode)
{ bmpread_set_undefined(h, (enum BmpUndefined) mode); }

API void bmpread_set_undefined(BMPHANDLE h, enum BmpUndefined mode)
{
	BMPREAD rp;

	if (!(h && cm_check_is_read_handle(h)))
		return;
	rp = (BMPREAD)(void*)h;

	if (mode == rp->undefined_mode)
		return;

	if (mode != BMP_UNDEFINED_TO_ALPHA && mode != BMP_UNDEFINED_LEAVE) {
		logerr(rp->log, "Invalid undefined-mode selected");
		return;
	}

	rp->undefined_mode = mode;

	if (!rp->getinfo_called || (rp->getinfo_called != BMP_RESULT_OK &&
		                    rp->getinfo_called != BMP_RESULT_INSANE)) {
		return;
	}

	/* we are changing the setting after dimensions have */
	/* been established. Only relevant for RLE-encoding  */

	if (!rp->rle)
		return;

	rp->result_bytes_per_pixel = (mode == BMP_UNDEFINED_TO_ALPHA) ?  4 :  3;
	rp->result_bits_per_pixel  = (mode == BMP_UNDEFINED_TO_ALPHA) ? 32 : 24;
	rp->result_channels        = (mode == BMP_UNDEFINED_TO_ALPHA) ?  4 :  3;

	rp->result_size = (size_t) rp->width *
                          (size_t) rp->height *
                          (size_t) rp->result_bytes_per_pixel;

	rp->dimensions_queried = FALSE;
	rp->dim_queried_channels = FALSE;

        /* we have to redo the insanity-check */
	if (rp->insanity_limit &&
	    rp->result_size > rp->insanity_limit) {
		logerr(rp->log, "file is insanely large");
		rp->getinfo_return = BMP_RESULT_INSANE;
	}
	else if (rp->getinfo_return == BMP_RESULT_INSANE)
		rp->getinfo_return = BMP_RESULT_OK;
}



/********************************************************
 * 	br_free
 *******************************************************/

void br_free(BMPREAD rp)
{
	if (rp->palette)
		free(rp->palette);
	if (rp->ih)
		free(rp->ih);
	if (rp->fh)
		free(rp->fh);
	if (rp->log)
		logfree(rp->log);
	free(rp);
}



/********************************************************
 * 	s_is_bmptype_supported
 *******************************************************/
static int s_is_bmptype_supported_rgb(BMPREAD_R rp);
static int s_is_bmptype_supported_indexed(BMPREAD_R rp);

static int s_is_bmptype_supported(BMPREAD_R rp)
{
	if (rp->ih->planes != 1) {
		logerr(rp->log, "Unsupported number of planes (%d). "
			        "Must be 1.", (int) rp->ih->planes);
		return FALSE;
	}

	if (rp->ih->compression == BI_OS2_HUFFMAN) {
		logerr(rp->log, "Huffman compression not supported");
		return FALSE;
	}

	if (rp->ih->bitcount <= 8)
		return s_is_bmptype_supported_indexed(rp);
	else
		return s_is_bmptype_supported_rgb(rp);

	return TRUE;
}



/********************************************************
 * 	s_is_bmptype_supported_rgb
 *******************************************************/

static int s_is_bmptype_supported_rgb(BMPREAD_R rp)
{
	switch (rp->ih->bitcount) {
	case 16:
	case 24:
	case 32:
	case 64:
		/*  ok */
		break;
	default:
		logerr(rp->log, "Unsupported bitcount %d for RGB image", (int) rp->ih->bitcount);
		return FALSE;
	}

	switch (rp->ih->compression) {
	case BI_RGB:
		/* ok */
		break;
	case BI_BITFIELDS:
	case BI_ALPHABITFIELDS:
		if (rp->ih->bitcount == 64) {
			logerr(rp->log, "Invalid bitcount %d for BITFIELDS", (int) rp->ih->bitcount);
			return FALSE;
		}
		break;
	case BI_OS2_RLE24:
		if (rp->ih->bitcount != 24) {
			logerr(rp->log, "Invalid bitcount %d for RLE24 compression", (int) rp->ih->bitcount);
			return FALSE;
		}
		break;
	default:
		logerr(rp->log, "Unsupported compression %s for RGB image",
		                 s_compression_name(rp->ih->compression));
		return FALSE;
	}

	return TRUE;
}



/********************************************************
 * 	s_is_bmptype_supported_indexed
 *******************************************************/

static int s_is_bmptype_supported_indexed(BMPREAD_R rp)
{
	switch (rp->ih->bitcount) {
	case 1:
	case 2:
	case 4:
	case 8:
		/*  ok */
		break;

	default:
		logerr(rp->log, "Unsupported bitcount %d for indexed image", (int) rp->ih->bitcount);
		return FALSE;
	}

	switch (rp->ih->compression) {
	case BI_RGB:
	case BI_RLE4:
	case BI_RLE8:
		if ( (rp->ih->compression == BI_RLE4 && rp->ih->bitcount != 4) ||
		     (rp->ih->compression == BI_RLE8 && rp->ih->bitcount != 8) ) {
			logerr(rp->log, "Unsupported compression %s for %d-bit data",
			                  s_compression_name(rp->ih->compression),
			                  (int) rp->ih->bitcount);
			return FALSE;
		}
		/*  ok */
		break;

	default:
		logerr(rp->log, "Unsupported compression %s for indexed image",
		                   s_compression_name(rp->ih->compression));
		return FALSE;
	}

	return TRUE;
}



/********************************************************
 * 	s_check_dimensions
 *******************************************************/

static int s_check_dimensions(BMPREAD_R rp)
{
	int total_bits; /* bitsize needed to allocate/address result buffer */

	total_bits = cm_count_bits(rp->width) +
		     cm_count_bits(rp->height) +
		     cm_count_bits(rp->result_bytes_per_pixel);

	if (total_bits > 8 * sizeof(size_t) ||
	    rp->width  > INT_MAX    ||
	    rp->width  < 1          ||
	    rp->height > INT_MAX    ||
	    rp->height < 1       ) {

		logerr(rp->log, "Invalid BMP dimensions (%dx%d)",
				     (int) rp->ih->width, (int) rp->ih->height);
		return FALSE;
	}
	return TRUE;
}



/********************************************************
 * 	s_read_palette
 *******************************************************/

static struct Palette* s_read_palette(BMPREAD_R rp)
{
	int              i, r,g,b;
	struct Palette  *palette;
	size_t           memsize;
	int              bytes_per_entry;
	int              colors_in_file;
	int              max_colors_in_file;
	int              colors_full_palette;
	int              colors_ignore = 0;


	if (rp->ih->clrused > INT_MAX || rp->ih->clrimportant > rp->ih->clrused) {
		logerr(rp->log, "Unreasonable color numbers for palette (%lu/%lu)",
					  (unsigned long) rp->ih->clrused,
					  (unsigned long) rp->ih->clrimportant);
		return NULL;
	}
	if (rp->fh->offbits - rp->bytes_read > INT_MAX) {
		logerr(rp->log, "gap to pixeldata too big (%lu)",
				(unsigned long) rp->fh->offbits - rp->bytes_read);
		return NULL;
	}
	if (rp->fh->offbits < rp->bytes_read) {
		logerr(rp->log, "Invalid offset to pixel data");
		return NULL;
	}

	bytes_per_entry = rp->ih->version == BMPINFO_CORE_OS21 ? 3 : 4;

	max_colors_in_file = (rp->fh->offbits - rp->bytes_read) / bytes_per_entry;

	colors_full_palette = 1 << rp->ih->bitcount;

	if (0 == (colors_in_file = rp->ih->clrused)) {
		colors_in_file = MIN(colors_full_palette, max_colors_in_file);
	}
	else if (colors_in_file > max_colors_in_file) {
		logerr(rp->log, "given palette size too large for available data");
		return NULL;
	}

	if (colors_in_file > colors_full_palette)
		colors_ignore = colors_in_file - colors_full_palette;

	memsize = sizeof *palette + (colors_in_file - colors_ignore) * sizeof palette->color[0];
	if (!(palette = malloc(memsize))) {
		logsyserr(rp->log, "Allocating mem for palette");
		return NULL;
	}

	memset(palette, 0, memsize);
	palette->numcolors = colors_in_file - colors_ignore;
	for (i = 0; i < palette->numcolors; i++) {
		if (EOF == (b = getc(rp->file)) ||
		    EOF == (g = getc(rp->file)) ||
		    EOF == (r = getc(rp->file)) ||
		    ((bytes_per_entry == 4) && (EOF == getc(rp->file))) ) {
			logerr(rp->log, "reading palette entries");
			free (palette);
			return NULL;
		}
		rp->bytes_read += bytes_per_entry;
		palette->color[i].red   = r;
		palette->color[i].green = g;
		palette->color[i].blue  = b;
	}

	for (i = 0; i < colors_ignore; i++) {
		if (!cm_gobble_up(rp->file, bytes_per_entry, rp->log)) {
			logerr(rp->log, "reading superfluous palette entries");
			free(palette);
			return NULL;
		}
		rp->bytes_read += bytes_per_entry;
	}

	return palette;
}



/********************************************************
 * 	s_read_colormasks
 *******************************************************/
static int s_read_masks_from_bitfields(BMPREAD_R rp);
static int s_create_implicit_colormasks(BMPREAD_R rp);
static inline unsigned long s_calc_bits_for_mask(unsigned long long mask);
static inline unsigned long s_calc_shift_for_mask(unsigned long long mask);

static int s_read_colormasks(BMPREAD_R rp)
{
	int   i, max_bits = 0, sum_bits = 0;

	switch (rp->ih->compression) {
	case BI_BITFIELDS:
	case BI_ALPHABITFIELDS:
		if (!s_read_masks_from_bitfields(rp))
			return FALSE;
		break;

	case BI_RGB:
		if (!s_create_implicit_colormasks(rp))
			return FALSE;
		break;

	default:
		logerr(rp->log, "Invalid compression (%s)", s_compression_name(rp->ih->compression));
		return FALSE;
	}

	if (rp->colormask.mask.alpha) {
		rp->has_alpha = TRUE;
		rp->result_channels = 4;
	}
	else {
		rp->has_alpha = FALSE;
		rp->result_channels = 3;
	}

	for (i = 0; i < 4; i++) {
		max_bits = MAX(max_bits, rp->colormask.bits.value[i]);
		sum_bits += rp->colormask.bits.value[i];
	}
	if (max_bits > MIN(rp->ih->bitcount, 32) || sum_bits > rp->ih->bitcount) {
		logerr(rp->log, "Invalid mask bitcount (max=%d, sum=%d)", max_bits, sum_bits);
		return FALSE;
	}
	if (!(rp->colormask.mask.red | rp->colormask.mask.green | rp->colormask.mask.blue)) {
		logerr(rp->log, "Empty color masks. Corrupted BMP?");
		return FALSE;
	}
	if (rp->colormask.mask.red  & rp->colormask.mask.green &
	    rp->colormask.mask.blue & rp->colormask.mask.alpha) {
		logerr(rp->log, "Overlapping color masks. Corrupt BMP?");
		return FALSE;
	}

	/* calculate required bit-depth for output bitmap (8, 16, or 32) */

	rp->result_bits_per_channel = 8;
	while (rp->result_bits_per_channel < max_bits && rp->result_bits_per_channel < 32)
		rp->result_bits_per_channel *= 2;

	rp->result_bits_per_pixel = rp->result_bits_per_channel * rp->result_channels;
	rp->result_bytes_per_pixel = rp->result_bits_per_pixel / 8;

	return TRUE;
}



/********************************************************
 * 	s_read_masks_from_bitfields
 *******************************************************/

static int s_read_masks_from_bitfields(BMPREAD_R rp)
{
	uint32_t  r,g,b,a;
	int       i;

	if (!(rp->ih->bitcount == 16 || rp->ih->bitcount == 32)) {
		logerr(rp->log, "Invalid bitcount (%d) for BI_BITFIELDS. Must be 16 or 32",
								   (int) rp->ih->bitcount);
		return FALSE;
	}

	if (rp->ih->version < BMPINFO_V3_ADOBE1) {
		if (!(read_u32_le(rp->file, &r) &&
		      read_u32_le(rp->file, &g) &&
		      read_u32_le(rp->file, &b))) {
			logsyserr(rp->log, "Reading BMP color masks");
			return FALSE;
		}
		rp->bytes_read += 12;
		rp->colormask.mask.red   = r;
		rp->colormask.mask.green = g;
		rp->colormask.mask.blue  = b;
		if (rp->ih->compression == BI_ALPHABITFIELDS) {
			if (!read_u32_le(rp->file, &a)) {
				logsyserr(rp->log, "Reading BMP color masks");
				return FALSE;
			}
			rp->bytes_read += 4;
			rp->colormask.mask.alpha = a;
		}

	}
	else {
		rp->colormask.mask.red   = rp->ih->redmask;
		rp->colormask.mask.green = rp->ih->greenmask;
		rp->colormask.mask.blue  = rp->ih->bluemask;
		if (rp->ih->version >= BMPINFO_V3_ADOBE2)
			rp->colormask.mask.alpha = rp->ih->alphamask;
	}

	for (i = 0; i < (rp->colormask.mask.alpha ? 4 : 3); i++) {
		rp->colormask.bits.value[i]  = s_calc_bits_for_mask(rp->colormask.mask.value[i]);
		rp->colormask.shift.value[i] = s_calc_shift_for_mask(rp->colormask.mask.value[i]);
	}

	return TRUE;
}



/********************************************************
 * 	s_create_implicit_colormasks
 *******************************************************/

static int s_create_implicit_colormasks(BMPREAD_R rp)
{
	int i, bits_per_channel;

	switch (rp->ih->bitcount) {
	case 16:
		bits_per_channel = 5;
		break;

	case 24:
	case 32:
		bits_per_channel = 8;
		break;

	case 64:
		bits_per_channel = 16;
		break;
	default:
		logerr(rp->log, "Invalid bitcount for BMP (%d)", (int) rp->ih->bitcount);
		return FALSE;
	}


	for (i = 0; i < 3; i++) {
		rp->colormask.shift.value[i] = (2-i) * bits_per_channel;
		rp->colormask.mask.value[i]  =
			   ((1ULL<<bits_per_channel)-1) << rp->colormask.shift.value[i];
		rp->colormask.bits.value[i]  = s_calc_bits_for_mask(rp->colormask.mask.value[i]);
	}

	if (rp->ih->bitcount == 64) {
		rp->colormask.shift.alpha = 3 * bits_per_channel;
		rp->colormask.mask.alpha  =
			   ((1ULL<<bits_per_channel)-1) << rp->colormask.shift.alpha;
		rp->colormask.bits.alpha  = s_calc_bits_for_mask(rp->colormask.mask.alpha);

	}

	return TRUE;
}



/********************************************************
 * 	s_calc_bits_for_mask
 *******************************************************/

static inline unsigned long s_calc_bits_for_mask(unsigned long long mask)
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



/********************************************************
 * 	s_calc_shift_for_mask
 *******************************************************/

static inline unsigned long s_calc_shift_for_mask(unsigned long long mask)
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



/********************************************************
 * 	s_read_file_header
 *******************************************************/
static int s_read_file_header(BMPREAD_R rp)
{
	if (read_u16_le(rp->file, &rp->fh->type)      &&
	    read_u32_le(rp->file, &rp->fh->size)      &&
	    read_u16_le(rp->file, &rp->fh->reserved1) &&
	    read_u16_le(rp->file, &rp->fh->reserved2) &&
	    read_u32_le(rp->file, &rp->fh->offbits)    ) {

		rp->bytes_read += 14;
		return TRUE;
	}

	if (feof(rp->file)) {
		logerr(rp->log, "unexpected end-of-file while reading "
				    "file header");
	}
	else {
		logsyserr(rp->log, "error reading file header");
	}

	return FALSE;
}



/********************************************************
 * 	s_read_info_header
 *******************************************************/
static void s_detect_os2_compression(BMPREAD_R rp);

static int s_read_info_header(BMPREAD_R rp)
{
	uint16_t  w16, h16, padding;
	int       skip, i, filepos;

	filepos = (int) rp->bytes_read;

	if (!read_u32_le(rp->file, &rp->ih->size))
		goto abort_file_err;
	rp->bytes_read += 4;

	switch (rp->ih->size) {
	case  12: rp->ih->version = BMPINFO_CORE_OS21; break;
	case  16:
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
			logerr(rp->log, "Invalid info header size (%lu)", (unsigned long) rp->ih->size);
			return FALSE;
		}
		break;
	}


	if (rp->ih->version == BMPINFO_CORE_OS21) {
		if (!(read_u16_le(rp->file, &w16)              &&
		      read_u16_le(rp->file, &h16)              &&
		      read_u16_le(rp->file, &rp->ih->planes)   &&
		      read_u16_le(rp->file, &rp->ih->bitcount)
		                                                )) {
			goto abort_file_err;
		}
		rp->ih->width  = w16;
		rp->ih->height = h16;
		rp->bytes_read += 8;  /* 12  */
		goto header_done;
	}

	if (!(read_s32_le(rp->file, &rp->ih->width)    &&
	      read_s32_le(rp->file, &rp->ih->height)   &&
	      read_u16_le(rp->file, &rp->ih->planes)   &&
	      read_u16_le(rp->file, &rp->ih->bitcount)
	                                                  )) {
		goto abort_file_err;
	}
	rp->bytes_read += 12;     /* 16 */

	if (rp->ih->size == 16) { /* 16-byte BMPINFO_OS22 */
		goto header_done;
	}

	if (!(read_u32_le(rp->file, &rp->ih->compression)   &&
	      read_u32_le(rp->file, &rp->ih->sizeimage)     &&
	      read_s32_le(rp->file, &rp->ih->xpelspermeter) &&
	      read_s32_le(rp->file, &rp->ih->ypelspermeter) &&
	      read_u32_le(rp->file, &rp->ih->clrused)       &&
	      read_u32_le(rp->file, &rp->ih->clrimportant)
	                                                     )) {
		goto abort_file_err;
	}
	rp->bytes_read += 24;     /* 40 */

	if (rp->ih->version == BMPINFO_OS22) {
		if (!(read_u16_le(rp->file, &rp->ih->resolution)     &&
		      read_u16_le(rp->file, &padding)                &&
		      read_u16_le(rp->file, &rp->ih->orientation)    &&
		      read_u16_le(rp->file, &rp->ih->halftone_alg)   &&
		      read_u32_le(rp->file, &rp->ih->halftone_parm1) &&
		      read_u32_le(rp->file, &rp->ih->halftone_parm2) &&
		      read_u32_le(rp->file, &rp->ih->color_encoding) &&
		      read_u32_le(rp->file, &rp->ih->app_id)
		                                                  )) {
			goto abort_file_err;
		}

		rp->bytes_read += 24;    /* 64 */
		goto header_done;
	}

	if (rp->ih->version >= BMPINFO_V3_ADOBE1) {
		if (!(read_u32_le(rp->file, &rp->ih->redmask)   &&
		      read_u32_le(rp->file, &rp->ih->greenmask) &&
		      read_u32_le(rp->file, &rp->ih->bluemask)
		                                               )) {
			goto abort_file_err;
		}
		rp->bytes_read += 12;  /* 52 */
	}

	if (rp->ih->version >= BMPINFO_V3_ADOBE2) {
		if (!(read_u32_le(rp->file, &rp->ih->alphamask) )) {
			goto abort_file_err;
		}
		rp->bytes_read += 4;   /* 56 */
	}


	if (rp->ih->version >= BMPINFO_V4) {
		if (!(read_u32_le(rp->file, &rp->ih->cstype)     &&
		      read_s32_le(rp->file, &rp->ih->redX)       &&
		      read_s32_le(rp->file, &rp->ih->redY)       &&
		      read_s32_le(rp->file, &rp->ih->redZ)       &&
		      read_s32_le(rp->file, &rp->ih->greenX)     &&
		      read_s32_le(rp->file, &rp->ih->greenY)     &&
		      read_s32_le(rp->file, &rp->ih->greenZ)     &&
		      read_s32_le(rp->file, &rp->ih->blueX)      &&
		      read_s32_le(rp->file, &rp->ih->blueY)      &&
		      read_s32_le(rp->file, &rp->ih->blueZ)      &&
		      read_u32_le(rp->file, &rp->ih->gammared)   &&
		      read_u32_le(rp->file, &rp->ih->gammagreen) &&
		      read_u32_le(rp->file, &rp->ih->gammablue)
			                                         )) {
			goto abort_file_err;
		}
		rp->bytes_read += 52;  /* 108 */
	}


	if (rp->ih->version >= BMPINFO_V5) {
		if (!(read_u32_le(rp->file, &rp->ih->intent)      &&
		      read_u32_le(rp->file, &rp->ih->profiledata) &&
		      read_u32_le(rp->file, &rp->ih->profilesize) &&
		      read_u32_le(rp->file, &rp->ih->reserved)
			                                          )) {
			goto abort_file_err;
		}
		rp->bytes_read += 16;  /* 124 */
	}

header_done:
	/* read past bigger info headers: */
	skip = (int) rp->ih->size - ((int) rp->bytes_read - filepos);

	for (i = 0; i < skip; i++) {
		if (EOF == getc(rp->file))
			goto abort_file_err;
		rp->bytes_read++;
	}

	s_detect_os2_compression(rp);

	return TRUE;

abort_file_err:
	if (feof(rp->file))
		logerr(rp->log, "Unexpected end of file while reading BMP info header");
	else
		logsyserr(rp->log, "While reading BMP info header");
	return FALSE;

}



/********************************************************
 * 	s_detect_os2_compression
 *******************************************************/

static void s_detect_os2_compression(BMPREAD_R rp)
{
	if (rp->ih->version == BMPINFO_V3) {
		/* might actually be a 40-byte OS/2 header */
		if (rp->ih->compression == BI_OS2_HUFFMAN_DUP) {
			/* might be huffman or BITFIELDS */
			if (rp->ih->bitcount == 1) {
				rp->ih->version     = BMPINFO_OS22;
				rp->ih->compression = BI_OS2_HUFFMAN;
			}
		}
		else if (rp->ih->compression == BI_OS2_RLE24_DUP) {
			/* might be RLE24 or JPEG */
			if (rp->ih->bitcount == 24) {
				rp->ih->version     = BMPINFO_OS22;
				rp->ih->compression = BI_OS2_RLE24;
			}
		}
	}
	else if (rp->ih->version <= BMPINFO_OS22) {
		switch (rp->ih->compression) {
		case BI_OS2_HUFFMAN_DUP:   /* = BI_BITFIELDS */
			rp->ih->compression = BI_OS2_HUFFMAN;
			break;
		case BI_OS2_RLE24_DUP:     /* = BI_JPEG */
			rp->ih->compression = BI_OS2_RLE24;
			break;
		}
	}
}



/********************************************************
 * 	bmpread_info_*  int
 *******************************************************/

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


/********************************************************
 * 	s_info_int
 *******************************************************/
static int s_info_int(BMPHANDLE h, enum Infoint info)
{
	BMPREAD rp;

	if (!(h && cm_check_is_read_handle(h)))
		return 0;
	rp = (BMPREAD)(void*)h;

	if (!rp->getinfo_called)
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



/********************************************************
 * 	bmpread_info_*  str
 *******************************************************/

enum Infostr {
	INFO_STR_HEADER_NAME,
	INFO_STR_COMPRESSION_NAME,
};

static const char* s_info_str(BMPHANDLE h, enum Infostr info);

API const char* bmpread_info_header_name(BMPHANDLE h)
{ return s_info_str(h, INFO_STR_HEADER_NAME); }
API const char* bmpread_info_compression_name(BMPHANDLE h)
{ return s_info_str(h, INFO_STR_COMPRESSION_NAME); }


/********************************************************
 * 	s_info_str
 *******************************************************/
static const char* s_info_str(BMPHANDLE h, enum Infostr info)
{
	BMPREAD rp;

	if (!(h && cm_check_is_read_handle(h)))
		return "";
	rp = (BMPREAD)(void*)h;

	if (!rp->getinfo_called)
		return "";

	switch (info) {
	case INFO_STR_HEADER_NAME:
		return s_infoheader_name(rp->ih->version);
	case INFO_STR_COMPRESSION_NAME:
		return s_compression_name(rp->ih->compression);

	}
	return "";
}



/********************************************************
 * 	bmpread_info_channel_bits
 *******************************************************/

API BMPRESULT bmpread_info_channel_bits(BMPHANDLE h, int *r, int *g, int *b, int *a)
{
	BMPREAD rp;

	if (!(h && cm_check_is_read_handle(h)))
		return BMP_RESULT_ERROR;
	rp = (BMPREAD)(void*)h;

	if (!(rp->getinfo_called &&
	               (rp->getinfo_return == BMP_RESULT_OK ||
	                rp->getinfo_return == BMP_RESULT_INSANE)))
		return BMP_RESULT_ERROR;

	*r = rp->colormask.bits.red;
	*g = rp->colormask.bits.green;
	*b = rp->colormask.bits.blue;
	*a = rp->colormask.bits.alpha;

	return BMP_RESULT_OK;
}



/********************************************************
 * 	s_infoheader_name
 *******************************************************/

const char* s_infoheader_name(int infoversion)
{
	switch (infoversion) {
	case BMPINFO_CORE_OS21 : return "OS21XBITMAPHEADER";
	case BMPINFO_OS22      : return "OS22XBITMAPHEADER";
	case BMPINFO_V3        : return "BITMAPINFOHEADER";
	case BMPINFO_V3_ADOBE1 : return "BITMAPINFOHEADER + RGB mask";
	case BMPINFO_V3_ADOBE2 : return "BITMAPINFOHEADER + RGBA mask";
	case BMPINFO_V4        : return "BITMAPV4HEADER";
	case BMPINFO_V5        : return "BITMAPV5HEADER";
	case BMPINFO_FUTURE    : return "unknown future version";
	default:
		return "invalid infoheader version";
	}
}



/********************************************************
 * 	s_compression_name
 *******************************************************/

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
