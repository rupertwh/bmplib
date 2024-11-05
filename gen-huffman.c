/* bmplib - gen-huffman.c
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


/* This program generates the header file "huffman-codes.h" */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "gen-huffman-codes.h"


#define ARR_SIZE(a) (sizeof a / sizeof a[0])
#define TRUE  1
#define FALSE 0

struct Node {
        int    l;
        int    r;
        int    value;
        int    terminal;
        int    makeup;
};

static int nnodes = 0;
static struct Node nodebuffer[416];

static int black_tree = -1;
static int white_tree = -1;


static void s_buildtree(void);
static void add_node(int *nodeidx, const char *bits, int value, int makeup);
static unsigned short str2bits(const char *str);


int main(int argc, char *argv[])
{
	int          i;
	struct Node *n = nodebuffer;
	FILE        *file;
	const char *src_name  = "huffman-codes.h";
	const char *this_name = "gen-huffman.c";

	if (argc == 2) {
		if (!(file = fopen(argv[1], "w"))) {
			perror(argv[1]);
			return 1;
		}
	} else {
		file = stdout;
	}

	s_buildtree();

	fprintf(file, "/* bmplib - %s\n"
                      " *\n"
                      " * Copyright (c) 2024, Rupert Weber.\n"
                      " *\n"
                      " * This file is part of bmplib.\n"
                      " * bmplib is free software: you can redistribute it and/or modify\n"
                      " * it under the terms of the GNU Lesser General Public License as\n"
                      " * published by the Free Software Foundation, either version 3 of\n"
                      " * the License, or (at your option) any later version.\n"
                      " *\n"
                      " * This program is distributed in the hope that it will be useful,\n"
                      " * but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
                      " * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n"
                      " * GNU Lesser General Public License for more details.\n"
                      " *\n"
                      " * You should have received a copy of the GNU Lesser General Public\n"
                      " * License along with this library.\n"
                      " * If not, see <https://www.gnu.org/licenses/>\n"
                      " */\n\n"
                      "/* This file is auto-generated by %s */\n\n\n",
                src_name, this_name);

	fputs("struct Node {\n"
	      "\tsigned short l;\n"
	      "\tsigned short r;\n"
	      "\tshort        value;\n"
	      "\tchar         terminal;\n"
	      "\tchar         makeup;\n"
	      "};\n\n",
	      file);

	fputs("struct Huffcode {\n"
	      "\tunsigned char bits;\n"
	      "\tunsigned char nbits;\n"
	      "};\n\n",
	      file);

	fprintf(file, "static const int blackroot = %d;\n", black_tree);
	fprintf(file, "static const int whiteroot = %d;\n\n\n", white_tree);
	fprintf(file, "static const struct Node nodebuffer[] = {\n");
	for (i = 0; i < ARR_SIZE(nodebuffer); i++) {
		fprintf(file, "\t{ %3d, %3d, %4d, %d, %d },\n",
		        n[i].l, n[i].r, n[i].value, n[i].terminal, n[i].makeup);
	}
	fputs("};\n\n", file);

	fputs("static const struct Huffcode huff_term_black[] = {\n\t", file);
	for (i = 0; i < ARR_SIZE(huff_term_black); i++) {
		fprintf(file, "{ 0x%02hx, %2d },",
		        str2bits(huff_term_black[i].bits),
		        (int) strlen(huff_term_black[i].bits));
		if ((i+1) % 4 == 0 && i != ARR_SIZE(huff_term_black) - 1)
			fputs("\n\t", file);
		else
			fputs(" ", file);
	}
	fputs("\n};\n\n", file);

	fputs("static const struct Huffcode huff_term_white[] = {\n\t", file);
	for (i = 0; i < ARR_SIZE(huff_term_white); i++) {
		fprintf(file, "{ 0x%02hx, %2d },",
		        str2bits(huff_term_white[i].bits),
		        (int) strlen(huff_term_white[i].bits));
		if ((i+1) % 4 == 0 && i != ARR_SIZE(huff_term_white) - 1)
			fputs("\n\t", file);
		else
			fputs(" ", file);
	}
	fputs("\n};\n\n", file);

	fputs("static const struct Huffcode huff_makeup_black[] = {\n\t", file);
	for (i = 0; i < ARR_SIZE(huff_makeup_black); i++) {
		fprintf(file, "{ 0x%02hx, %2d },",
		        str2bits(huff_makeup_black[i].bits),
		        (int) strlen(huff_makeup_black[i].bits));
		if ((i+1) % 4 == 0 && i != ARR_SIZE(huff_makeup_black) - 1)
			fputs("\n\t", file);
		else
			fputs(" ", file);
	}
	fputs("\n};\n\n", file);

	fputs("static const struct Huffcode huff_makeup_white[] = {\n\t", file);
	for (i = 0; i < ARR_SIZE(huff_makeup_white); i++) {
		fprintf(file, "{ 0x%02hx, %2d },",
		        str2bits(huff_makeup_white[i].bits),
		        (int) strlen(huff_makeup_white[i].bits));
		if ((i+1) % 4 == 0 && i != ARR_SIZE(huff_makeup_white) - 1)
			fputs("\n\t", file);
		else
			fputs(" ", file);
	}
	fputs("\n};\n\n", file);

	return 0;
}


static unsigned short str2bits(const char *str)
{
	unsigned short value = 0;

	while (*str) {
		value <<= 1;
		value |= *str - '0';
		str++;
	}
	return value;
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

static void add_node(int *nodeidx, const char *bits, int value, int makeup)
{
	if (*nodeidx == -1) {
		*nodeidx = nnodes++;
		nodebuffer[*nodeidx].l = -1;
		nodebuffer[*nodeidx].r = -1;
	}
	if (nnodes > ARR_SIZE(nodebuffer)) {
		printf("too many nodes (have %d, max is %d)\n",
		       nnodes, (int) ARR_SIZE(nodebuffer));
		exit(1);
	}

	if (!*bits) {
		/* we are on the final bit of the sequence */
		nodebuffer[*nodeidx].value    = value;
		nodebuffer[*nodeidx].terminal = TRUE;
		nodebuffer[*nodeidx].makeup   = makeup;
	} else {
		switch (*bits) {
		case '0':
			add_node(&nodebuffer[*nodeidx].l, bits+1, value, makeup);
			break;
		case '1':
			add_node(&nodebuffer[*nodeidx].r, bits+1, value, makeup);
			break;
		}
	}
}
