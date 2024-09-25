/* bmplib - bmp-read-loadindexed.c
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

#include "config.h"
#include "bmplib.h"
#include "logging.h"
#include "bmp-common.h"
#include "bmp-read.h"


/* Functions in this file are for retreiving an indexed BMP as
 * index + palette. (Unlike the bmplib default which is to
 * return 24bit RGB data.)
 */



/********************************************************
 * 	bmpread_num_palette_colors
 *
 * will return the number of palette-color. For non-
 * indexed BMPs, 0 is returned.
 *******************************************************/

API int bmpread_num_palette_colors(BMPHANDLE h)
{
	BMPREAD rp;

	if (!(h && cm_check_is_read_handle(h)))
		return 0;
	rp = (BMPREAD)(void*)h;

	if (rp->palette)
		return rp->palette->numcolors;

	return 0;
}



/********************************************************
 * 	bmpread_load_palette
 * palette entries are always passed on as a
 * character array, 4 bytes per color (R-G-B-0)
 *******************************************************/

API BMPRESULT bmpread_load_palette(BMPHANDLE h, unsigned char **palette)
{
	BMPREAD rp;
	int     i,c;
	size_t	memsize;


	if (!(h && cm_check_is_read_handle(h)))
		return 0;
	rp = (BMPREAD)(void*)h;

	if (!rp->palette) {
		logerr(rp->log, "Image has no palette");
		return BMP_RESULT_ERROR;
	}

	if (!palette) {
		logerr(rp->log, "palette is NULL");
		return BMP_RESULT_ERROR;
	}

	memsize = rp->palette->numcolors * 4;
	if (!*palette) {
		if (!(*palette = malloc(memsize))) {
			logsyserr(rp->log, "allocating palette");
			return BMP_RESULT_ERROR;
		}
	}
	memset(*palette, 0, memsize);

	/* irreversible. image will be returned as indexed pixels */
	if (!rp->result_indexed) {
		rp->result_indexed = TRUE;
		rp->dimensions_queried = FALSE;

		rp->result_channels = 1;
		rp->result_bits_per_pixel = 8;
		rp->result_bytes_per_pixel = 1;
		rp->result_bits_per_channel = 8;
		rp->result_size = (size_t) rp->width * (size_t) rp->height;
	}

	for (i = 0; i < rp->palette->numcolors; i++) {
		for (c = 0; c < 3; c++) {
			(*palette)[4*i + c] = rp->palette->color[i].value[c];
		}
	}

	return BMP_RESULT_OK;
}
