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
	int              indexed;
	struct Palette  *palette;
	int              palette_size; /* sizeof palette in bytes */
	/* output */
	size_t           bytes_written;
	size_t           bytes_written_before_bitdata;
	int              has_alpha;
	struct Colormask colormask;
	int              rle_requested;
	int              rle;
	int              allow_2bit; /* Windows CE, but many will not read it */
	int              outbytes_per_pixel;
	int              outpixels_per_byte;
	int              padding;
	int             *group;
	int              group_count;
	/* state */
	int              outbits_set;
	int              dimensions_set;
	int              saveimage_done;
	int              line_by_line;
	int              lbl_y;
};


static void s_decide_outformat(BMPWRITE_R wp);
static int s_write_palette(BMPWRITE_R wp);
static int s_save_line_rgb(BMPWRITE_R wp, const unsigned char *line);
static inline unsigned long s_set_outpixel_rgb(BMPWRITE_R wp, const unsigned char *restrict buffer, size_t offs);
static int s_write_bmp_file_header(BMPWRITE_R wp);
static int s_write_bmp_info_header(BMPWRITE_R wp);
static int s_check_is_write_handle(BMPHANDLE h);
static inline unsigned s_scaleint(unsigned long val, int frombits, int tobits);
static inline int s_write_one_byte(int byte, BMPWRITE_R wp);
static int s_save_info(BMPWRITE_R wp);
static int s_try_saving_image_size(BMPWRITE_R wp);




/********************************************************
 * 	bmpwrite_new
 *******************************************************/

API BMPHANDLE bmpwrite_new(FILE *file)
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
		logerr(wp->log, "code doesn't work on %d-bit platforms!\n", (int)(8 * sizeof(int)));
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

API BMPRESULT bmpwrite_set_dimensions(BMPHANDLE h,
                                       unsigned  width,
                                       unsigned  height,
                                       unsigned  source_channels,
                                       unsigned  source_bits_per_channel)
{
	int total_bits;
	BMPWRITE wp;

	if (!s_check_is_write_handle(h))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (wp->saveimage_done) {
		logerr(wp->log, "Image already saved.");
		return BMP_RESULT_ERROR;
	}

	switch (source_bits_per_channel) {
	case 8:
		/* ok */
		break;

	case 16:
	case 32:
		if (wp->palette) {
			logerr(wp->log, "Indexed images must be 8-bit (not %d)",
						        source_bits_per_channel);
			return BMP_RESULT_ERROR;
		}
		break;

	default:
		logerr(wp->log, "Invalid number of bits per channel: %d", (int) source_bits_per_channel);
		return BMP_RESULT_ERROR;
	}

	if (source_channels < 1 || source_channels > 4) {
		logerr(wp->log, "Invalid number of channels: %d", (int) source_channels);
		return BMP_RESULT_ERROR;
	}
	if (wp->palette && source_channels != 1) {
		logerr(wp->log, "Indexed images must have 1 channel (not %d)", (int) source_channels);
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
		                    (unsigned) width, (unsigned) height,
		                    (int) source_channels, (int) source_bits_per_channel);
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

API BMPRESULT bmpwrite_set_output_bits(BMPHANDLE h, int red, int green, int blue, int alpha)
{
	BMPWRITE wp;

	if (!s_check_is_write_handle(h))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (wp->saveimage_done) {
		logerr(wp->log, "Image already saved.");
		return BMP_RESULT_ERROR;
	}

	if (wp->palette) {
		logerr(wp->log, "Cannot set output bits for indexed images");
		return BMP_RESULT_ERROR;
	}

	if (!(cm_all_positive_int(4, (int)red, (int)green, (int)blue, (int)alpha) &&
	      cm_all_lessoreq_int(32, 4, (int)red, (int)green, (int)blue, (int)alpha) &&
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
 * 	bmpwrite_set_palette
 *******************************************************/

API BMPRESULT bmpwrite_set_palette(BMPHANDLE h, int numcolors,
                                   const unsigned char *palette)
{
	BMPWRITE wp;
	int      i, c;
	size_t   memsize;

	if (!s_check_is_write_handle(h))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (wp->saveimage_done) {
		logerr(wp->log, "Image already saved.");
		return BMP_RESULT_ERROR;
	}

	if (wp->palette) {
		logerr(wp->log, "Palette already set. Cannot set twice");
		return BMP_RESULT_ERROR;
	}

	if (wp->dimensions_set) {
		if (!(wp->source_channels == 1 &&
		      wp->source_bits_per_channel == 8)) {
			logerr(wp->log, "Invalid channels/bits (%d/%d)"
			        " set for indexed image", wp->source_channels,
			        wp->source_bits_per_channel);
			return BMP_RESULT_ERROR;
		}
	}

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



/********************************************************
 * 	bmpwrite_set_rle
 *******************************************************/

API BMPRESULT bmpwrite_set_rle(BMPHANDLE h, enum BmpRLEtype type)
{
	BMPWRITE wp;

	if (!s_check_is_write_handle(h))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	switch (type) {
	case BMP_RLE_NONE:
	case BMP_RLE_AUTO:
	case BMP_RLE_RLE8:
		wp->rle_requested = type;
		break;
	default:
		logerr(wp->log, "Invalid RLE type specified (%d)", (int) type);
		return BMP_RESULT_ERROR;
	}
	return BMP_RESULT_OK;
}



/********************************************************
 * 	bmpwrite_set_resolution
 *******************************************************/

API BMPRESULT bmpwrite_set_resolution(BMPHANDLE h, int xdpi, int ydpi)
{
	BMPWRITE wp;

	if (!s_check_is_write_handle(h))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (wp->saveimage_done) {
		logerr(wp->log, "Image already saved.");
		return BMP_RESULT_ERROR;
	}

	wp->ih->xpelspermeter = 39.37 * xdpi + 0.5;
	wp->ih->ypelspermeter = 39.37 * ydpi + 0.5;

	return BMP_RESULT_OK;
}



/********************************************************
 * 	bmpwrite_allow_2bit
 *******************************************************/

API BMPRESULT bmpwrite_allow_2bit(BMPHANDLE h)
{
	BMPWRITE wp;

	if (!s_check_is_write_handle(h))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (wp->saveimage_done) {
		logerr(wp->log, "Image already saved.");
		return BMP_RESULT_ERROR;
	}

	wp->allow_2bit = TRUE;

	return BMP_RESULT_OK;
}



/********************************************************
 * 	bmpwrite_save_image
 *******************************************************/
static int s_save_line_rgb(BMPWRITE_R wp, const unsigned char *line);
static int s_save_line_rle8(BMPWRITE_R wp, const unsigned char *line);
static int s_save_line_rle4(BMPWRITE_R wp, const unsigned char *line);

API BMPRESULT bmpwrite_save_image(BMPHANDLE h, const unsigned char *image)
{
	BMPWRITE wp;
	size_t   offs, linesize;
	int      y, res;

	if (!s_check_is_write_handle(h))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (wp->line_by_line) {
		logerr(wp->log, "Cannot switch from line-by-line to saving full image");
		return BMP_RESULT_ERROR;
	}

	if  (!s_save_info(wp))
		return BMP_RESULT_ERROR;

	wp->saveimage_done = TRUE;
	wp->bytes_written_before_bitdata = wp->bytes_written;

	linesize = (size_t) wp->width * (size_t) wp->source_bytes_per_pixel;
	for (y = wp->height - 1; y >= 0; y--) {
		offs = (size_t) y * linesize;
		switch (wp->rle) {
		case 8:
			res = s_save_line_rle8(wp, image + offs);
			break;
		case 4:
			res = s_save_line_rle4(wp, image + offs);
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
		if (EOF == s_write_one_byte(0, wp) ||
		    EOF == s_write_one_byte(1, wp)) {
			logsyserr(wp->log, "Writing RLE end-of-file marker");
			return BMP_RESULT_ERROR;
		}
		s_try_saving_image_size(wp);
	}
#ifdef DEBUG
	printf("bytes written: %d (0x%04lx) = %dkb\n", (int)wp->bytes_written,
		                         wp->bytes_written, (int) (wp->bytes_written>>10));
#endif
	return BMP_RESULT_OK;
}

/********************************************************
 * 	bmpwrite_save_line
 *******************************************************/

API BMPRESULT bmpwrite_save_line(BMPHANDLE h, const unsigned char *line)
{
	BMPWRITE wp;
	int      res;

	if (!s_check_is_write_handle(h))
		return BMP_RESULT_ERROR;
	wp = (BMPWRITE)(void*)h;

	if (wp->saveimage_done) {
		logerr(wp->log, "Image already saved.");
		return BMP_RESULT_ERROR;
	}

	if (!wp->line_by_line) {  /* first line */
		if  (!s_save_info(wp))
			goto abort;
		wp->bytes_written_before_bitdata = wp->bytes_written;
		wp->line_by_line = TRUE;
	}

	switch (wp->rle) {
	case 8:
		res = s_save_line_rle8(wp, line);
		break;
	case 4:
		res = s_save_line_rle4(wp, line);
		break;
	default:
		res = s_save_line_rgb(wp, line);
		break;
	}

	if (!res)
		goto abort;

	if (++wp->lbl_y >= wp->height) {
		if (wp->rle) {
			if (EOF == s_write_one_byte(0, wp) ||
			    EOF == s_write_one_byte(1, wp)) {
				logsyserr(wp->log, "Writing RLE end-of-file marker");
				goto abort;
			}
			s_try_saving_image_size(wp);
		}
		wp->saveimage_done = TRUE;
#ifdef DEBUG
		printf("bytes written: %d (0x%04lx) = %dkb\n", (int)wp->bytes_written,
		                         wp->bytes_written, (int) (wp->bytes_written>>10));
#endif

	}

	return BMP_RESULT_OK;
abort:
	wp->saveimage_done = TRUE;
	return BMP_RESULT_ERROR;
}



/********************************************************
 * 	s_save_info
 *******************************************************/

static int s_save_info(BMPWRITE_R wp)
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



/********************************************************
 * 	s_try_saving_image_size
 * this will fail on unseekable files like pipes etc.
 * which isn't too bad, because reader generally
 * ignore the file and image size in the header anyway.
 *******************************************************/

static int s_try_saving_image_size(BMPWRITE_R wp)
{
	uint32_t image_size, file_size;

	image_size = wp->bytes_written - wp->bytes_written_before_bitdata;
	file_size = wp->bytes_written;

	if (fseek(wp->file, 2, SEEK_SET))
		return FALSE;
	if (!write_u32_le(wp->file, file_size))
		return FALSE;
	if (fseek(wp->file, 14 + 20, SEEK_SET))
		return FALSE;
	if (!write_u32_le(wp->file, image_size))
		return FALSE;
	return TRUE;
}



/********************************************************
 * 	s_save_line_rgb
 *******************************************************/

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
		}
		else {
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


/********************************************************
 * 	s_length_of_runs
 *
 * 	for RLE-encoding, returns the number of
 *      pixels in contiguous upcoming groups with
 *      run-lengths > 1. Used to termine if it is
 *      worthwile to switch from literal run to
 *      repeat-run.
 *******************************************************/

static inline int s_length_of_runs(BMPWRITE_R wp, int x, int group, int minlen)
{
	int i, len = 0;

	for (i = group; i < wp->group_count; i++) {
		if (wp->group[i] <= minlen)
			break;
		len += wp->group[i];
	}
	return len;
}

/********************************************************
 * 	s_save_line_rle8
 *******************************************************/

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
		while (i+l < wp->group_count && wp->group[i+l] == 1 && dx < 255) {
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
			if (i+l < wp->group_count && s_length_of_runs(wp, x+dx, i+l, 1) <= small_number) {
				while (i+l < wp->group_count && wp->group[i+l] > 1 && dx + wp->group[i+l] < 255) {
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



/********************************************************
 * 	s_save_line_rle4
 *******************************************************/

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
			if (i+l < wp->group_count && s_length_of_runs(wp, x+dx, i+l, 2) <= small_number) {
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
			if ((dx+1)%4 > 1) {  /* pad odd byte-length literal run */
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
	logsyserr(wp->log, "Writing RLE8 data to BMP file");
	return FALSE;
}



/********************************************************
 * 	bw_free
 *******************************************************/

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



/********************************************************
 * 	s_decide_outformat
 *******************************************************/

static void s_decide_outformat(BMPWRITE_R wp)
{
	int   bitsum, i, bytes_per_line;

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


	if (wp->palette) {
		wp->ih->version     = BMPINFO_V3;
		wp->ih->size        = BMPIHSIZE_V3;
		if (wp->rle_requested != BMP_RLE_NONE) {
			if (wp->palette->numcolors > 16 ||
			    wp->rle_requested == BMP_RLE_RLE8) {
				wp->rle = 8;
				wp->ih->compression = BI_RLE8;
				wp->ih->bitcount = 8;
			}
			else {
				wp->rle = 4;
				wp->ih->compression = BI_RLE4;
				wp->ih->bitcount = 4;
			}
		}
		else {
			wp->ih->compression = BI_RGB;
			wp->ih->bitcount = 1;
			while ((1<<wp->ih->bitcount) < wp->palette->numcolors)
				wp->ih->bitcount *= 2;
			if (wp->ih->bitcount == 2 && !wp->allow_2bit)
				wp->ih->bitcount = 4;
		}



	}
	/* we need BI_BITFIELDS if any of the following is true:
	 *    - not all RGB-components have the same bitlength
	 *    - we are writing an alpha-channel
	 *    - bits per component are not either 5 or 8 (which have
	 *      known RI_RGB representation)
	 */
	else if (!cm_all_equal_int(3, (int) wp->colormask.bits.red,
	                         (int) wp->colormask.bits.green,
	                         (int) wp->colormask.bits.blue) ||
	          wp->has_alpha ||
                  (wp->colormask.bits.red > 0 &&
                   wp->colormask.bits.red != 5 &&
                   wp->colormask.bits.red != 8)    ) {

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
	 */
	else {
		wp->ih->version     = BMPINFO_V3;
		wp->ih->size        = BMPIHSIZE_V3;
		wp->ih->compression = BI_RGB;
		wp->ih->bitcount    = (bitsum + 7) / 8 * 8;
	}

	if (wp->palette) {
		wp->outpixels_per_byte = 8 / wp->ih->bitcount;
		wp->ih->clrused = wp->palette->numcolors;
	}
	else {
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
	}

	bytes_per_line = (wp->ih->bitcount * wp->width + 7) / 8;
	wp->padding = cm_align4padding(bytes_per_line);

	wp->fh->type = 0x4d42; /* "BM" */
	wp->fh->size = wp->rle ? 0 : BMPFHSIZE + wp->ih->size + wp->palette_size +
	                             (bytes_per_line + wp->padding) * wp->height;
	wp->fh->offbits = BMPFHSIZE + wp->ih->size + wp->palette_size;

	wp->ih->width = wp->width;
	wp->ih->height = wp->height;
	wp->ih->planes = 1;
	wp->ih->sizeimage = wp->rle ? 0 : (bytes_per_line + wp->padding) * wp->height;
}



/********************************************************
 * 	s_set_outpixel_rgb
 *******************************************************/

static inline unsigned long s_set_outpixel_rgb(BMPWRITE_R wp, const unsigned char *restrict buffer, size_t offs)
{
	unsigned long bytes, r,g,b, a = 0;
	int           alpha_offs, rgb = TRUE;

	if (wp->source_channels < 3)
		rgb = FALSE; /* grayscale */

	if (wp->has_alpha)
		alpha_offs = rgb ? 3 : 1;

	switch(wp->source_bits_per_channel) {
	case 8:
		r = ((unsigned char*)buffer)[offs];
		if (rgb) {
			g = ((const unsigned char*)buffer)[offs+1];
			b = ((const unsigned char*)buffer)[offs+2];
		}
		else {
			g = b = r;
		}

		if (wp->has_alpha)
			a = ((const unsigned char*)buffer)[offs+alpha_offs];
		break;
	case 16:
		r = ((uint16_t*)buffer)[offs];
		if (rgb) {
			g = ((const uint16_t*)buffer)[offs+1];
			b = ((const uint16_t*)buffer)[offs+2];
		}
		else {
			g = b = r;
		}
		if (wp->has_alpha)
			a = ((const uint16_t*)buffer)[offs+alpha_offs];
		break;

	case 32:
		r = ((uint32_t*)buffer)[offs];
		if (rgb) {
			g = ((const uint32_t*)buffer)[offs+1];
			b = ((const uint32_t*)buffer)[offs+2];
		}
		else {
			g = b = r;
		}
		if (wp->has_alpha)
			a = ((const uint32_t*)buffer)[offs+alpha_offs];
		break;

	default:
		logerr(wp->log, "Panic! Bitdepth (%d) other than 8/16/32", 
			                     (int) wp->source_bits_per_channel);
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
 * 	s_write_palette
 *******************************************************/

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



/********************************************************
 * 	s_write_bmp_file_header
 *******************************************************/

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



/********************************************************
 * 	s_write_bmp_info_header
 *******************************************************/

static int s_write_bmp_info_header(BMPWRITE_R wp)
{
	if (!(write_u32_le(wp->file, wp->ih->size) &&
	      write_s32_le(wp->file, wp->ih->width) &&
	      write_s32_le(wp->file, wp->ih->height) &&
	      write_u16_le(wp->file, wp->ih->planes) &&
	      write_u16_le(wp->file, wp->ih->bitcount) &&
	      write_u32_le(wp->file, wp->ih->compression) &&
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




/********************************************************
 * 	s_write_one_byte
 *******************************************************/

static inline int s_write_one_byte(int byte, BMPWRITE_R wp)
{
	int ret;

	if (EOF != (ret = putc(byte, wp->file)))
		wp->bytes_written++;

	return ret;
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
