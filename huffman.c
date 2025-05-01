/* bmplib - huffman.c
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
#include <limits.h>
#include <stdbool.h>

#include "config.h"
#include "bmplib.h"
#include "logging.h"
#include "bmp-common.h"
#include "huffman.h"
#include "huffman-codes.h"



static int s_findnode(uint32_t bits, int nbits, bool black, int *found);
static bool s_zerofill(BMPWRITE_R wp);


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
	int idx;
	int bits_used = 0;
	int result = 0;

	do {
		huff_fillbuf(rp);
		bits_used = s_findnode(rp->hufbuf, rp->hufbuf_len, black, &idx);
		if (idx == -1) {
			/* invalid code */
			return -1;
		}

		result += nodebuffer[idx].value;
		rp->hufbuf <<= bits_used;
		rp->hufbuf_len -= bits_used;

	} while (nodebuffer[idx].makeup && result < INT_MAX - 2560);

	return nodebuffer[idx].makeup ? -1 : result;
}



/*****************************************************************************
 * s_findnode()
 ****************************************************************************/

static int s_findnode(uint32_t bits, int nbits, bool black, int *found)
{
	int idx;
	int bits_used = 0;

	idx = black ? blackroot : whiteroot;

	while (idx != -1 && !nodebuffer[idx].terminal && bits_used < nbits) {
		if (bits & 0x80000000UL)
			idx = nodebuffer[idx].r;
		else
			idx = nodebuffer[idx].l;
		bits_used++;
		bits <<= 1;
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
		rp->hufbuf |= (uint32_t)byte << (24 - rp->hufbuf_len);
		rp->hufbuf_len += 8;
	}
}



/*****************************************************************************
 * s_push()
 ****************************************************************************/

static bool s_push(BMPWRITE_R wp, int bits, int nbits)
{
	if (nbits > 32 - wp->hufbuf_len) {
		if (!huff_flush(wp))
			return false;
	}
	wp->hufbuf <<= nbits;
	wp->hufbuf |= bits;
	wp->hufbuf_len += nbits;
	return true;
}



/*****************************************************************************
 * huff_encode()
 ****************************************************************************/

bool huff_encode(BMPWRITE_R wp, int val, bool black)
{
	const struct Huffcode *makeup, *term;

	if (val == -1) {
		/* eol */
		return s_push(wp, 1, 12);
	}

	if (black) {
		makeup = huff_makeup_black;
		term   = huff_term_black;
	} else {
		makeup = huff_makeup_white;
		term   = huff_term_white;
	}

	while (val > 63) {
		int n = MIN(2560 / 64, val / 64);
		if (!s_push(wp, makeup[n - 1].bits, makeup[n - 1].nbits))
			return false;
		val -= n * 64;
	}
	if (!s_push(wp, term[val].bits, term[val].nbits))
		return false;

	return true;
}



/*****************************************************************************
 * huff_encode_eol()
 ****************************************************************************/

bool huff_encode_eol(BMPWRITE_R wp)
{
	return huff_encode(wp, -1, 0);
}



/*****************************************************************************
 * huff_encode_rtc()
 ****************************************************************************/

bool huff_encode_rtc(BMPWRITE_R wp)
{
	if (!s_zerofill(wp))
		return false;

	for (int i = 0; i < 6; i++) {
		if (!huff_encode_eol(wp))
			return false;
	}
	return true;
}



/*****************************************************************************
 * s_zerofill()
 *
 * add fill 0s up to next byte boundary
 ****************************************************************************/

static bool s_zerofill(BMPWRITE_R wp)
{
	int n = 8 - wp->hufbuf_len % 8;

	if (n < 8)
		return s_push(wp, 0, n);

	return true;
}



/*****************************************************************************
 * huff_flush()
 ****************************************************************************/

bool huff_flush(BMPWRITE_R wp)
{
	int byte;

	while (wp->hufbuf_len >= 8) {
		byte = 0x00ff & (wp->hufbuf >> (wp->hufbuf_len - 8));
		if (EOF == putc(byte, wp->file)) {
			logsyserr(wp->c.log, "writing Huffman bitmap");
			return false;
		}
		wp->bytes_written++;
		wp->hufbuf_len -= 8;
		wp->hufbuf &= (1UL << wp->hufbuf_len) - 1;
	}
	return true;
}
