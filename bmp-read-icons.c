/* bmplib - bmp-read-icons.c
 *
 * Copyright (c) 2025, Rupert Weber.
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

#define BMPLIB_LIB

#include "config.h"
#include "bmplib.h"
#include "logging.h"
#include "bmp-common.h"
#include "bmp-read-icons.h"




/********************************************************
 * 	bmpread_array_num
 *******************************************************/

API int bmpread_array_num(BMPHANDLE h)
{
	BMPREAD rp;

	if (!(rp = cm_read_handle(h)))
		return BMP_RESULT_ERROR;

	if (rp->read_state != RS_ARRAY) {
		logerr(rp->c.log, "Not a bitmap array");
		return -1;
	}

	return rp->narrayimgs;
}


/********************************************************
 * 	bmpread_array_info
 *******************************************************/

API BMPRESULT bmpread_array_info(BMPHANDLE h, struct BmpArrayInfo *ai, int idx)
{
	BMPREAD rp;

	if (!(rp = cm_read_handle(h)))
		return BMP_RESULT_ERROR;

	if (rp->read_state != RS_ARRAY) {
		logerr(rp->c.log, "Not a bitmap array");
		return BMP_RESULT_ERROR;
	}

	if (idx < 0 || idx >= rp->narrayimgs) {
		logerr(rp->c.log, "Invalid array index %d. Max is %d", idx, rp->narrayimgs - 1);
		return BMP_RESULT_ERROR;
	}

	if (!ai) {
		logerr(rp->c.log, "Invalid array info pointer (NULL)");
		return BMP_RESULT_ERROR;
	}

	struct Arraylist *img = &rp->arrayimgs[idx];
	BMPREAD imgrp = (BMPREAD)img->handle;

	memset(ai, 0, sizeof *ai);

	ai->type   = imgrp->fh->type;
	ai->handle = img->handle;
	ai->width  = imgrp->width;
	ai->height = imgrp->height;
	if (imgrp->ih->bitcount <= 8)
		ai->ncolors = 1 << imgrp->ih->bitcount;
	else
		ai->ncolors = 0;
	ai->screenwidth  = img->ah.screenwidth;
	ai->screenheight = img->ah.screenheight;

	return BMP_RESULT_OK;
}


/********************************************************
 * 	icon_read_array
 *******************************************************/
static bool s_read_array_header(BMPREAD_R rp, struct Bmparray *ah);
static void s_array_header_from_file_header(struct Bmparray *ah, struct Bmpfile *fh);

bool icon_read_array(BMPREAD_R rp)
{
	struct Arraylist  *imgs = NULL;
	struct Bmparray    ah   = { 0 };
	int                n    = 0;
	const int          nmax = 16;
	bool               invalid = false;

	if (!(imgs = calloc(nmax, sizeof *imgs))) {
		logsyserr(rp->c.log, "Allocating bitmap array list");
		rp->lasterr = BMP_ERR_MEMORY;
		return false;
	}

	s_array_header_from_file_header(&ah, rp->fh);

	while (n < nmax) {
		if (ah.type != BMPFILE_BA) {
			logerr(rp->c.log, "Invalid BMP type (0x%04x), expected 'BA'", (unsigned) ah.type);
			invalid = true;
			rp->lasterr = BMP_ERR_HEADER;
			break;
		}

		memcpy(&imgs[n].ah, &ah, sizeof ah);

		imgs[n].handle = bmpread_new(rp->file);
		if (imgs[n].handle) {
			if (BMP_RESULT_OK == bmpread_load_info(imgs[n].handle)) {
				((BMPREAD)imgs[n].handle)->is_arrayimg = true;
				n++;
			} else {
				bmp_free(imgs[n].handle);
				invalid = true;
				rp->lasterr = BMP_ERR_HEADER;
				break;
			}
		} else {
			logerr(rp->c.log, "Failed to create handle for array image");
			invalid = true;
			rp->lasterr = BMP_ERR_MEMORY;
			break;
		}

		if (!ah.offsetnext)
			break;

#if ( LONG_MAX <= 0x7fffffffL )
		if (ah.offsetnext > (unsigned long)LONG_MAX) {
			logerr(rp->c.log, "Invalid offset to next array image: %lu", (unsigned long)ah.offsetnext);
			invalid = true;
			rp->lasterr = BMP_ERR_HEADER;
			break;
		}
#endif
		if (fseek(rp->file, ah.offsetnext, SEEK_SET)) {
			logsyserr(rp->c.log, "Seeking next array header");
			invalid = true;
			rp->lasterr = BMP_ERR_FILEIO;
			break;
		}

		if (!s_read_array_header(rp, &ah)) {
			invalid = true;
			break;
		}
	}

	rp->arrayimgs  = imgs;
	rp->narrayimgs = n;

	return !invalid;
}


/********************************************************
 * 	s_read_array_header
 *******************************************************/

static bool s_read_array_header(BMPREAD_R rp, struct Bmparray *ah)
{
	if (read_u16_le(rp->file, &ah->type)        &&
	    read_u32_le(rp->file, &ah->size)        &&
	    read_u32_le(rp->file, &ah->offsetnext)  &&
	    read_u16_le(rp->file, &ah->screenwidth) &&
	    read_u16_le(rp->file, &ah->screenheight)) {

		return true;
	}

	if (feof(rp->file)) {
		logerr(rp->c.log, "unexpected end-of-file while reading "
				"array header");
		rp->lasterr = BMP_ERR_TRUNCATED;
	} else {
		logsyserr(rp->c.log, "error reading array header");
		rp->lasterr = BMP_ERR_FILEIO;
	}

	return false;
}


/********************************************************
 * 	s_array_header_from_file_header
 *******************************************************/

static void s_array_header_from_file_header(struct Bmparray *ah, struct Bmpfile *fh)
{
	ah->type = fh->type;
	ah->size = fh->size;
	ah->offsetnext   = (uint32_t)fh->reserved2 << 16 | fh->reserved1;
	ah->screenwidth  = fh->offbits & 0xffff;
	ah->screenheight = (fh->offbits >> 16) & 0xffff;
}


/********************************************************
 * 	icon_load_masks
 *******************************************************/

long icon_load_masks(BMPREAD_R rp)
{
	/* OS/2 icons and pointers contain 1-bit AND and XOR masks, stacked in a single
	 * image. For monochrome (IC/PT), that's all the image; for color (CI/CP), these are
	 * followed by a complete color image (including headers), the masks are only used
	 * for transparency information.
	 */

	BMPHANDLE      hmono = NULL;
	BMPREAD        rpmono;
	unsigned char *monobuf = NULL;
	size_t         bufsize;
	unsigned       bmptype = rp->fh->type;
	long           posmono = 0, poscolor = 0;

	if (fseek(rp->file, -14, SEEK_CUR)) {
		logsyserr(rp->c.log, "Seeking to start of icon/pointer");
		goto abort;
	}

	if (-1 == (posmono = ftell(rp->file))) {
		logsyserr(rp->c.log, "Saving file position");
		goto abort;
	}

	/* first,  load monochrome XOR/AND bitmap. We'll use the
	 * AND bitmap as alpha channel for the color bitmap
	 */

	if (!(hmono = bmpread_new(rp->file))) {
		logerr(rp->c.log, "Getting handle for monochrome XOR/AND map");
		goto abort;
	}
	rpmono = cm_read_handle(hmono);

	rpmono->read_state = RS_EXPECT_ICON_MASK;
	if (BMP_RESULT_OK != bmpread_load_info(hmono)) {
		logerr(rp->c.log, "%s", bmp_errmsg(hmono));
		goto abort;
	}

	if (rpmono->fh->type != bmptype) {
		logerr(rp->c.log, "File type mismatch. Have 0x%04x, expected 0x%04x",
		                                             (unsigned)rpmono->fh->type, bmptype);
	}

	if (rp->fh->type == BMPFILE_CI || rp->fh->type == BMPFILE_CP) {
		if (-1 == (poscolor = ftell(rp->file))) {
			logsyserr(rp->c.log, "Saving position of color header");
			goto abort;
		}
	}

	if (!(rpmono->width > 0 && rpmono->height > 0 && rpmono->width <=512 && rpmono->height <= 512)) {
		logerr(rp->c.log, "Invalid icon/pointer dimensions: %dx%d", rpmono->width, rpmono->height);
		goto abort;
	}

	if (rpmono->ih->bitcount != 1) {
		logerr(rp->c.log, "Invalid icon/pointer monochrome bitcount: %d", rpmono->ih->bitcount);
		goto abort;
	}

	if (rpmono->height & 1) {
		logerr(rp->c.log, "Invalid odd icon/pointer height: %d (must be even)", rpmono->height);
		goto abort;
	}

	int width, height, bitsperchannel, channels;
	if (BMP_RESULT_OK != bmpread_dimensions(hmono, &width, &height, &channels, &bitsperchannel, NULL)) {
		logerr(rp->c.log, "%s", bmp_errmsg(hmono));
		goto abort;
	}

	height /= 2; /* mochrome contains two stacked bitmaps (AND and XOR) */

	if (channels != 3 || bitsperchannel != 8) {
		logerr(rp->c.log, "Unexpected result color depth for monochrome image: "
		                             "%d channels, %d bits/channel", channels, bitsperchannel);
		goto abort;
	}

	/* store the AND/XOR bitmaps in the main BMPREAD struct */

	bufsize = bmpread_buffersize(hmono);
	if (BMP_RESULT_OK != bmpread_load_image(hmono, &monobuf)) {
		logerr(rp->c.log, "%s", bmp_errmsg(hmono));
		goto abort;
	}

	if (!(bufsize > 0 && monobuf != NULL)) {
		logerr(rp->c.log, "Panic! unkown error while loading monochrome bitmap");
		goto abort;
	}

	if (!(rp->icon_mono_and = malloc(width * height))) {
		logsyserr(rp->c.log, "Allocating mono AND bitmap");
		goto abort;
	}
	if (!(rp->icon_mono_xor = malloc(width * height))) {
		logsyserr(rp->c.log, "Allocating mono XOR bitmap");
		goto abort;
	}

	for (int i = 0; i < width * height; i++)
		rp->icon_mono_and[i] = 255 - monobuf[3 * i];

	for (int i = 0; i < width * height; i++)
		rp->icon_mono_xor[i] = monobuf[3 * (width * height + i)];

	rp->icon_mono_width  = width;
	rp->icon_mono_height = height;
	free(monobuf);
	monobuf = NULL;
	bmp_free(hmono);
	hmono = NULL;

	if (rp->fh->type == BMPFILE_CI || rp->fh->type == BMPFILE_CP)
		return poscolor;

	return posmono;

abort:
	if (hmono)
		bmp_free(hmono);
	if (monobuf)
		free(monobuf);
	return -1;
}
