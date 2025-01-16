/* bmplib - bmp-read-loadimage.c
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
#include <stdbool.h>
#include <math.h>

#define BMPLIB_LIB

#include "config.h"
#include "bmplib.h"
#include "logging.h"
#include "bmp-common.h"
#include "huffman.h"
#include "bmp-read.h"
#include "reversebits.h"


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
static inline void s_int_to_result_format(BMPREAD_R rp, int frombits, unsigned char *restrict px);

static BMPRESULT s_load_image_or_line(BMPREAD_R rp, unsigned char **restrict buffer, bool line_by_line);
static void s_read_rgb_line(BMPREAD_R rp, unsigned char *restrict line);
static void s_read_indexed_line(BMPREAD_R rp, unsigned char *restrict line);
static void s_read_rle_line(BMPREAD_R rp, unsigned char *restrict line,
                               int *restrict x, int *restrict yoff);
static void s_read_huffman_line(BMPREAD_R rp, unsigned char *restrict line);

_Static_assert(sizeof(float) == 4, "sizeof(float) must be 4. Cannot build bmplib.");
_Static_assert(sizeof(int) >= 4, "int must be at least 32bit. Cannot build bmplib.");


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

	logreset(rp->log); /* otherwise we might accumulate thousands  */
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

	if (!(rp->getinfo_called && (rp->getinfo_return == BMP_RESULT_OK))) {
		if (rp->getinfo_return == BMP_RESULT_INSANE) {
			logerr(rp->log, "trying to load insanley large image");
			return BMP_RESULT_INSANE;
		}
		logerr(rp->log, "getinfo had failed, cannot load image");
		return BMP_RESULT_ERROR;
	}

	if (rp->image_loaded) {
		logerr(rp->log, "Cannot load image more than once!");
		return BMP_RESULT_ERROR;
	}

	if (rp->line_by_line && !line_by_line) {
		logerr(rp->log, "Image is being loaded line-by-line. "
		                "Cannot switch to full image.");
		return BMP_RESULT_ERROR;
	}

	if (!rp->dimensions_queried) {
		logerr(rp->log, "must query dimensions before loading image");
		return BMP_RESULT_ERROR;
	}

	if (!buffer) {
		logerr(rp->log, "buffer pointer is NULL");
		return BMP_RESULT_ERROR;
	}

	if (line_by_line)
		buffer_size = (size_t) rp->width * rp->result_bytes_per_pixel;
	else
		buffer_size = rp->result_size;
	if (!*buffer) { /* no buffer supplied, we will allocate one */
		if (!(*buffer = malloc(buffer_size))) {
			logsyserr(rp->log, "allocating result buffer");
			return BMP_RESULT_ERROR;
		}
		rp->we_allocated_buffer = true;
	} else {
		rp->we_allocated_buffer = false;
	}

	if (rp->we_allocated_buffer || (rp->rle && (rp->undefined_mode == BMP_UNDEFINED_TO_ALPHA)))
		memset(*buffer, 0, buffer_size);

	if (!line_by_line)
		rp->image_loaded = true; /* point of no return */

	if (!rp->line_by_line) {  /* either whole image or first line */
		if (rp->bytes_read > rp->fh->offbits) {
			logerr(rp->log, "Corrupt file");
			goto abort;
		}
		/* skip to actual bitmap data: */
		if (!cm_gobble_up(rp, rp->fh->offbits - rp->bytes_read)) {
			logerr(rp->log, "while seeking start of bitmap data");
			goto abort;
		}
		rp->bytes_read += rp->fh->offbits - rp->bytes_read;
	}

	if (line_by_line) {
		rp->line_by_line = true;  /* don't set this earlier, or we won't */
		                          /* be able to identify first line      */
		s_read_one_line(rp, *buffer);
	} else {
		s_read_whole_image(rp, *buffer);
	}

	s_log_error_from_state(rp);
	if (s_stopping_error(rp)) {
		rp->truncated = true;
		rp->image_loaded = true;
		return BMP_RESULT_TRUNCATED;
	} else if (s_cont_error(rp))
		return BMP_RESULT_INVALID;

	return BMP_RESULT_OK;

abort:
	if (rp->we_allocated_buffer) {
		free(*buffer);
		*buffer = NULL;
	}
	rp->image_loaded = true;
	return BMP_RESULT_ERROR;
}



/********************************************************
 * 	s_read_whole_image
 *******************************************************/

static void s_read_whole_image(BMPREAD_R rp, unsigned char *restrict image)
{
	int          y, yoff = 1;
	size_t       linesize, real_y;

	linesize = (size_t) rp->width * rp->result_bytes_per_pixel;

	for (y = 0; y < (int) rp->height; y += yoff) {
		real_y = (rp->orientation == BMP_ORIENT_TOPDOWN) ? y : rp->height-1-y;
		s_read_one_line(rp, image + real_y * linesize);
		if (rp->rle_eof || s_stopping_error(rp))
			break;
	}
}



/********************************************************
 * 	s_read_one_line
 *******************************************************/

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
				} else {
					s_read_indexed_line(rp, line);
				}
			}

			if (!(rp->rle_eof || s_stopping_error(rp))) {
				if (yoff > (int) rp->height - rp->lbl_file_y) {
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

	rp->lbl_y++;
	if (rp->lbl_y >= (int) rp->height) {
		rp->image_loaded = true;
	}
}



/********************************************************
 * 	s_read_rgb_line
 *******************************************************/

static inline bool     s_read_rgb_pixel(BMPREAD_R rp, union Pixel *restrict px);
static inline double   s_s2_13_to_float(uint16_t s2_13);
static inline double   s_int_to_float(unsigned long ul, int bits);
static inline void     s_convert64(uint16_t *val64);
static inline void     s_convert64srgb(uint16_t *val64);
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
					logerr(rp->log, "Waaaaaaaaaaaaaah!");
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
					d = d * 8192.0 + 0.5;
					((uint16_t*)line)[offs+i] = (uint16_t) d;
				}
			}
			break;

		default:
			logerr(rp->log, "Unknown format");
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
	return (int16_t)s2_13 / 8192.0;
}


static inline double s_int_to_float(unsigned long ul, int bits)
{
	return (double) ul / ((1ULL<<bits)-1);
}


static inline void s_convert64(uint16_t *val64)
{
	double d;

	for (int i = 0; i < 4; i++) {
		d = (double) (int16_t) val64[i];
		d /= 8192.0;
		d = MAX(0.0, d);
		d = MIN(d, 1.0);
		val64[i] = (uint16_t) (d * 0xffff + 0.5);
	}
}


static inline void s_convert64srgb(uint16_t *val64)
{
	double  d;

	for (int i = 0; i < 4; i++) {
		d = (double) (int16_t) val64[i];
		d /= 8192.0;
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
	double d;

	d = (int16_t)s2_13 / 8192.0;
	d = s_srgb_gamma_float(d);
	return (uint16_t) (((int)(d * 8192.0 + 0.5)) & 0xffff);
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
static inline bool s_read_n_bytes(BMPREAD_R rp, int n, unsigned long *restrict buff);
static inline unsigned long s_bits_from_buffer(unsigned long buf, int size,
                                              int nbits, int used_bits);

static void s_read_indexed_line(BMPREAD_R rp, unsigned char *restrict line)
{
	int           bits_used, buffer_size, x = 0, v;
	bool          done = false;
	unsigned long buffer;
	size_t        offs;

	/* setting the buffer size to the alignment takes care of padding bytes */
	buffer_size = 32;

	while (!done && s_read_n_bytes(rp, buffer_size / 8, &buffer)) {

		bits_used = 0;
		while (bits_used < buffer_size) {

			/* mask out the relevant bits for current pixel */
			v = (int) s_bits_from_buffer(buffer, buffer_size,
			                             rp->ih->bitcount, bits_used);
			bits_used += rp->ih->bitcount;

			if (v >= rp->palette->numcolors) {
				v = rp->palette->numcolors - 1;
				rp->invalid_index = true;
			}

			offs = (size_t) x * rp->result_bytes_per_pixel;
			if (rp->result_indexed) {
				line[offs] = v;
			} else {
				line[offs]   = rp->palette->color[v].red;
				line[offs+1] = rp->palette->color[v].green;
				line[offs+2] = rp->palette->color[v].blue;
				s_int_to_result_format(rp, 8, line + offs);
			}
			if (++x == rp->width) {
				done = true;
				break;      /* discarding rest of buffer == padding */
			}
		}
	}
}



/********************************************************
 * 	s_read_n_bytes
 *******************************************************/

static inline bool s_read_n_bytes(BMPREAD_R rp, int n, unsigned long *restrict buff)
{
	int byte;

	*buff = 0;
	while (n--) {
		if (EOF == (byte = s_read_one_byte(rp))) {
			s_set_file_error(rp);
			return false;
		}
		*buff <<= 8;
		*buff |= byte;
	}
	return true;
}



/********************************************************
 * 	s_bits_from_buffer
 *******************************************************/

static inline unsigned long s_bits_from_buffer(unsigned long buf, int size,
                                              int nbits, int used_bits)
{
	unsigned long mask;
	int           shift;

	shift = size - (nbits + used_bits);

	mask  = (1U << nbits) - 1;
	mask <<= shift;

	buf   &= mask;
	buf  >>= shift;

	return buf;
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
			if ((rp->undefined_mode == BMP_UNDEFINED_TO_ALPHA) && !rp->result_indexed)
				line[offs+3] = 0xff; /* set alpha to 1.0 for defined pixels */
			switch (bits) {
			case 24:
				line[offs]   = r;
				line[offs+1] = g;
				line[offs+2] = b;
				s_int_to_result_format(rp, 8, line + offs);
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
					line[offs]   = rp->palette->color[v].red;
					line[offs+1] = rp->palette->color[v].green;
					line[offs+2] = rp->palette->color[v].blue;
					s_int_to_result_format(rp, 8, line + offs);
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

		logerr(rp->log, "Should never get here! (x=%d, byte=%d)", (int) *x, (int) v);
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
	bool     black = false;

	while (x < rp->width)  {
		huff_fillbuf(rp);

		if (rp->hufbuf_len == 0)
			break;

		if ((rp->hufbuf & 0x00ff) == 0) {
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
				line[offs] = black;
			} else {
				line[offs]   = rp->palette->color[black].red;
				line[offs+1] = rp->palette->color[black].green;
				line[offs+2] = rp->palette->color[black].blue;
				s_int_to_result_format(rp, 8, line + offs);
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
		while ((rp->hufbuf & 0x0001) == 0) {
			rp->hufbuf >>= 1;
			rp->hufbuf_len--;
		}
		rp->hufbuf >>= 1;
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
		if ((rp->hufbuf & 0x07ff) == 0) {
			rp->hufbuf >>= 11;
			rp->hufbuf_len -= 11;
			return s_huff_skip_eol (rp);
		}
		rp->hufbuf >>= 1;
		rp->hufbuf_len -= 1;
		if (rp->hufbuf_len < 12)
			huff_fillbuf (rp);
	}
	return false;
}



/********************************************************
 * 	s_int_to_result_format
 * convert integer values in image buffer
 * to selected number format.
 *******************************************************/

static inline void s_int_to_result_format(BMPREAD_R rp, int frombits, unsigned char *restrict px)
{
	int      c;
	uint32_t v;

	if (rp->result_format == BMP_FORMAT_INT)
		return;
#ifdef DEBUG
	if (frombits > rp->result_bitsperchannel) {
		printf("This is bad, frombits must be <= result_bitsperchannel");
		exit(1);
	}
#endif
	for (c = rp->result_channels - 1; c >= 0; c--) {
		/* going backwards because we are converting in place and
		 * always growing, never shrinking
		 */
		switch (frombits) {
		case 8:
			v = px[c];
			break;
		case 16:
			v = ((uint16_t*)px)[c];
			break;
		case 32:
			v = ((uint32_t*)px)[c];
			break;
		default:
#ifdef DEBUG
			printf("Invalid pixel int size %d!!!", frombits);
			exit(1);
#endif
			break;
		}
		switch (rp->result_format) {
		case BMP_FORMAT_FLOAT:
			((float*)px)[c] = (float) ((double) v / ((1ULL<<frombits)-1));
			break;
		case BMP_FORMAT_S2_13:
			((uint16_t*)px)[c] = (uint16_t) ((double) v / ((1ULL<<frombits)-1) * 8192.0 + 0.5);
			break;
		default:
#ifdef DEBUG
			logerr(rp->log, "Unexpected result format %d", rp->result_format);
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
		logerr(rp->log, "An internal error occured.");
	if (rp->file_eof)
		logerr(rp->log, "Unexpected end of file.");
	if (rp->file_err)
		logsyserr(rp->log, "While reading file");
	if (rp->invalid_index)
		logerr(rp->log, "File contained invalid color index.");
	if (rp->invalid_delta)
		logerr(rp->log, "Invalid delta pointing outside image area.");
	if (rp->invalid_overrun)
		logerr(rp->log, "RLE data overrunning image area.");
	if (rp->truncated)
		logerr(rp->log, "Image was truncated.");
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
