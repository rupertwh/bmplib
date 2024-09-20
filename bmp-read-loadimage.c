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


static int s_read_rgb_pixels(BMPREAD_R rp, void *restrict image);
static inline int s_get_next_rgb(BMPREAD_R rp, union Pixel* restrict px);

static int s_read_indexed_pixels(BMPREAD_R rp, void *restrict image);
static inline unsigned int s_bits_from_buffer(unsigned int buf, int size,
                                              int nbits, int used_bits);

static inline int s_read_n_bytes(FILE *file, int n, unsigned int* restrict buff, LOG log);

static inline unsigned long s_scaleint(unsigned long val, int frombits, int tobits);



/********************************************************
 * 	bmpread_load_image
 *******************************************************/

EXPORT_VIS BMPRESULT bmpread_load_image(BMPHANDLE h, char **restrict buffer)
{
	BMPREAD rp;

	if (!(h && cm_check_is_read_handle(h)))
		return BMP_RESULT_ERROR;
	rp = (BMPREAD)(void*)h;

	if (!(rp->getinfo_called && (rp->getinfo_return == BMP_RESULT_OK))) {
		if (rp->getinfo_return == BMP_RESULT_INSANE) {
			logerr(rp->log, "trying to load insanley large image");
			return BMP_RESULT_INSANE;
		}
		logerr(rp->log, "getinfo had failed, cannot load image");
		return BMP_RESULT_ERROR;
	}

#ifdef NEVER  /* gets too complicated with querying single dim values */
	if (!rp->dimensions_queried) {
		logerr(rp->log, "must query dimensions before loading image");
		return BMP_RESULT_ERROR;
	}
#endif

	if (rp->image_loaded) {
		logerr(rp->log, "Cannot load image more than once!");
		return BMP_RESULT_ERROR;
	}

	rp->image_loaded = TRUE;

	if (!buffer) {
		logerr(rp->log, "buffer pointer is NULL");
		return BMP_RESULT_ERROR;
	}

	if (!*buffer) { /* no buffer supplied, we will allocate one */
		if (!(*buffer = malloc(rp->result_size))) {
			logsyserr(rp->log, "allocating result buffer");
			return BMP_RESULT_ERROR;
		}
		rp->we_allocated_buffer = TRUE;
		memset(*buffer, 0, rp->result_size);
	}



	/* skip to actual data: */

#ifdef DEBUG
	printf("bytes read: %d  Start of data: %d\n", (int)rp->bytes_read, (int)rp->fh->offbits);
#endif
	if (rp->bytes_read > rp->fh->offbits) {
		logerr(rp->log, "Panic! Corrupt file?");
		goto abort;
	}
	if (!cm_gobble_up(rp->file, rp->fh->offbits - rp->bytes_read, rp->log)) {
		logerr(rp->log, "while seeking start of bitmap data");
		goto abort;
	}

	if (rp->ih->bitcount <= 8 || rp->ih->compression == BI_OS2_RLE24) {
		if (!s_read_indexed_pixels(rp, *buffer))
			goto abort;
	} else {
		if (!s_read_rgb_pixels(rp, *buffer)) {
			goto abort;
		}
	}

	if (rp->truncated || rp->invalid_pixels)
		return BMP_RESULT_TRUNCATED;

	return BMP_RESULT_OK;

abort:
	if (rp->we_allocated_buffer) {
		free(*buffer);
		*buffer = NULL;
	}
	return BMP_RESULT_ERROR;
}




/********************************************************
 * 	s_read_rgb_pixels
 *******************************************************/

static int s_read_rgb_pixels(BMPREAD_R rp, void* restrict image)
{
	int           x,y, padding;
	union Pixel   pixel;
	size_t        linelength, offs;

	linelength = (size_t) rp->width * rp->result_channels;

	padding = cm_align4padding((rp->width * rp->ih->bitcount + 7) / 8);

	for (y = 0; y < rp->height; y++) {
		for (x = 0; x < rp->width; x++) {

			if (!s_get_next_rgb(rp, &pixel)) {
				goto truncated;
				/*return feof(rp->file) ? TRUE : FALSE;*/
			}

			offs = linelength * (size_t) (rp->topdown ? y : rp->height-1-y) + (size_t) x * rp->result_channels;

			switch (rp->result_bits_per_channel) {
			case 8:
				((unsigned char*)image)[offs]   = pixel.red;
				((unsigned char*)image)[offs+1] = pixel.green;
				((unsigned char*)image)[offs+2] = pixel.blue;
				if (rp->has_alpha)
					((unsigned char*)image)[offs+3] = pixel.alpha;
				break;
			case 16:
				((uint16_t*)image)[offs]   = (pixel.red);
				((uint16_t*)image)[offs+1] = (pixel.green);
				((uint16_t*)image)[offs+2] = (pixel.blue);
				if (rp->has_alpha)
					((uint16_t*)image)[offs+3] = (pixel.alpha);
				break;
			case 32:
				((uint32_t*)image)[offs]   = pixel.red;
				((uint32_t*)image)[offs+1] = pixel.green;
				((uint32_t*)image)[offs+2] = pixel.blue;
				if (rp->has_alpha)
					((uint32_t*)image)[offs+3] = pixel.alpha;
				break;
			default:
				logerr(rp->log, "Waaaaaaaaaaaaaah!");
				return FALSE;

			}
		}
		if (!cm_gobble_up(rp->file, padding, rp->log)) {
			logerr(rp->log, "while reading padding bytes from BMP file");
			goto truncated;
		}
	}
	return TRUE;

truncated:
	rp->truncated = TRUE;
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
			if (feof(rp->file))
				logerr(rp->log, "Unexpected end of file");
			else
				logsyserr(rp->log, "Reading BMP RGB data");
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
 * 	s_read_indexed_pixels
 *
 * read paletted (indexed) image pixels.
 * and also RLE24 (which isn't indexed)
 * works with all known varieties:
 * 1/2/4/8-bit non-RLE and 4/8-bit RLE BMPs
 *******************************************************/


#define RIL_OK         (0)
#define RIL_EOFMARKER  (1)
#define RIL_FILE_ERR   (2)
#define RIL_FILE_EOF   (3)
#define RIL_CORRUPT    (4)
#define RIL_PANIC      (5)

static int s_read_indexed_line(BMPREAD_R rp, unsigned char* restrict line,
                               int* restrict x, int* restrict yoff);
static int s_read_rle24_line(BMPREAD_R rp, unsigned char* restrict line,
                               int* restrict x, int* restrict yoff);

#define TOPDOWN(y, height, topdown) ((topdown) ? (y) : (height)-(y)-1)

static int s_read_indexed_pixels(BMPREAD_R rp, void* restrict image)
{
	int          x = 0, y = 0, yoff;
	size_t       linesize;
	int          ril = 0;

	linesize = (size_t) rp->width * (size_t) rp->result_bytes_per_pixel;

	if (rp->ih->compression == BI_RLE8 ||
	    rp->ih->compression == BI_RLE4 ||
	    rp->ih->compression == BI_OS2_RLE24)
		rp->idx_state_rle = TRUE;

	/* this sets alpha to zero for undefined pixels: */
	if (!rp->we_allocated_buffer && rp->idx_state_rle && rp->undefined_to_alpha)
		memset(image, 0, rp->result_size);

	while (y < rp->height) {
		if (rp->ih->compression != BI_OS2_RLE24) {
			ril = s_read_indexed_line(rp, (unsigned char*)image +
		                     TOPDOWN(y, rp->height, rp->topdown) * linesize, &x, &yoff);
		}
		else {
			ril = s_read_rle24_line(rp, (unsigned char*)image +
			             TOPDOWN(y, rp->height, rp->topdown) * linesize, &x, &yoff);
		}
		if (ril != RIL_OK && ril != RIL_CORRUPT)
			break;

		y += yoff;

		if (y > rp->height) {
			logerr(rp->log, "RLE delta beyond image dimensions");
			ril = RIL_CORRUPT;
			break;
		}

		if (x >= rp->width)
			x = 0;
	}

	switch (ril) {
	case RIL_PANIC:
		return FALSE;

	case RIL_FILE_EOF:
		logerr(rp->log, "Unexpected end of file. Image truncated");
		rp->truncated = TRUE;
		break;

	case RIL_FILE_ERR:
		logsyserr(rp->log, "Image truncated");
		rp->truncated = TRUE;
		break;
	case RIL_CORRUPT:
		logerr(rp->log, "BMP file possibly damaged or corrupt");
		rp->invalid_pixels = TRUE;
		break;
	}

	return TRUE;
}



/********************************************************
 * 	s_read_indexed_line
 *******************************************************/
static int s_read_indexed_line(BMPREAD_R rp, unsigned char* restrict line,
                               int* restrict x, int* restrict yoff)
{
	int          bits_used, buffer_size, v;
	int          repeat = FALSE, left_in_run = 0, done = FALSE;
	int          right, up;
	unsigned int buffer;
	size_t       offs;
	int          ret = RIL_OK;

	*yoff = 1;
	/* setting the buffer size to the alignment takes care of padding bytes */
	buffer_size = rp->idx_state_rle ? 16 : 32;

	while (!done && s_read_n_bytes(rp->file, buffer_size / 8, &buffer, rp->log)) {

		bits_used = 0;
		while (bits_used < buffer_size) {

			/* RLE-run. non-RLE can be treated as a never-ending RLE literal run. */
			if (left_in_run > 0 || !rp->idx_state_rle) {

				if (rp->idx_state_rle)
					left_in_run--;

				/* mask out the relevant bits for current pixel      */
				v = (int) s_bits_from_buffer(buffer, buffer_size, rp->ih->bitcount, bits_used);
				bits_used += rp->ih->bitcount;

				/* for repeated runs, use upper bits of buffer over and over */
				if (repeat && bits_used == buffer_size)
					bits_used = buffer_size - 8;

				if (rp->idx_state_rle && left_in_run == 0) {
					bits_used = buffer_size;   /* takes care of RLE-padding */
				}

				if (v >= rp->palette->numcolors) {
					/* don't log inside loop if we continue! */
					/*logerr(rp->log, "Invalid palette index %d. Maximum is %d",
					                    v, rp->palette->numcolors - 1);*/
					ret = RIL_CORRUPT;
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
					rp->idx_state_eol = FALSE;
					done = TRUE;
					break;      /* discarding rest of buffer == padding */
				}

				continue;
			}

			if (bits_used > 0) {
				logerr(rp->log, "bits_used > 0. This should be impossible!");
				ret = RIL_PANIC;
				done = TRUE;
				break;
			}

			/* not in a literal or RLE run, start afresh */
			v = (int) s_bits_from_buffer(buffer, buffer_size, 8, 0);
			bits_used = 8;

			/* start RLE run */
			if (v > 0) {
				left_in_run = v;
				repeat = TRUE;
				continue;
			}

			/* v == 0: escape, look at next byte */
			v = (int) s_bits_from_buffer(buffer, buffer_size, 8, bits_used);
			bits_used += 8;

			/* start literal run */
			if (v > 2) {
				left_in_run = v;
				repeat = FALSE;
				continue;
			}

			/* end of line.  */
			if (v == 0) {
				if (*x != 0 || rp->idx_state_eol) {
					done = TRUE;
					*x = rp->width;
					rp->idx_state_eol = TRUE;
					break;
				}
				continue;
			}

			/* end of bitmap */
			if (v == 1) {
				ret = RIL_EOFMARKER;
				done = TRUE;
				break;
			}

			/* delta. */
			if (v == 2) {
				if (EOF == (right = getc(rp->file)) || EOF == (up = getc(rp->file))) {
					ret = RIL_FILE_EOF;
					done = TRUE;
					break;
				}
				if (right >= rp->width - *x) {
					logerr(rp->log, "RLE delta beyond image dimensions. "
					                "%d right at column %d in a %dpx wide image.",
					                right, *x, rp->width);
					rp->invalid_pixels = TRUE;
					ret = RIL_CORRUPT;
					done = TRUE;
					break;
				}
				*x += right;
				if (up > 0) {
					*yoff = up;
					done = TRUE;
					break;
				}
				continue;
			}

			logerr(rp->log, "Should never get here! (x=%d, byte=%d)", x, v);
			ret = RIL_PANIC;
			done = TRUE;
			break;
		}
	}

	if (ret == RIL_FILE_EOF || !done) {
		if (!feof(rp->file))
			ret = RIL_FILE_ERR;
		else
			ret = RIL_FILE_EOF;
	}
	return ret;
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
 * 	s_read_rle24_line
 *******************************************************/

static int s_read_rle24_line(BMPREAD_R rp, unsigned char* restrict line,
                               int* restrict x, int* restrict yoff)
{
	int     repeat = FALSE, left_in_run = 0, done = FALSE;
	int     right, up;
	int     odd = 0, v, r, g, b;
	size_t  offs;
	int     ret = RIL_OK;

	*yoff = 1;
	while(1)  {
		if (left_in_run > 0) {
			left_in_run--;

			/* literal run */
			if (!repeat) {
				if (EOF == (b = getc(rp->file)) ||
				    EOF == (g = getc(rp->file)) ||
				    EOF == (r = getc(rp->file))) {
					ret = RIL_FILE_EOF;
					break;
				}
			}

			if (left_in_run == 0) {
				if (odd) {
					if (EOF == getc(rp->file)) {
						ret = RIL_FILE_EOF;
						break;
					}
				}
			}

			offs = (size_t) *x * (size_t) rp->result_bytes_per_pixel;
			line[offs]   = r;
			line[offs+1] = g;
			line[offs+2] = b;
			if (rp->undefined_to_alpha)
				line[offs+3] = 0xff; /* set alpha to 1.0 for defined pixels */

			*x += 1;
			if (*x == rp->width) {
				rp->idx_state_eol = FALSE;
				done = TRUE;
				break;
			}
			continue;
		}

		/* not in a literal or RLE run, start afresh */
		if (EOF == (v = getc(rp->file))) {
			ret = RIL_FILE_EOF;
			break;
		}

		/* start RLE run */
		if (v > 0) {
			if (EOF == (b = getc(rp->file)) ||
			    EOF == (g = getc(rp->file)) ||
			    EOF == (r = getc(rp->file))) {
				ret = RIL_FILE_EOF;
				break;
			}
			odd = FALSE;
			left_in_run = v;
			repeat = TRUE;
			continue;
		}

		/* v == 0: escape, look at next byte */
		if (EOF == (v = getc(rp->file))) {
			ret = RIL_FILE_EOF;
			break;
		}
		/* start literal run */
		if (v > 2) {
			left_in_run = v;
			repeat = FALSE;

			if (v & 0x01)
				odd = TRUE;
			else
				odd = FALSE;

			continue;
		}

		/* end of line.  */
		if (v == 0) {
			if (*x != 0 || rp->idx_state_eol) {
				done = TRUE;
				*x = rp->width;
				rp->idx_state_eol = TRUE;
				break;
			}
			continue;
		}

		/* end of bitmap */
		if (v == 1) {
			ret = RIL_EOFMARKER;
			done = TRUE;
			break;
		}

		/* delta. */
		if (v == 2) {
			if (EOF == (right = getc(rp->file)) || EOF == (up = getc(rp->file))) {
				ret = RIL_FILE_EOF;
				done = TRUE;
				break;
			}
			if (right >= rp->width - *x) {
				logerr(rp->log, "RLE delta beyond image dimensions. "
				                "%d right at column %d in a %dpx wide image.",
				                (int)right, *x, rp->width);
				rp->invalid_pixels = TRUE;
				ret = RIL_CORRUPT;
				done = TRUE;
				break;
			}
			*x += right;
			if (up > 0) {
				*yoff = up;
				done = TRUE;
				break;
			}
			continue;
		}

		logerr(rp->log, "Should never get here! (x=%d, byte=%d)", x, v);
		ret = RIL_PANIC;
		done = TRUE;
		break;
	}


	if (ret == RIL_FILE_EOF || !done) {
		if (!feof(rp->file))
			ret = RIL_FILE_ERR;
		else
			ret = RIL_FILE_EOF;
	}
	return ret;
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
