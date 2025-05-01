/* bmplib - bmp-read-loadindexed.c
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

#define BMPLIB_LIB

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

	if (!(rp = cm_read_handle(h)))
		return 0;

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

	if (!(rp = cm_read_handle(h)))
		return BMP_RESULT_ERROR;

	if (rp->read_state < RS_HEADER_OK) {
		logerr(rp->c.log, "Must call bmpread_load_info() before loading palette");
		return BMP_RESULT_ERROR;
	}
	if (rp->read_state >= RS_LOAD_STARTED) {
		logerr(rp->c.log, "Cannot load palette after image data");
		return BMP_RESULT_ERROR;
	}

	if (!rp->palette) {
		logerr(rp->c.log, "Image has no palette");
		return BMP_RESULT_ERROR;
	}

	if (!palette) {
		logerr(rp->c.log, "palette is NULL");
		return BMP_RESULT_ERROR;
	}

	if (rp->result_format != BMP_FORMAT_INT) {
		logerr(rp->c.log, "Palette can only be loaded when number format is BMP_FORMAT_INT");
		return BMP_RESULT_ERROR;
	}

	memsize = rp->palette->numcolors * 4;
	if (!*palette) {
		if (!(*palette = malloc(memsize))) {
			logsyserr(rp->c.log, "allocating palette");
			rp->read_state = RS_FATAL;
			return BMP_RESULT_ERROR;
		}
	}
	memset(*palette, 0, memsize);

	/* irreversible. image will be returned as indexed pixels */
	if (!rp->result_indexed) {
		rp->result_indexed = true;
		rp->read_state = MIN(RS_HEADER_OK, rp->read_state);
		rp->dim_queried_channels = false;
		rp->result_channels = 1;
		if (!br_set_resultbits(rp)) {
			rp->read_state = RS_FATAL;
			return BMP_RESULT_ERROR;
		}
	}

	for (i = 0; i < rp->palette->numcolors; i++) {
		for (c = 0; c < 3; c++) {
			(*palette)[4*i + c] = rp->palette->color[i].value[c];
		}
	}

	return BMP_RESULT_OK;
}
