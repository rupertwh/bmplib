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
#include <errno.h>

#include "config.h"
#include "bmplib.h"
#include "logging.h"
#include "bmp-common.h"
#include "bmp-read.h"


static BMPRESULT s_load_image(BMPREAD_R rp, char **restrict buffer, int line_by_line);
static void s_read_rgb_pixels(BMPREAD_R rp, char *restrict image);
static int s_read_rgb_line(BMPREAD_R rp, void* restrict line);
static inline int s_get_next_rgb(BMPREAD_R rp, union Pixel* restrict px);

static void s_read_indexed_or_rle_image(BMPREAD_R rp, void *restrict image);
static inline unsigned int s_bits_from_buffer(unsigned int buf, int size,
                                              int nbits, int used_bits);

static inline int s_read_n_bytes(FILE *file, int n, unsigned int* restrict buff, LOG log);

static inline unsigned long s_scaleint(unsigned long val, int frombits, int tobits);
static void s_set_file_error(BMPREAD_R rp);



/********************************************************
 * 	bmpread_load_line
 *******************************************************/

EXPORT_VIS BMPRESULT bmpread_load_line(BMPHANDLE h, char **restrict buffer)
{
	BMPREAD rp;

	if (!(h && cm_check_is_read_handle(h)))
		return BMP_RESULT_ERROR;
	rp = (BMPREAD)(void*)h;

	logreset(rp->log); /* otherwise we might accumulate thousands */
	                   /* of log entries with large corrupted     */
	                   /* images                                  */

	return s_load_image(rp, buffer, TRUE);
}



/********************************************************
 * 	bmpread_load_image
 *******************************************************/

EXPORT_VIS BMPRESULT bmpread_load_image(BMPHANDLE h, char **restrict buffer)
{
	BMPREAD rp;

	if (!(h && cm_check_is_read_handle(h)))
		return BMP_RESULT_ERROR;
	rp = (BMPREAD)(void*)h;

	return s_load_image(rp, buffer, FALSE);
}


static void s_read_whole_image(BMPREAD_R rp, void* restrict image);
static void s_read_one_line(BMPREAD_R rp, void* restrict image);
static void s_log_error_from_state(BMPREAD_R rp);


/********************************************************
 * 	s_load_image
 *******************************************************/

static BMPRESULT s_load_image(BMPREAD_R rp, char **restrict buffer, int line_by_line)
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

#ifdef NEVER  /* gets too complicated with querying single dim values */
	if (!rp->dimensions_queried) {
		logerr(rp->log, "must query dimensions before loading image");
		return BMP_RESULT_ERROR;
	}
#endif

	if (!buffer) {
		logerr(rp->log, "buffer pointer is NULL");
		return BMP_RESULT_ERROR;
	}

	if (!*buffer) { /* no buffer supplied, we will allocate one */
		if (line_by_line)
			buffer_size = rp->width * rp->result_bytes_per_pixel;
		else
			buffer_size = rp->result_size;
		if (!(*buffer = malloc(buffer_size))) {
			logsyserr(rp->log, "allocating result buffer");
			return BMP_RESULT_ERROR;
		}
		rp->we_allocated_buffer = TRUE;
		memset(*buffer, 0, buffer_size);
	}
		else rp->we_allocated_buffer = FALSE;

	if (!line_by_line)
		rp->image_loaded = TRUE; /* point of no return */

	/* skip to actual data: */

#ifdef NEVER
	printf("bytes read: %d  Start of data: %d\n", (int)rp->bytes_read, (int)rp->fh->offbits);
#endif
#ifdef NEVER
	if (line_by_line)
		{ printf("%d ", rp->lbl_y); fflush(stdout); }
#endif

	if (!rp->line_by_line) {  /* either whole image ot first line */

		if (rp->bytes_read > rp->fh->offbits) {
			logerr(rp->log, "Panic! Corrupt file?");
			goto abort;
		}
		if (!cm_gobble_up(rp->file, rp->fh->offbits - rp->bytes_read, rp->log)) {
			logerr(rp->log, "while seeking start of bitmap data");
			goto abort;
		}
	}

	if (rp->ih->compression == BI_RLE8 ||
	    rp->ih->compression == BI_RLE4 ||
	    rp->ih->compression == BI_OS2_RLE24)
		rp->idx_state_rle = TRUE;

	if (line_by_line) {
		rp->line_by_line = TRUE;
		s_read_one_line(rp, *buffer);
	}
	else {
		s_read_whole_image(rp, *buffer);
	}

	s_log_error_from_state(rp);
	if (rp->truncated || rp->invalid_delta || rp->file_err || rp->file_eof || rp->panic) {
		rp->image_loaded = TRUE;
		return BMP_RESULT_TRUNCATED;
	}
	else if (rp->invalid_pixels)
		return BMP_RESULT_INVALID;

	return BMP_RESULT_OK;

abort:
	if (rp->we_allocated_buffer) {
		free(*buffer);
		*buffer = NULL;
	}
	rp->image_loaded = TRUE;
	return BMP_RESULT_ERROR;
}

static void s_log_error_from_state(BMPREAD_R rp)
{
	if (rp->panic)
		logerr(rp->log, "An internal error occured.");
	if (rp->file_eof)
		logerr(rp->log, "Unexpected end of file.");
	if (rp->file_err)
		logsyserr(rp->log, "While reading file");
	if (rp->invalid_pixels)
		logerr(rp->log, "File contained invalid data.");
	if (rp->invalid_delta)
		logerr(rp->log, "Invalid delta pointing outside image area.");
	if (rp->truncated)
		logerr(rp->log, "Image was truncated.");
}



static void s_read_whole_image(BMPREAD_R rp, void* restrict image)
{
	if (rp->ih->bitcount <= 8 || rp->ih->compression == BI_OS2_RLE24)
		s_read_indexed_or_rle_image(rp, image);
	else
		s_read_rgb_pixels(rp, image);
}




static void s_read_indexed_line(BMPREAD_R rp, unsigned char* restrict line,
                               int* restrict x, int* restrict yoff);
static void s_read_rle_line(BMPREAD_R rp, unsigned char* restrict line,
                               int* restrict x, int* restrict yoff);

static void s_read_one_line(BMPREAD_R rp, void* restrict line)
{
	int yoff;

	if (rp->ih->bitcount <= 8 || rp->ih->compression == BI_OS2_RLE24) {

		if (!rp->dont_clear_buffer)
			memset(line, 0, rp->width * rp->result_bytes_per_pixel);

		if (rp->lbl_x >= rp->width)
			rp->lbl_x = 0;

		if (rp->lbl_file_y > rp->lbl_y) {
			; /* nothing to do, RLE skipped line */
		}
		else {
			if (rp->ih->compression == BI_RLE4 ||
			    rp->ih->compression == BI_RLE8 ||
			    rp->ih->compression == BI_OS2_RLE24) {
				s_read_rle_line(rp, (unsigned char*) line,
				                            &rp->lbl_x, &yoff);
			}
			else {
				s_read_indexed_line(rp, (unsigned char*) line,
				                            &rp->lbl_x, &yoff);
			}

			if (!(rp->rle_state_eof || rp->invalid_delta ||
			      rp->file_err || rp->file_eof || rp->panic)) {
				rp->lbl_file_y += yoff;

				if (rp->lbl_file_y > rp->height) {
					rp->invalid_delta = TRUE;
				}
			}
			else {
				if (!rp->rle_state_eof)
					rp->truncated = TRUE;
			}

			if (rp->rle_state_eof)
				rp->lbl_file_y = rp->height;
		}
	}
	else {
		s_read_rgb_line(rp, line);
	}

	rp->lbl_y++;
	if (rp->lbl_y >= rp->height) {
		rp->image_loaded = TRUE;
	}
}



/********************************************************
 * 	s_read_rgb_pixels
 *******************************************************/

static void s_read_rgb_pixels(BMPREAD_R rp, char* restrict image)
{
	int           y;
	size_t        linelength, offs, real_y;

	linelength = (size_t) rp->width * rp->result_bytes_per_pixel;

	for (y = 0; y < rp->height; y++) {
		real_y = rp->topdown ? y : rp->height-1-y;
		offs = linelength * real_y;
		if (!s_read_rgb_line(rp, image + offs))
			rp->truncated = TRUE;
	}
}



/********************************************************
 * 	s_read_rgb_line
 *******************************************************/

static int s_read_rgb_line(BMPREAD_R rp, void* restrict line)
{
	int           x, padding;
	union Pixel   pixel;
	size_t        offs;

	for (x = 0; x < rp->width; x++) {

		if (!s_get_next_rgb(rp, &pixel)) {
			return FALSE;
			/*return feof(rp->file) ? TRUE : FALSE;*/
		}

		offs = x * rp->result_channels;

		switch (rp->result_bits_per_channel) {
		case 8:
			((unsigned char*)line)[offs]   = pixel.red;
			((unsigned char*)line)[offs+1] = pixel.green;
			((unsigned char*)line)[offs+2] = pixel.blue;
			if (rp->has_alpha)
				((unsigned char*)line)[offs+3] = pixel.alpha;
			break;
		case 16:
			((uint16_t*)line)[offs]   = (pixel.red);
			((uint16_t*)line)[offs+1] = (pixel.green);
			((uint16_t*)line)[offs+2] = (pixel.blue);
			if (rp->has_alpha)
				((uint16_t*)line)[offs+3] = (pixel.alpha);
			break;
		case 32:
			((uint32_t*)line)[offs]   = pixel.red;
			((uint32_t*)line)[offs+1] = pixel.green;
			((uint32_t*)line)[offs+2] = pixel.blue;
			if (rp->has_alpha)
				((uint32_t*)line)[offs+3] = pixel.alpha;
			break;
		default:
			logerr(rp->log, "Waaaaaaaaaaaaaah!");
			rp->panic = TRUE;
			return FALSE;

		}
	}
	padding = cm_align4padding((rp->width * rp->ih->bitcount + 7) / 8);
	if (!cm_gobble_up(rp->file, padding, rp->log)) {
		s_set_file_error(rp);
		return FALSE;
	}
	return TRUE;
}



/********************************************************
 * 	s_get_next_rgb
 *
 * read RGB image pixels.
 * works with up to 32bits and any RGBA-masks
 *******************************************************/

static inline int s_get_next_rgb(BMPREAD_R rp, union Pixel* restrict px)
{
	unsigned long v, i;
	int           byte;

	v = 0;
	for (i = 0; i < rp->ih->bitcount; i+=8 ) {
		if (EOF == (byte = getc(rp->file))) {
			s_set_file_error(rp);
			return FALSE;
		}
		v |= ((unsigned long)byte) << i;
	}

	px->red   = (v & rp->colormask.mask.red)   >> rp->colormask.shift.red;
	px->green = (v & rp->colormask.mask.green) >> rp->colormask.shift.green;
	px->blue  = (v & rp->colormask.mask.blue)  >> rp->colormask.shift.blue;

	px->red   = s_scaleint(px->red,   rp->colormask.bits.red,   rp->result_bits_per_channel);
	px->green = s_scaleint(px->green, rp->colormask.bits.green, rp->result_bits_per_channel);
	px->blue  = s_scaleint(px->blue,  rp->colormask.bits.blue,  rp->result_bits_per_channel);

	if (rp->has_alpha) {
		px->alpha = (v & rp->colormask.mask.alpha) >> rp->colormask.shift.alpha;
		px->alpha = s_scaleint(px->alpha, rp->colormask.bits.alpha, rp->result_bits_per_channel);
	}
	else
		px->alpha = (1<<rp->result_bits_per_channel) - 1;

	return TRUE;
}



/********************************************************
 * 	s_read_indexed_or_rle_image
 *
 * read paletted (indexed) image pixels.
 * and also RLE24 (which isn't indexed)
 * works with all known varieties:
 * 1/2/4/8-bit non-RLE and 4/8-bit RLE BMPs
 *******************************************************/

static void s_read_indexed_or_rle_image(BMPREAD_R rp, void* restrict image)
{
	int          x = 0, y = 0, yoff;
	size_t       linesize, real_y;

	linesize = (size_t) rp->width * (size_t) rp->result_bytes_per_pixel;


	/* this sets alpha to zero for undefined pixels: */
	if (!rp->we_allocated_buffer && rp->idx_state_rle && rp->undefined_to_alpha)
		memset(image, 0, rp->result_size);

	while (y < rp->height) {
		real_y = rp->topdown ? y : rp->height-1-y;
		if (rp->ih->compression == BI_RLE4 ||
		    rp->ih->compression == BI_RLE8 ||
		    rp->ih->compression == BI_OS2_RLE24) {
			s_read_rle_line(rp, (unsigned char*)image +
			                        real_y * linesize, &x, &yoff);
		}
		else {
			s_read_indexed_line(rp, (unsigned char*)image +
		                                  real_y * linesize, &x, &yoff);
		}
		if (rp->rle_state_eof || rp->invalid_delta ||
		    rp->file_err || rp->file_eof || rp->panic)
			break;

		y += yoff;

		if (y > rp->height) {
			logerr(rp->log, "RLE delta beyond image dimensions");
			rp->invalid_delta = TRUE;
			break;
		}

		if (x >= rp->width)
			x = 0;
	}
}



/********************************************************
 * 	s_read_indexed_line
 *******************************************************/
static void s_read_indexed_line(BMPREAD_R rp, unsigned char* restrict line,
                               int* restrict x, int* restrict yoff)
{
	int          bits_used, buffer_size, v;
	int          done = FALSE;
	unsigned int buffer;
	size_t       offs;

	*yoff = 1;
	/* setting the buffer size to the alignment takes care of padding bytes */
	buffer_size = 32;

	while (!done && s_read_n_bytes(rp->file, buffer_size / 8, &buffer, rp->log)) {

		bits_used = 0;
		while (bits_used < buffer_size) {

			/* mask out the relevant bits for current pixel      */
			v = (int) s_bits_from_buffer(buffer, buffer_size, rp->ih->bitcount, bits_used);
			bits_used += rp->ih->bitcount;

			if (v >= rp->palette->numcolors) {
				rp->invalid_pixels = TRUE;
			}
			else {
				offs = (size_t) *x * (size_t) rp->result_bytes_per_pixel;
				line[offs]   = rp->palette->color[v].red;
				line[offs+1] = rp->palette->color[v].green;
				line[offs+2] = rp->palette->color[v].blue;
				if (rp->idx_state_rle && rp->undefined_to_alpha)
					line[offs+3] = 0xff; /* set alpha to 1.0 for defined pixels */
			}
			*x += 1;

			if (*x == rp->width) {
				done = TRUE;
				break;      /* discarding rest of buffer == padding */
			}
		}
	}
}



/********************************************************
 * 	s_bits_from_buffer
 *
 * much easier to read than
 * #define s_bits_from_buffer(buf,size,nbits,used) \
 *   (((buf)&(((1<<(nbits))-1)<<((size)-((nbits)+(used)))))>>((size)-((nbits)+(used))))
 *******************************************************/

static inline unsigned int s_bits_from_buffer(unsigned int buf, int size,
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
 *******************************************************/

static void s_read_rle_line(BMPREAD_R rp, unsigned char* restrict line,
                               int* restrict x, int* restrict yoff)
{
	int     repeat = FALSE, left_in_run = 0;
	int     right, up;
	int     padding = FALSE, odd = FALSE, v, r, g, b;
	size_t  offs;

	if (!(rp->ih->bitcount == 4 || rp->ih->bitcount == 8 || rp->ih->bitcount == 24)) {
		rp->panic = TRUE;
		return;
	}

	*yoff = 1;
	while(1)  {
		if (left_in_run > 0) {
			left_in_run--;

			/* literal run */
			if (!repeat && !(rp->ih->bitcount == 4 && odd)) {
			        /* for 24-bit RLE, b holds blue value,     */
			        /* for 4/8-bit RLE, b holds index value(s) */
				if (EOF == (b = getc(rp->file)) ||
				    (rp->ih->bitcount == 24 && EOF == (g = getc(rp->file))) ||
				    (rp->ih->bitcount == 24 && EOF == (r = getc(rp->file)))) {
					s_set_file_error(rp);
					break;
				}
			}
			if (left_in_run == 0 && padding) {
				if (EOF == getc(rp->file)) {
					s_set_file_error(rp);
					break;
				}
			}

			offs = (size_t) *x * (size_t) rp->result_bytes_per_pixel;
			switch (rp->ih->bitcount) {
			case 24:
				line[offs]   = r;
				line[offs+1] = g;
				line[offs+2] = b;
				break;
			case 4:
			case 8:
				if (rp->ih->bitcount == 8)
					v = b;
				else {
					v = odd ? b & 0x0f : (b >> 4) & 0x0f;
					odd = !odd;
				}
				if (v >= rp->palette->numcolors) {
					rp->invalid_pixels = TRUE;
				}
				else {
					line[offs]   = rp->palette->color[v].red;
					line[offs+1] = rp->palette->color[v].green;
					line[offs+2] = rp->palette->color[v].blue;
				}
				break;

			}
			if (rp->undefined_to_alpha)
				line[offs+3] = 0xff; /* set alpha to 1.0 for defined pixels */

			*x += 1;
			if (*x == rp->width) {
				rp->rle_state_eol = FALSE;
				break;
			}
			continue;
		}

		/* not in a literal or RLE run, start afresh */
		if (EOF == (v = getc(rp->file))) {
			s_set_file_error(rp);
			break;
		}

		/* start RLE run */
		if (v > 0) {
			if (EOF == (b = getc(rp->file)) ||
			    (rp->ih->bitcount == 24 && EOF == (g = getc(rp->file))) ||
			    (rp->ih->bitcount == 24 && EOF == (r = getc(rp->file)))) {
				s_set_file_error(rp);
				break;
			}
			padding = FALSE;
			odd = FALSE;
			left_in_run = v;
			repeat = TRUE;
			continue;
		}

		/* v == 0: escape, look at next byte */
		if (EOF == (v = getc(rp->file))) {
			s_set_file_error(rp);
			break;
		}
		/* start literal run */
		if (v > 2) {
			left_in_run = v;
			repeat = FALSE;

			switch (rp->ih->bitcount) {
			case 8:
			case 24:
				padding = v & 0x01 ? TRUE : FALSE;
				break;
			case 4:
				if (v%4 == 1 || v%4 == 2)
					padding = TRUE;
				else
					padding = FALSE;
				break;
			}
			odd = FALSE;
			continue;
		}

		/* end of line.  */
		if (v == 0) {
			if (*x != 0 || rp->rle_state_eol) {
				*x = rp->width;
				rp->rle_state_eol = TRUE;
				break;
			}
			continue;
		}

		/* end of bitmap */
		if (v == 1) {
			rp->rle_state_eof = TRUE;
			break;
		}

		/* delta. */
		if (v == 2) {
			if (EOF == (right = getc(rp->file)) || EOF == (up = getc(rp->file))) {
				s_set_file_error(rp);
				break;
			}
			if (right >= rp->width - *x) {
				rp->invalid_delta = TRUE;
				break;
			}
			*x += right;
			if (up > 0) {
				*yoff = up;
				break;
			}
			continue;
		}

		logerr(rp->log, "Should never get here! (x=%d, byte=%d)", x, v);
		rp->panic = TRUE;
		break;
	}
}



/********************************************************
 * 	s_read_n_bytes
 *******************************************************/

static inline int s_read_n_bytes(FILE *file, int n, unsigned int* restrict buff, LOG log)
{
	int byte;

#ifdef DEBUG
	if (n > sizeof *buff) {
		logerr(log, "Nooooo! Trying to read more than fits into buffer");
		return FALSE;
	}
#endif

	*buff = 0;
	while (n--) {
		if (EOF == (byte = getc(file))) {
			if (!feof(file))
				logsyserr(log, "Reading image data from BMP file");
			return FALSE;
		}

		*buff <<= 8;
		*buff |= byte;
	}

	return TRUE;
}



/********************************************************
 * 	s_set_file_error
 *******************************************************/

static void s_set_file_error(BMPREAD_R rp)
{
	if (feof(rp->file))
		rp->file_eof = TRUE;
	else
		rp->file_err = TRUE;
}



/********************************************************
 * 	s_scaleint
 *******************************************************/

static inline unsigned long s_scaleint(unsigned long val, int frombits, int tobits)
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
