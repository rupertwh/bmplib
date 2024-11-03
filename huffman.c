/* bmplib - huffman.c
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
#include "reversebits.h"
#include "huffman.h"
#include "huffman-codes.h"



static int s_findnode(uint32_t bits, int nbits, int black, int *found);



/*****************************************************************************
 * huff_decode()
 *
 * Decodes the rp->hufbuf_len long bit sequence given in rp->hufbuf.
 * Direction is from lowest to highest bit.
 * Returns -1 if no valid terminating code is found.
 * EOL is _not_ handled, must be done by caller.
 ****************************************************************************/

int huff_decode(BMPREAD_R rp, int black)
{
	int    idx;
	int    bits_used = 0;
	int    result = 0;
	do {
		huff_fillbuf(rp);
		bits_used = s_findnode(rp->hufbuf, rp->hufbuf_len, black, &idx);
		if (idx == -1) {
			/* invalid code */
			return -1;
		}

		result += nodebuffer[idx].value;
		rp->hufbuf >>= bits_used;
		rp->hufbuf_len -= bits_used;

	} while (nodebuffer[idx].makeup && result < INT_MAX - 2560);

	return nodebuffer[idx].makeup ? -1 : result;
}



/*****************************************************************************
 * s_findnode()
 ****************************************************************************/

static int s_findnode(uint32_t bits, int nbits, int black, int *found)
{
	int idx;
	int bits_used = 0;

	idx = black ? blackroot : whiteroot;

	while (idx != -1 && !nodebuffer[idx].terminal && bits_used < nbits) {
		if (bits & 1)
			idx = nodebuffer[idx].r;
		else
			idx = nodebuffer[idx].l;
		bits_used++;
		bits >>= 1;
	}
	*found = idx;
	return idx != -1 ? bits_used : 0;
}



/*****************************************************************************
 * huff_fillbuf()
 ****************************************************************************/

void huff_fillbuf(BMPREAD_R rp)
{
	int byte;

	while (rp->hufbuf_len <= 24) {
		if (EOF == (byte = getc(rp->file)))
			break;
		rp->bytes_read++;
		byte = reversebits[byte];
		rp->hufbuf |= ((uint32_t)byte) << rp->hufbuf_len;
		rp->hufbuf_len += 8;
	}
}
