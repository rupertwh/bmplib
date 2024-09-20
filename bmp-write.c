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
#include <errno.h>

#include "config.h"
#include "bmplib.h"
#include "logging.h"
#include "bmp-common.h"
#include "bmp-write.h"


typedef struct Bmpwrite * restrict BMPWRITE_R;

struct Bmpwrite {
	struct {
		uint32_t magic;
		LOG      log;
	};
	FILE            *file;
	struct Bmpfile  *fh;
	struct Bmpinfo  *ih;
	unsigned         width;
	unsigned         height;
	/* input */
	int              source_channels;
	int              source_bits_per_channel;
	int              source_bytes_per_pixel;
	/* output */
	int              has_alpha;
	int              outbytes_per_pixel;
	struct Colormask colormask;
	/* state */
	int              outbits_set;
	int              dimensions_set;
	int              saveimage_done;
};


static int s_create_header(BMPWRITE_R wp);
static inline unsigned long s_set_outpixel_rgb(BMPWRITE_R wp, void* restrict image, size_t offs);
static int s_write_bmp_file_header(struct Bmpfile *bfh, FILE *file);
static int s_write_bmp_info_header(struct Bmpinfo *bih, FILE *file);
static int s_check_is_write_handle(BMPHANDLE h);
static inline unsigned s_scaleint(unsigned long val, int frombits, int tobits);

#define ALL_EQUAL3(a,b,c) ( (a)==(b) && (a)==(c) )
#define ALL_POSITIVE4(a,b,c,d) ( (a)>=0 && (b)>=0 && (c)>=0 && (d)>=0)
#define ALL_LESSTHAN4(x,a,b,c,d) ( (a)<(x) && (b)<(x) && (c)<(x) && (d)<(x))



/********************************************************
 * 	bmpwrite_new
 *******************************************************/

EXPORT_VIS BMPHANDLE bmpwrite_new(FILE *file)
{
	BMPWRITE wp = NULL;

	if (!(wp = malloc(sizeof *wp))) {
		goto abort;
	}
	memset(wp, 0, sizeof *wp);
	wp->magic = HMAGIC_WRITE;


	if (!(wp->log = logcreate()))
		goto abort;


	if (sizeof(int) < 4 || sizeof(unsigned int) < 4) {
		logerr(wp->log, "code doesn't work on %s-bit platforms!\n", 8 * sizeof(int));
		goto abort;
	}

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



/********************************************************
 * 	bmpwrite_set_dimensions
 *******************************************************/

EXPORT_VIS BMPRESULT bmpwrite_set_dimensions(BMPHANDLE h,
                                       unsigned  width,
                                       unsigned  height,
                                       unsigned  source_channels,
                                       unsigned  source_bits_per_channel)
{
	int total_bits;
	BMPWRITE wp;

	if (!(h && s_check_is_write_handle(h)))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (wp->saveimage_done) {
		logerr(wp->log, "Image already saved.");
		return BMP_RESULT_ERROR;
	}

	if (source_channels < 3 || source_channels > 4) {
		logerr(wp->log, "Invalid number of channels: %d", source_channels);
		return BMP_RESULT_ERROR;
	}

	switch (source_bits_per_channel) {
	case 8:
	case 16:
	case 32:
		/* ok */
		break;
	default:
		logerr(wp->log, "Invalid number of bits per channel: %d", source_bits_per_channel);
		return BMP_RESULT_ERROR;
	}

	wp->source_bytes_per_pixel = source_bits_per_channel / 8 * source_channels;
	total_bits = cm_count_bits(width) +
	             cm_count_bits(height) +
	             cm_count_bits(wp->source_bytes_per_pixel);

	if (width > INT_MAX || height > INT_MAX ||
	    width < 1 || height < 1 ||
	    total_bits > 8*sizeof(size_t) ) {
		logerr(wp->log, "Invalid dimensions %ux%ux%d @ %dbits",
		                    width, height, source_channels, source_bits_per_channel);
		return BMP_RESULT_ERROR;
	}

	wp->width = width;
	wp->height = height;
	wp->source_channels = source_channels;
	wp->source_bits_per_channel = source_bits_per_channel;
	wp->dimensions_set = TRUE;

	return BMP_RESULT_OK;

}



/********************************************************
 * 	bmpwrite_set_output_bits
 *******************************************************/

EXPORT_VIS BMPRESULT bmpwrite_set_output_bits(BMPHANDLE h, int red, int green, int blue, int alpha)
{
	BMPWRITE wp;

	if (!(h && s_check_is_write_handle(h)))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (wp->saveimage_done) {
		logerr(wp->log, "Image already saved.");
		return BMP_RESULT_ERROR;
	}

	if (!(ALL_POSITIVE4(red, green, blue, alpha) &&
	      ALL_LESSTHAN4(33, red, green, blue, alpha) &&
	      red + green + blue > 0 &&
	      red + green + blue + alpha <= 32 )) {
		logerr(wp->log, "Invalid output bit depths specified: %d-%d-%d - %d",
		                              red, green, blue, alpha);
		wp->outbits_set = FALSE;
		return BMP_RESULT_ERROR;
	}

	wp->colormask.bits.red   = red;
	wp->colormask.bits.green = green;
	wp->colormask.bits.blue  = blue;
	wp->colormask.bits.alpha = alpha;

	wp->outbits_set = TRUE;

	return BMP_RESULT_OK;
}



/********************************************************
 * 	bmpwrite_save_image
 *******************************************************/

EXPORT_VIS BMPRESULT bmpwrite_save_image(BMPHANDLE h, void *image)
{
	size_t        offs, linelength;
	unsigned long bytes;
	unsigned char padding[4] = {0,0,0,0};
	int           pad, i, x, y;
	BMPWRITE wp;

	if (!(h && s_check_is_write_handle(h)))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (wp->saveimage_done) {
		logerr(wp->log, "Cannot call bmp_write_saveimage more than once");
		return BMP_RESULT_ERROR;
	}


	if (!wp->dimensions_set) {
		logerr(wp->log, "Must set dimensions before saving");
		return BMP_RESULT_ERROR;
	}

	wp->saveimage_done = TRUE;

	pad = s_create_header(wp);

	if (!s_write_bmp_file_header(wp->fh, wp->file)) {
		logsyserr(wp->log, "Writing BMP file header");
		goto abort;
	}

	if (!s_write_bmp_info_header(wp->ih, wp->file)) {
		logsyserr(wp->log, "Writing BMP info header");
		goto abort;
	}

#ifdef DEBUG
	printf("RGB format: %d-%d-%d - %d\n", wp->colormask.bits.red, wp->colormask.bits.green,
		                         wp->colormask.bits.blue, wp->colormask.bits.alpha);
	printf("masks: 0x%04lx 0x%04lx 0x%04lx \nshift: 0x%04lx 0x%04lx 0x%04lx \n", 
	              wp->colormask.mask.red, wp->colormask.mask.green, wp->colormask.mask.blue,
	              wp->colormask.shift.red, wp->colormask.shift.green, wp->colormask.shift.blue);
	printf("bmpinfo: %d\nBits: %d bytes: %d\n", wp->ih->version,
		                          (int) wp->ih->bitcount, wp->outbytes_per_pixel);
#endif

	linelength = (size_t) wp->width * (size_t) wp->source_channels;
	for (y = wp->height - 1; y >= 0; y--) {
		for (x = 0; x < wp->width; x++) {
			offs = (size_t) y * linelength + (size_t) x * (size_t) wp->source_channels;
			bytes = s_set_outpixel_rgb(wp, image, offs);
			if (bytes == (unsigned long)-1)
				return BMP_RESULT_ERROR;

			for (i = 0; i < wp->outbytes_per_pixel; i++) {
				if (EOF == fputc((bytes >> (8*i)) & 0xff, wp->file)) {
					logsyserr(wp->log, "Writing image to BMP file");
					goto abort;
				}
			}
		}

		if (pad != fwrite(padding, 1, pad, wp->file)) {
			logsyserr(wp->log, "Writing padding bytes to BMP file");
			goto abort;
		}
	}

	return BMP_RESULT_OK;

abort:
	return BMP_RESULT_TRUNCATED;
}



/********************************************************
 * 	bw_free
 *******************************************************/

void bw_free(BMPWRITE wp)
{
	if (wp->ih)
		free(wp->ih);
	if (wp->fh)
		free(wp->fh);
	if (wp->log)
		logfree(wp->log);

	free(wp);
}



/********************************************************
 * 	s_create_header
 *******************************************************/

static int s_create_header(BMPWRITE_R wp)
{
	int   bitsum, i, pad;

	if ((wp->source_channels == 4 || wp->source_channels == 2) &&
	    ((wp->outbits_set && wp->colormask.bits.alpha) || !wp->outbits_set) ) {
		wp->has_alpha = TRUE;
	}
	else {
		wp->colormask.bits.alpha = 0;
		wp->has_alpha = FALSE;
	}


	if (!wp->outbits_set) {
		wp->colormask.bits.red = wp->colormask.bits.green = wp->colormask.bits.blue = 8;
		if (wp->has_alpha)
			wp->colormask.bits.alpha = 8;
	}

	bitsum = 0;
	for (i = 0; i < 4; i++)
		bitsum += wp->colormask.bits.value[i];

	/* we need BI_BITFIELDS if any of the following is true:
	 *    - not all RGB-components have the same bitlength
	 *    - we are writing an alpha-channel
	 *    - bits per component are not either 5 or 8 (which have
	 *      known RI_RGB representation)
	 */

	if (!ALL_EQUAL3( wp->colormask.bits.red, wp->colormask.bits.green, wp->colormask.bits.blue) ||
	                 wp->has_alpha ||
                    (wp->colormask.bits.red > 0 && wp->colormask.bits.red != 5 && wp->colormask.bits.red != 8)
                            ) {

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
	 * resulting in bitcount of 16 or 24.
	 * (No indexed images yet!)
	 */
	else {
		wp->ih->version     = BMPINFO_V3;
		wp->ih->size        = BMPIHSIZE_V3;
		wp->ih->compression = BI_RGB;

		wp->ih->bitcount = (bitsum + 7) / 8 * 8;
	}

	wp->outbytes_per_pixel = wp->ih->bitcount / 8;

	wp->colormask.mask.red    = (1<<wp->colormask.bits.red) - 1;
	wp->colormask.shift.red   = wp->colormask.bits.green + wp->colormask.bits.blue + wp->colormask.bits.alpha;
	wp->colormask.mask.green  = (1<<wp->colormask.bits.green) - 1;
	wp->colormask.shift.green = wp->colormask.bits.blue + wp->colormask.bits.alpha;
	wp->colormask.mask.blue   = (1<<wp->colormask.bits.blue) - 1;
	wp->colormask.shift.blue  = wp->colormask.bits.alpha;
	wp->colormask.mask.alpha  = (1<<wp->colormask.bits.alpha) - 1;
	wp->colormask.shift.alpha = 0;


	if (wp->ih->version >= BMPINFO_V4) {
		wp->ih->redmask   = wp->colormask.mask.red   << wp->colormask.shift.red;
		wp->ih->greenmask = wp->colormask.mask.green << wp->colormask.shift.green;
		wp->ih->bluemask  = wp->colormask.mask.blue  << wp->colormask.shift.blue;
		wp->ih->alphamask = wp->colormask.mask.alpha << wp->colormask.shift.alpha;
	}

	pad = cm_align4padding(wp->outbytes_per_pixel * wp->width);

	wp->fh->type = 0x4d42; /* "BM" */
	wp->fh->size = BMPFHSIZE + wp->ih->size + (wp->width * wp->outbytes_per_pixel + pad) * wp->height;
	wp->fh->offbits = BMPFHSIZE + wp->ih->size;

	wp->ih->width = wp->width;
	wp->ih->height = wp->height;
	wp->ih->planes = 1;
	wp->ih->sizeimage = 0;
	wp->ih->xpelspermeter = 72 * 39.37;
	wp->ih->ypelspermeter = 72 * 39.37;

	return pad;
}



/********************************************************
 * 	s_set_outpixel_rgb
 *******************************************************/

static inline unsigned long s_set_outpixel_rgb(BMPWRITE_R wp, void* restrict image, size_t offs)
{
	unsigned long bytes, r,g,b, a = 0;
	int           alpha_offs, rgb = TRUE;

	if (wp->source_channels < 3)
		rgb = FALSE; /* grayscale */

	if (wp->has_alpha)
		alpha_offs = rgb ? 3 : 1;

	switch(wp->source_bits_per_channel) {
	case 8:
		r = ((unsigned char*)image)[offs];
		if (rgb) {
			g = ((unsigned char*)image)[offs+1];
			b = ((unsigned char*)image)[offs+2];
		}
		else {
			g = b = r;
		}

		if (wp->has_alpha)
			a = ((unsigned char*)image)[offs+alpha_offs];
		break;
	case 16:
		r = ((uint16_t*)image)[offs];
		if (rgb) {
			g = ((uint16_t*)image)[offs+1];
			b = ((uint16_t*)image)[offs+2];
		}
		else {
			g = b = r;
		}
		if (wp->has_alpha)
			a = ((uint16_t*)image)[offs+alpha_offs];
		break;

	case 32:
		r = ((uint32_t*)image)[offs];
		if (rgb) {
			g = ((uint32_t*)image)[offs+1];
			b = ((uint32_t*)image)[offs+2];
		}
		else {
			g = b = r;
		}
		if (wp->has_alpha)
			a = ((uint32_t*)image)[offs+alpha_offs];
		break;

	default:
		logerr(wp->log, "Panic! Bitdepth (%d) other than 8/16/32", 
			                     wp->source_bits_per_channel);
		return (unsigned long)-1;
	}

	r = s_scaleint(r, wp->source_bits_per_channel, wp->colormask.bits.red);
	g = s_scaleint(g, wp->source_bits_per_channel, wp->colormask.bits.green);
	b = s_scaleint(b, wp->source_bits_per_channel, wp->colormask.bits.blue);
	if (wp->has_alpha)
		a = s_scaleint(a, wp->source_bits_per_channel, wp->colormask.bits.alpha);

	bytes = 0;
	bytes |= r << wp->colormask.shift.red;
	bytes |= g << wp->colormask.shift.green;
	bytes |= b << wp->colormask.shift.blue;
	if (wp->has_alpha)
		bytes |= a << wp->colormask.shift.alpha;

	return bytes;
}



/********************************************************
 * 	s_write_bmp_file_header
 *******************************************************/

static int s_write_bmp_file_header(struct Bmpfile *bfh, FILE *file)
{
	return write_u16_le(file, bfh->type) &&
	       write_u32_le(file, bfh->size) &&
	       write_u16_le(file, bfh->reserved1) &&
	       write_u16_le(file, bfh->reserved2) &&
	       write_u32_le(file, bfh->offbits);
}



/********************************************************
 * 	s_write_bmp_info_header
 *******************************************************/

static int s_write_bmp_info_header(struct Bmpinfo *bih, FILE *file)
{
	if (!(write_u32_le(file, bih->size) &&
	      write_s32_le(file, bih->width) &&
	      write_s32_le(file, bih->height) &&
	      write_u16_le(file, bih->planes) &&
	      write_u16_le(file, bih->bitcount) &&
	      write_u32_le(file, bih->compression) &&
	      write_u32_le(file, bih->sizeimage) &&
	      write_s32_le(file, bih->xpelspermeter) &&
	      write_s32_le(file, bih->ypelspermeter) &&
	      write_u32_le(file, bih->clrused) &&
	      write_u32_le(file, bih->clrimportant) )) {
		return FALSE;
	}
	if (bih->version == BMPINFO_V3)
		return TRUE;

	return write_u32_le(file, bih->redmask) &&
	       write_u32_le(file, bih->greenmask) &&
	       write_u32_le(file, bih->bluemask) &&
	       write_u32_le(file, bih->alphamask) &&
	       write_u32_le(file, bih->cstype) &&
	       write_s32_le(file, bih->redX) &&
	       write_s32_le(file, bih->redY) &&
	       write_s32_le(file, bih->redZ) &&
	       write_s32_le(file, bih->greenX) &&
	       write_s32_le(file, bih->greenY) &&
	       write_s32_le(file, bih->greenZ) &&
	       write_s32_le(file, bih->blueX) &&
	       write_s32_le(file, bih->blueY) &&
	       write_u32_le(file, bih->blueZ) &&
	       write_u32_le(file, bih->gammared) &&
	       write_u32_le(file, bih->gammagreen) &&
	       write_u32_le(file, bih->gammablue);

}



/********************************************************
 * 	s_check_is_write_handle
 *******************************************************/

static int s_check_is_write_handle(BMPHANDLE h)
{
	BMPWRITE wp = (BMPWRITE)(void*)h;

	if (wp && wp->magic == HMAGIC_WRITE)
		return TRUE;
	return FALSE;
}



/********************************************************
 * 	s_scaleint
 *******************************************************/

static inline unsigned s_scaleint(unsigned long val, int frombits, int tobits)
{
	unsigned long result;
	int           spaceleft;

	/* scaling down, easy */
	if (frombits >= tobits)
		return val >> (frombits - tobits);

	if (frombits < 1)
		return 0UL;

	/* scaling up */
	result = val << (tobits - frombits);
	spaceleft = tobits - frombits;

	while (spaceleft > 0) {
		if (spaceleft >= frombits)
			result |= val << (spaceleft - frombits);
		else
			result |= val >> (frombits - spaceleft);
		spaceleft -= frombits;
	}

	return result;
}
