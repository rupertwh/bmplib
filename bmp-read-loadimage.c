/* bmplib - bmp-read-loadimage.c
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
#include <stdbool.h>
#include <limits.h>
#include <assert.h>
#include <math.h>

#define BMPLIB_LIB

#include "config.h"
#include "bmplib.h"
#include "logging.h"
#include "bmp-common.h"
#include "huffman.h"
#include "bmp-read.h"


/*
 * Rough overview over top-level functions defined in this file:
 *
 *
 * API entry points       bmpread_load_image()        bmpread_load_line()
 *                                      \                 /
 *                                       \               /
 * common prep work,                      \             /
 * buffer allocation,                 s_load_image_or_line()
 * sanity checks, etc.                     |           |
 *                                         |           |
 *                                         |           |
 *                            s_read_whole_image()     |
 *                                         \           |
 *                                          \          |
 *                                        s_read_one_line()
 *                                               |
 *                                        s_read_rgb_line()
 * 'grunt work'                         s_read_indexed_line()
 *                                        s_read_rle_line()
 *                                      s_read_huffman_line()
 */

static inline unsigned long s_scaleint(unsigned long val, int frombits, int tobits) ATTR_CONST;
static void s_set_file_error(BMPREAD_R rp);
static void s_log_error_from_state(BMPREAD_R rp);
static bool s_cont_error(BMPREAD_R rp);
static bool s_stopping_error(BMPREAD_R rp);
static inline int s_read_one_byte(BMPREAD_R rp);
static inline void s_int8_to_result_format(BMPREAD_R rp, const int *restrict fromrgba, unsigned char *restrict px);

static BMPRESULT s_load_image_or_line(BMPREAD_R rp, unsigned char **restrict buffer, bool line_by_line);
static void s_read_rgb_line(BMPREAD_R rp, unsigned char *restrict line);
static void s_read_indexed_line(BMPREAD_R rp, unsigned char *restrict line);
static void s_read_rle_line(BMPREAD_R rp, unsigned char *restrict line,
                               int *restrict x, int *restrict yoff);
static void s_read_huffman_line(BMPREAD_R rp, unsigned char *restrict line);
static bool s_are_settings_icon_compatible(BMPREAD_R rp);

static_assert(sizeof(float) == 4, "sizeof(float) must be 4. Cannot build bmplib.");
static_assert(sizeof(int) * CHAR_BIT >= 32, "int must be at least 32bit. Cannot build bmplib.");


/********************************************************
 * 	bmpread_load_image
 *******************************************************/

API BMPRESULT bmpread_load_image(BMPHANDLE h, unsigned char **restrict buffer)
{
	BMPREAD rp;

	if (!(rp = cm_read_handle(h)))
		return BMP_RESULT_ERROR;

	return s_load_image_or_line(rp, buffer, false);
}



/********************************************************
 * 	bmpread_load_line
 *******************************************************/

API BMPRESULT bmpread_load_line(BMPHANDLE h, unsigned char **restrict buffer)
{
	BMPREAD rp;

	if (!(rp = cm_read_handle(h)))
		return BMP_RESULT_ERROR;

	logreset(rp->c.log); /* otherwise we might accumulate thousands  */
	                   /* of log entries with large corrupt images */

	return s_load_image_or_line(rp, buffer, true);
}



/********************************************************
 * 	s_load_image_or_line
 *******************************************************/

static void s_read_whole_image(BMPREAD_R rp, unsigned char *restrict image);
static void s_read_one_line(BMPREAD_R rp, unsigned char *restrict image);

static BMPRESULT s_load_image_or_line(BMPREAD_R rp, unsigned char **restrict buffer, bool line_by_line)
{
	size_t	buffer_size;

	if (rp->read_state == RS_FATAL) {
		logerr(rp->c.log, "Cannot load image due to a previous fatal error");
		return BMP_RESULT_ERROR;
	}

	if (rp->read_state >= RS_ARRAY) {
		logerr(rp->c.log, "Invalid operation on bitmap array");
		return BMP_RESULT_ERROR;
	}

	if (rp->read_state >= RS_LOAD_DONE) {
		logerr(rp->c.log, "Cannot load image more than once!");
		return BMP_RESULT_ERROR;
	}

	if (rp->read_state >= RS_LOAD_STARTED && !line_by_line) {
		logerr(rp->c.log, "Image is being loaded line-by-line. "
		                  "Cannot switch to full image.");
		return BMP_RESULT_ERROR;
	}

	if (rp->read_state < RS_DIMENSIONS_QUERIED) {
		logerr(rp->c.log, "Must query dimensions before loading image");
		return BMP_RESULT_ERROR;
	}

	if (rp->getinfo_return == BMP_RESULT_INSANE) {
		logerr(rp->c.log, "trying to load insanley large image");
		return BMP_RESULT_INSANE;
	}

	if (rp->read_state < RS_LOAD_STARTED && rp->is_icon) {
		if (!s_are_settings_icon_compatible(rp)) {
			logerr(rp->c.log, "Panic! Trying to load icon/pointer with incompatibele settings.\n");
			rp->read_state = RS_FATAL;
			rp->lasterr = BMP_ERR_INTERNAL;
			return BMP_RESULT_ERROR;
		}
	}

	if (!buffer) {
		logerr(rp->c.log, "Buffer pointer is NULL. (It may point to a NULL pointer, but must not itself be NULL)");
		return BMP_RESULT_ERROR;
	}

	if (line_by_line)
		buffer_size = (size_t) rp->width * rp->result_bytes_per_pixel;
	else
		buffer_size = rp->result_size;
	if (!*buffer) { /* no buffer supplied, we will allocate one */
		if (!(*buffer = malloc(buffer_size))) {
			logsyserr(rp->c.log, "allocating result buffer");
			return BMP_RESULT_ERROR;
		}
		rp->we_allocated_buffer = true;
	} else {
		rp->we_allocated_buffer = false;
	}

	if (rp->we_allocated_buffer || (rp->rle && (rp->undefined_mode == BMP_UNDEFINED_TO_ALPHA)))
		memset(*buffer, 0, buffer_size);

	if (rp->read_state < RS_LOAD_STARTED) {  /* either whole image or first line */
		if (rp->bytes_read > rp->fh->offbits) {
			logerr(rp->c.log, "Corrupt file, invalid offset to image bitmap data");
			goto abort;
		}
		/* skip to actual bitmap data: */
		/*if (!cm_gobble_up(rp, rp->fh->offbits - rp->bytes_read)) {*/
		if (fseek(rp->file, (long)rp->fh->offbits, SEEK_SET)) {
			logerr(rp->c.log, "while seeking start of bitmap data");
			goto abort;
		}
		rp->bytes_read += rp->fh->offbits - rp->bytes_read;
		rp->read_state = RS_LOAD_STARTED;
	}

	if (line_by_line)
		s_read_one_line(rp, *buffer);
	else
		s_read_whole_image(rp, *buffer);

	s_log_error_from_state(rp);
	if (s_stopping_error(rp)) {
		rp->truncated = true;
		rp->read_state = RS_FATAL;
		return BMP_RESULT_TRUNCATED;
	} else if (s_cont_error(rp))
		return BMP_RESULT_INVALID;

	return BMP_RESULT_OK;

abort:
	if (rp->we_allocated_buffer) {
		free(*buffer);
		*buffer = NULL;
	}
	rp->read_state = RS_FATAL;
	return BMP_RESULT_ERROR;
}

static bool s_are_settings_icon_compatible(BMPREAD_R rp)
{
	/* some catch-all sanity checks for icons/pointers. Strictly, these
	 * shouldn't be necessary, as they should have been caught already.
	 * If any of these fail, there is a bug somewhere else.
	 */

	if (rp->result_channels != 4 || rp->result_bitsperchannel != 8)
		return false;

	if (rp->result_format != BMP_FORMAT_INT)
		return false;

	if (rp->rle && (rp->undefined_mode != BMP_UNDEFINED_LEAVE))
		return false;

	if (!(rp->rle || (rp->ih->compression == BI_RGB)))
		return false;

	return true;
}

/********************************************************
 * 	apply_icon_alpha
 *******************************************************/

static void apply_icon_alpha(BMPREAD_R rp, int y, unsigned char *restrict line)
{
	for (int x = 0; x < rp->width; x++) {
		line[x * 4 + 3] = rp->icon_mono_and[(rp->height - y - 1) * rp->width + x];
	}
}


/********************************************************
 * 	s_read_whole_image
 *******************************************************/

static void s_read_whole_image(BMPREAD_R rp, unsigned char *restrict image)
{
	int          y, yoff = 1;
	size_t       linesize, real_y;

	linesize = (size_t) rp->width * rp->result_bytes_per_pixel;

	for (y = 0; y < rp->height; y += yoff) {
		real_y = (rp->orientation == BMP_ORIENT_TOPDOWN) ? y : rp->height-1-y;
		s_read_one_line(rp, image + real_y * linesize);
		if (rp->rle_eof || s_stopping_error(rp))
			break;
	}
}



/********************************************************
 * 	s_read_one_line
 *******************************************************/
static void s_read_monoicon_line(BMPREAD_R rp, unsigned char *restrict line, int y);

static void s_read_one_line(BMPREAD_R rp, unsigned char *restrict line)
{
	int yoff = 1;

	if (rp->ih->bitcount <= 8 || rp->rle) {

		if (rp->lbl_x >= rp->width)
			rp->lbl_x = 0;

		if (rp->lbl_file_y > rp->lbl_y) {
			; /* nothing to do, RLE skipped line */
		} else {
			if (rp->rle) {
				s_read_rle_line(rp, line, &rp->lbl_x, &yoff);
			} else {
				if (rp->ih->compression == BI_OS2_HUFFMAN) {
					s_read_huffman_line(rp, line);
				} else if (rp->is_icon && rp->icon_is_mono) {
					s_read_monoicon_line(rp, line, rp->lbl_y);
				} else {
					s_read_indexed_line(rp, line);
				}
			}

			if (!(rp->rle_eof || s_stopping_error(rp))) {
				if (yoff > rp->height - rp->lbl_file_y) {
					rp->invalid_delta = true;
				}
				rp->lbl_file_y += yoff;

			}
			if (rp->rle_eof)
				rp->lbl_file_y = rp->height;
		}
	} else {
		s_read_rgb_line(rp, line);
	}

	if (rp->is_icon) {
		apply_icon_alpha(rp, rp->lbl_y, line);
	}

	rp->lbl_y++;
	if (rp->lbl_y >= rp->height) {
		rp->read_state = RS_LOAD_DONE;
	}
}


/********************************************************
 * 	s_read_monoicon_line
 *******************************************************/

static void s_read_monoicon_line(BMPREAD_R rp, unsigned char *restrict line, int y)
{
	for (int x = 0; x < rp->width; x++) {
		for (int c = 0; c < 3; c++) {
			line[rp->result_bytes_per_pixel * x + c] =
			            rp->icon_mono_xor[(rp->height - y - 1) * rp->width + x];
		}
	}
}


/********************************************************
 * 	s_read_rgb_line
 *******************************************************/

static inline bool     s_read_rgb_pixel(BMPREAD_R rp, union Pixel *restrict px);
static inline double   s_s2_13_to_float(uint16_t s2_13);
static inline uint16_t s_float_to_s2_13(double d);
static inline double   s_int_to_float(unsigned long ul, int bits);
static inline void s_convert64(uint16_t val64[static 4]);
static inline void s_convert64srgb(uint16_t val64[static 4]);
static inline double   s_srgb_gamma_float(double d);
static inline uint16_t s_srgb_gamma_s2_13(uint16_t s2_13);

static void s_read_rgb_line(BMPREAD_R rp, unsigned char *restrict line)
{
	int           i, x, padding;
	union Pixel   px;
	size_t        offs;
	int           bits = rp->result_bitsperchannel;
	uint32_t      pxval;
	double        d;
	uint16_t      s2_13;

	for (x = 0; x < rp->width; x++) {

		if (!s_read_rgb_pixel(rp, &px)) {
			return;
		}

		offs = x * rp->result_channels;

		switch (rp->result_format) {
		case BMP_FORMAT_INT:
			for (i = 0; i < rp->result_channels; i++) {
				pxval = s_scaleint(px.value[i], rp->cmask.bits.value[i], bits);
				switch(bits) {
				case 8:
					((unsigned char*)line)[offs + i] = pxval;
					break;
				case 16:
					((uint16_t*)line)[offs + i] = pxval;
					break;
				case 32:
					((uint32_t*)line)[offs + i] = pxval;
					break;
				default:
					logerr(rp->c.log, "Waaaaaaaaaaaaaah!");
					rp->panic = true;
					return;
				}
			}
			if (rp->ih->bitcount == 64) {
				switch (rp->conv64) {
				case BMP_CONV64_SRGB:
					s_convert64srgb(&((uint16_t*)line)[offs]);
					break;
				case BMP_CONV64_LINEAR:
					s_convert64(&((uint16_t*)line)[offs]);
					break;
				default:
					break;
				}
			}
			break;

		case BMP_FORMAT_FLOAT:
			if (rp->ih->bitcount == 64) {
				for (i = 0; i < rp->result_channels; i++) {
					d = s_s2_13_to_float(px.value[i]);
					if (i < 3 && rp->conv64 == BMP_CONV64_SRGB)
						d = s_srgb_gamma_float(d);
					((float*)line)[offs+i] = (float) d;
				}
			} else {
				for (i = 0; i < rp->result_channels; i++) {
					d = s_int_to_float(px.value[i], rp->cmask.bits.value[i]);
					((float*)line)[offs + i] = (float) d;
				}
			}
			break;

		case BMP_FORMAT_S2_13:
			if (rp->ih->bitcount == 64) {
				for (i = 0; i < rp->result_channels; i++) {
					s2_13 = px.value[i];
					if (i < 3 && rp->conv64 == BMP_CONV64_SRGB)
						s2_13 = s_srgb_gamma_s2_13(s2_13);
					((uint16_t*)line)[offs+i] = s2_13;
				}
			} else {
				for (i = 0; i < rp->result_channels; i++) {
					d = s_int_to_float(px.value[i], rp->cmask.bits.value[i]);
					((uint16_t*)line)[offs+i] = s_float_to_s2_13(d);
				}
			}
			break;

		default:
			logerr(rp->c.log, "Unknown format");
			rp->panic = true;
			return;
		}
	}
	padding = cm_align4padding(((uint64_t)rp->width * rp->ih->bitcount + 7) / 8);
	if (!cm_gobble_up(rp, padding)) {
		s_set_file_error(rp);
		return;
	}
	rp->bytes_read += padding;
}


static inline double s_s2_13_to_float(uint16_t s2_13)
{
	int16_t s16;

	if (s2_13 >= 0x8000)
		s16 = (int16_t)(s2_13 - 0x8000) - 0x7fff - 1;
	else
		s16 = (int16_t)s2_13;

	return s16 / 8192.0;
}


static inline uint16_t s_float_to_s2_13(double d)
{
	d = MIN(d, 3.99987793);
	d = MAX(-4.0, d);
	return (uint16_t) ((int)round(d * 8192.0) & 0xffff);
}


static inline double s_int_to_float(unsigned long ul, int bits)
{
	return (double) ul / ((1ULL << bits) - 1);
}


static inline void s_convert64(uint16_t val64[static 4])
{
	/* convert the s2.13 values of a 64bit BMP to plain old 16bit integers.
	 * Values are clipped to the representable [0..1] range
	 */

	double d;

	for (int i = 0; i < 4; i++) {
		d = s_s2_13_to_float(val64[i]);
		d = MAX(0.0, d);
		d = MIN(d, 1.0);
		val64[i] = (uint16_t) (d * 0xffff + 0.5);
	}
}


static inline void s_convert64srgb(uint16_t val64[static 4])
{
	/* Same as s_convert64(), but also apply sRGB gamma. */

	double  d;

	for (int i = 0; i < 4; i++) {
		d = s_s2_13_to_float(val64[i]);
		d = MAX(0.0, d);
		d = MIN(d, 1.0);

		/* apply gamma to RGB channels, but not to alpha channel */
		if (i < 3)
			d = s_srgb_gamma_float(d);

		val64[i] = (uint16_t) (d * 0xffff + 0.5);
	}
}



static inline double s_srgb_gamma_float(double d)
{
	if (d <= 0.0031308)
		d = 12.92 * d;
	else
		d = 1.055 * pow(d, 1.0/2.4) - 0.055;
	return d;
}


static inline uint16_t s_srgb_gamma_s2_13(uint16_t s2_13)
{
	double  d;

	d = s_s2_13_to_float(s2_13);
	d = s_srgb_gamma_float(d);
	return s_float_to_s2_13(d);
}



/********************************************************
 * 	s_read_rgb_pixel
 *******************************************************/

static inline bool s_read_rgb_pixel(BMPREAD_R rp, union Pixel *restrict px)
{
	unsigned long long v;
	int                i, byte;

	v = 0;
	for (i = 0; i < rp->ih->bitcount; i+=8 ) {
		if (EOF == (byte = s_read_one_byte(rp))) {
			s_set_file_error(rp);
			return false;
		}
		v |= ((unsigned long long)byte) << i;
	}

	px->red   = (unsigned int) ((v & rp->cmask.mask.red)   >> rp->cmask.shift.red);
	px->green = (unsigned int) ((v & rp->cmask.mask.green) >> rp->cmask.shift.green);
	px->blue  = (unsigned int) ((v & rp->cmask.mask.blue)  >> rp->cmask.shift.blue);
	if (rp->has_alpha)
		px->alpha = (unsigned int) ((v & rp->cmask.mask.alpha) >> rp->cmask.shift.alpha);
	else
		px->alpha = (1ULL<<rp->result_bitsperchannel) - 1;

	return true;
}



/********************************************************
 * 	s_read_indexed_line
 * - 1/2/4/8 bits non-RLE indexed
 *******************************************************/
struct Buffer32 {
	uint32_t buffer;
	int      n;
};

static inline bool s_buffer32_fill(BMPREAD_R rp, struct Buffer32 *restrict buf)
{
	int byte;

	memset(buf, 0, sizeof *buf);

	for (int i = 0; i < 4; i++) {
		if (EOF == (byte = s_read_one_byte(rp))) {
			s_set_file_error(rp);
			break;
		}
		buf->buffer |= ((uint32_t)byte) << (8 * (3 - i));
		buf->n += 8;
	}

	return buf->n > 0;
}


static inline uint32_t s_buffer32_bits(struct Buffer32 *restrict buf, int nbits)
{
	uint32_t result;

	assert(nbits == 1 || nbits == 2 || nbits == 4 || nbits == 8);
	assert(nbits <= buf->n);

	result = buf->buffer >> (32 - nbits);
	buf->buffer = (buf->buffer << nbits) & 0xffffffffUL;
	buf->n -= nbits;

	return result;
}

static void s_read_indexed_line(BMPREAD_R rp, unsigned char *restrict line)
{
	int             x = 0, v;
	struct Buffer32 buffer;
	size_t          offs;
	int             rgba[4] = { 0 };
	bool            done = false;

	/* the buffer size of 32 bits takes care of padding bytes */

	while (!done && s_buffer32_fill(rp, &buffer)) {

		while (buffer.n >= rp->ih->bitcount) {

			v = (int) s_buffer32_bits(&buffer, rp->ih->bitcount);

			if (v >= rp->palette->numcolors) {
				v = rp->palette->numcolors - 1;
				rp->invalid_index = true;
			}

			offs = (size_t) x * rp->result_bytes_per_pixel;
			if (rp->result_indexed) {
				line[offs] = v;
			} else {
				rgba[0] = rp->palette->color[v].red;
				rgba[1] = rp->palette->color[v].green;
				rgba[2] = rp->palette->color[v].blue;
				s_int8_to_result_format(rp, rgba, line + offs);
			}

			if (++x == rp->width) {
				done = true;
				break;      /* discarding rest of buffer == padding */
			}
		}
	}
}



/********************************************************
 * 	s_read_rle_line
 * - 4/8/24 bit RLE
 *******************************************************/

static void s_read_rle_line(BMPREAD_R rp, unsigned char *restrict line,
                               int *restrict x, int *restrict yoff)
{
	int     left_in_run = 0;
	bool    repeat = false, padding = false, odd = false;
	int     right, up;
	int     v, r = 0, g = 0, b = 0;
	int     rgba[4] = { 0, 0, 0, 0xff };
	size_t  offs;
	int     bits = rp->ih->bitcount;

	if (!(bits == 4 || bits == 8 || bits == 24)) {
		rp->panic = true;
		return;
	}

	*yoff = 1;
	while(1)  {
		if (left_in_run > 0) {
			left_in_run--;

			/* literal run */
			if (!repeat && !(bits == 4 && odd)) {
			        /* for 24-bit RLE, b holds blue value,            */
			        /* for 4/8-bit RLE, b holds index value(s)        */
			        /* 4-bit RLE only needs new byte every other time */
				if (EOF == (b = s_read_one_byte(rp)) ||
				    (bits == 24 && EOF == (g = s_read_one_byte(rp))) ||
				    (bits == 24 && EOF == (r = s_read_one_byte(rp)))) {
					s_set_file_error(rp);
					break;
				}
			}
			if (left_in_run == 0 && padding) {
				if (EOF == s_read_one_byte(rp)) {
					s_set_file_error(rp);
					break;
				}
			}

			offs = (size_t) *x * rp->result_bytes_per_pixel;
			switch (bits) {
			case 24:
				rgba[0] = r;
				rgba[1] = g;
				rgba[2] = b;
				s_int8_to_result_format(rp, rgba, line + offs);
				break;
			case 4:
			case 8:
				if (bits == 8)
					v = b;
				else {
					v = odd ? b & 0x0f : (b >> 4) & 0x0f;
					odd = !odd;
				}
				if (v >= rp->palette->numcolors) {
					v = rp->palette->numcolors - 1;
					rp->invalid_index = true;
				}
				if (rp->result_indexed) {
					line[offs] = v;
				} else {
					rgba[0] = rp->palette->color[v].red;
					rgba[1] = rp->palette->color[v].green;
					rgba[2] = rp->palette->color[v].blue;
					s_int8_to_result_format(rp, rgba, line + offs);
				}
				break;
			}

			*x += 1;
			if (*x >= rp->width) {
				rp->rle_eol = false; /* EOL detected by width, not by RLE-code */
				if (left_in_run) {
					rp->invalid_overrun = true;
				}
				break;
			}
			continue;
		}

		/* not in a literal or RLE run, start afresh */
		if (EOF == (v = s_read_one_byte(rp))) {
			s_set_file_error(rp);
			break;
		}

		/* start RLE run */
		if (v > 0) {
			if (EOF == (b = s_read_one_byte(rp)) ||
			    (bits == 24 && EOF == (g = s_read_one_byte(rp))) ||
			    (bits == 24 && EOF == (r = s_read_one_byte(rp)))) {
				s_set_file_error(rp);
				break;
			}
			padding = false;
			odd = false;
			left_in_run = v;
			repeat = true;
			continue;
		}

		/* v == 0: escape, look at next byte */
		if (EOF == (v = s_read_one_byte(rp))) {
			s_set_file_error(rp);
			break;
		}

		/* start literal run */
		if (v > 2) {
			left_in_run = v;
			repeat = false;

			switch (bits) {
			case 8:
			case 24:
				padding = v & 0x01 ? true : false;
				break;
			case 4:
				if ((v+1)%4 >= 2)
					padding = true;
				else
					padding = false;
				break;
			}
			odd = false;
			continue;
		}

		/* end of line.  */
		if (v == 0) {
			if (*x != 0 || rp->rle_eol) {
				*x = rp->width;
				rp->rle_eol = true;
				break;
			}
			continue;
		}

		/* end of bitmap */
		if (v == 1) {
			rp->rle_eof = true;
			break;
		}

		/* delta. */
		if (v == 2) {
			if (EOF == (right = s_read_one_byte(rp)) || EOF == (up = s_read_one_byte(rp))) {
				s_set_file_error(rp);
				break;
			}
			if (right >= rp->width - *x) {
				rp->invalid_delta = true;
				break;
			}
			*x += right;
			if (up > 0) {
				*yoff = up;
				break;
			}
			continue;
		}

		logerr(rp->c.log, "Should never get here! (x=%d, byte=%d)", (int) *x, (int) v);
		rp->panic = true;
		break;
	}
}


/********************************************************
 * 	s_read_huffman_line
 *******************************************************/
static bool s_huff_skip_eol(BMPREAD_R rp);
static bool s_huff_find_eol(BMPREAD_R rp);

static void s_read_huffman_line(BMPREAD_R rp, unsigned char *restrict line)
{
	size_t   offs;
	int      x = 0, runlen;
	int      rgba[4] = { 0 };
	bool     black = false;

	while (x < rp->width)  {
		huff_fillbuf(rp);

		if (rp->hufbuf_len == 0)
			break;

		if ((rp->hufbuf & 0xff000000UL) == 0) {
			if (!s_huff_skip_eol(rp)) {
				rp->truncated = true;
				break;
			}

			if (x == 0) /* ignore eol at start of line */
				continue;
			break;
		}

		runlen = huff_decode(rp, black);
		if (runlen == -1) {
			/* code was invalid, look for next eol */
			rp->lasterr |= BMP_ERR_PIXEL;
			if (!s_huff_find_eol(rp))
				rp->truncated = true;
			break;
		}

		if (runlen > rp->width - x) {
			rp->lasterr |= BMP_ERR_PIXEL;
			runlen = rp->width - x;
		}

		for (int i = 0; i < runlen; i++, x++) {
			offs = (size_t) x * rp->result_bytes_per_pixel;
			if (rp->result_indexed) {
				line[offs] = black ^ rp->c.huffman_black_is_zero;
			} else {
				rgba[0] = rp->palette->color[black ^ rp->c.huffman_black_is_zero].red;
				rgba[1] = rp->palette->color[black ^ rp->c.huffman_black_is_zero].green;
				rgba[2] = rp->palette->color[black ^ rp->c.huffman_black_is_zero].blue;
				s_int8_to_result_format(rp, rgba, line + offs);
			}
		}
		black = !black;
	}
}


static bool s_huff_skip_eol(BMPREAD_R rp)
{
	huff_fillbuf(rp);
	while (rp->hufbuf_len > 0) {
		if (rp->hufbuf == 0) {
			rp->hufbuf_len = 0;
			huff_fillbuf(rp);
			continue;
		}
		while ((rp->hufbuf & 0x80000000UL) == 0) {
			rp->hufbuf <<= 1;
			rp->hufbuf_len--;
		}
		rp->hufbuf <<= 1;
		rp->hufbuf_len--;
		return true;
	}
	return false;
}



static bool s_huff_find_eol(BMPREAD_R rp)
{
	/* look for the next full 12-bit eol sequence,
	* discard anything else
	*/
	huff_fillbuf (rp);
	while (rp->hufbuf_len > 11)
	{
		if ((rp->hufbuf & 0xffe00000UL) == 0) {
			rp->hufbuf <<= 11;
			rp->hufbuf_len -= 11;
			return s_huff_skip_eol (rp);
		}
		rp->hufbuf <<= 1;
		rp->hufbuf_len -= 1;
		if (rp->hufbuf_len < 12)
			huff_fillbuf (rp);
	}
	return false;
}



/********************************************************
 * s_int8_to_result_format
 * convert 8-bit integer values (from indexed images)
 * to selected number format.
 *******************************************************/

static inline void s_int8_to_result_format(BMPREAD_R rp, const int *restrict fromrgba, unsigned char *restrict px)
{

	for (int c = 0; c < rp->result_channels; c++) {
		switch (rp->result_format) {
		case BMP_FORMAT_INT:
			assert(rp->result_bitsperchannel == 8);
			px[c] = fromrgba[c];
			break;
		case BMP_FORMAT_FLOAT:
			((float*)px)[c] = s_int_to_float(fromrgba[c], 8);
			break;
		case BMP_FORMAT_S2_13:
			((uint16_t*)px)[c] = s_float_to_s2_13(s_int_to_float(fromrgba[c], 8));
			break;
		default:
#ifdef DEBUG
			logerr(rp->c.log, "Unexpected result format %d", rp->result_format);
			exit(1);
#endif
			break;
		}
	}
}



/********************************************************
 * 	s_set_file_error
 *******************************************************/

static void s_set_file_error(BMPREAD_R rp)
{
	if (feof(rp->file))
		rp->file_eof = true;
	else
		rp->file_err = true;
}



/********************************************************
 * 	s_log_error_from_state
 *******************************************************/

static void s_log_error_from_state(BMPREAD_R rp)
{
	if (rp->panic)
		logerr(rp->c.log, "An internal error occured.");
	if (rp->file_eof)
		logerr(rp->c.log, "Unexpected end of file.");
	if (rp->file_err)
		logsyserr(rp->c.log, "While reading file");
	if (rp->invalid_index)
		logerr(rp->c.log, "File contained invalid color index.");
	if (rp->invalid_delta)
		logerr(rp->c.log, "Invalid delta pointing outside image area.");
	if (rp->invalid_overrun)
		logerr(rp->c.log, "RLE data overrunning image area.");
	if (rp->truncated)
		logerr(rp->c.log, "Image was truncated.");
}



/********************************************************
 * 	s_cont_error
 *******************************************************/

static bool s_cont_error(BMPREAD_R rp)
{
	if (rp->invalid_index || rp->invalid_overrun) {
		return true;
	}
	return false;
}



/********************************************************
 * 	s_stopping_error
 *******************************************************/

static bool s_stopping_error(BMPREAD_R rp)
{
	if (rp->truncated     ||
	    rp->invalid_delta ||
	    rp->file_err      ||
	    rp->file_eof      ||
	    rp->panic) {
		return true;
	}
	return false;
}



/********************************************************
 * 	s_read_one_byte
 *******************************************************/

static inline int s_read_one_byte(BMPREAD_R rp)
{
	int byte;
	if (EOF != (byte = getc(rp->file)))
		rp->bytes_read++;
	return byte;
}



/********************************************************
 * 	s_scaleint
 *******************************************************/

static inline unsigned long s_scaleint(unsigned long val, int frombits, int tobits)
{
	return (unsigned long) ((double) val * ((1ULL<<tobits)-1) / ((1ULL<<frombits)-1) + 0.5);
}
