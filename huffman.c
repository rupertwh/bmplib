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
#ifndef __STDC_NO_THREADS__
#include <threads.h>
#endif

#include "config.h"
#include "bmplib.h"
#include "logging.h"
#include "bmp-common.h"
#include "reversebits.h"
#include "huffman-codes.h"
#include "huffman.h"



struct Node {
	struct Node *l;
	struct Node *r;
	int    value;
	int    terminal;
	int    makeup;
};

static int         nnodes = 0;
static struct Node nodebuffer[2 * (ARR_SIZE(huff_term_black) + ARR_SIZE(huff_makeup_black)) + 1
                            + 2 * (ARR_SIZE(huff_term_white) + ARR_SIZE(huff_makeup_white)) + 1];

static struct Node *black_tree;
static struct Node *white_tree;


static void s_buildtree(void);
static void add_node(struct Node **node, const char *bits, int value, int makeup);
static int s_findnode(uint32_t bits, int nbits, int black, struct Node **found);


#ifndef __STDC_NO_THREADS__
static once_flag build_once = ONCE_FLAG_INIT;
#else
static int initialized = FALSE;
#endif



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
	struct Node *node;
	int    bits_used = 0;
	int    result = 0;

#ifndef __STDC_NO_THREADS__
	call_once(&build_once, s_buildtree);
#else
	if (!initialized) {
		s_buildtree();
		initialized = TRUE;
	}
#endif

	do {
		huff_fillbuf(rp);
		bits_used = s_findnode(rp->hufbuf, rp->hufbuf_len, black, &node);
		if (!node) {
			/* invalid code */
			return -1;
		}

		result += node->value;
		rp->hufbuf >>= bits_used;
		rp->hufbuf_len -= bits_used;

	} while (node->makeup && result < INT_MAX - 2560);

	return node->makeup ? -1 : result;
}


static int s_findnode(uint32_t bits, int nbits, int black, struct Node **found)
{
	struct Node *node;
	int          bits_used = 0;

	node = black ? black_tree : white_tree;

	while (node && !node->terminal && bits_used < nbits) {
		if (bits & 1)
			node = node->r;
		else
			node = node->l;
		bits_used++;
		bits >>= 1;
	}
	*found = node;
	return node ? bits_used : 0;
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



/*****************************************************************************
 * s_buildtree()
 ****************************************************************************/

static void s_buildtree(void)
{
	int i;

	memset(nodebuffer, 0, sizeof nodebuffer);

	for (i = 0; i < ARR_SIZE(huff_term_black); i++) {
		add_node(&black_tree, huff_term_black[i].bits,
		                      huff_term_black[i].number, FALSE);
	}
	for (i = 0; i < ARR_SIZE(huff_makeup_black); i++) {
		add_node(&black_tree, huff_makeup_black[i].bits,
		                      huff_makeup_black[i].number, TRUE);
	}

	for (i = 0; i < ARR_SIZE(huff_term_white); i++) {
		add_node(&white_tree, huff_term_white[i].bits,
		                      huff_term_white[i].number, FALSE);
	}
	for (i = 0; i < ARR_SIZE(huff_makeup_white); i++) {
		add_node(&white_tree, huff_makeup_white[i].bits,
		                      huff_makeup_white[i].number, TRUE);
	}
}



/*****************************************************************************
 * add_node()
 ****************************************************************************/

static void add_node(struct Node **node, const char *bits, int value, int makeup)
{
	if (!*node) {
		*node = &nodebuffer[nnodes++];
	}

	if (!*bits) {
		/* we are on the final bit of the sequence */
		(*node)->value    = value;
		(*node)->terminal = TRUE;
		(*node)->makeup   = makeup;
	} else {
		switch (*bits) {
		case '0':
			add_node(&((*node)->l), bits+1, value, makeup);
			break;
		case '1':
			add_node(&((*node)->r), bits+1, value, makeup);
			break;
		}
	}
}



#ifdef NEVER

/* keeping these around for a while in case I want to revisit
 * construction of the trees.
 */
static void print_table(struct Huffcode *table, int size)
{
	int i;
	unsigned long buf;

	for (i = 0; i < size; i++) {
		buf = 0;
		for (int j = 0; j < strlen(table[i].bits); j++) {
			buf <<= 1;
			buf |= table[i].bits[j] == '1' ? 1 : 0;
		}
		printf ("%02d: 0x%02x, %d, %s\n", i, (int) buf, (int) strlen(table[i].bits), table[i].bits);
	}
}

static int depth = 0;

static void find_empty_nodes(struct Node *node)
{
	static char str[30];

	str[depth] = 0;
	if (!node->terminal) {
		if (!(node->l && node->r)) {
			str[depth] = 0;
			printf("node %s not full. l=%p r=%p\n", str, (void*)node->l, (void*)node->r);
		}
		if (node->l) {
			str[depth++] = '0';
			find_empty_nodes(node->l);
		}
		if (node->r) {
			str[depth++] = '1';
			find_empty_nodes(node->r);
		}
	} else {
		//printf("%d: %s\n", node->value, str);
	}
	depth--;
}
#endif
















