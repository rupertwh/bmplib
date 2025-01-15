/* bmplib - gen-reversebits.c
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

#include <stdio.h>

static int reverse(int val, int bits)
{
	int mask;

	bits /= 2;
	if (bits == 0)
		return val;
	mask = (1 << bits) - 1;
	return (reverse(val & mask, bits) << bits) | reverse(val >> bits, bits);
}



int main(int argc, char *argv[])
{
	int         reversed, i;
	FILE       *file;
	const char *src_name  = "reversebits.h";
	const char *this_name = "gen-reversebits.c";

	if (argc == 2) {
		if (!(file = fopen(argv[1], "w"))) {
			perror(argv[1]);
			return 1;
		}
	} else {
		file = stdout;
	}

	fprintf(file, "/* bmplib - %s\n", src_name);
	fprintf(file, " *\n"
	              " * Copyright (c) 2024, Rupert Weber.\n"
	              " *\n"
	              " * This file is part of bmplib.\n"
	              " * bmplib is free software: you can redistribute it and/or modify\n"
	              " * it under the terms of the GNU Lesser General Public License as\n"
	              " * published by the Free Software Foundation, either version 3 of\n"
	              " * the License, or (at your option) any later version.\n"
	              " *\n");
	fprintf(file, " * This program is distributed in the hope that it will be useful,\n"
	              " * but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	              " * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n"
	              " * GNU Lesser General Public License for more details.\n"
	              " *\n"
	              " * You should have received a copy of the GNU Lesser General Public\n"
	              " * License along with this library.\n"
	              " * If not, see <https://www.gnu.org/licenses/>\n"
	              " */\n\n");
	fprintf(file, "/* This file is auto-generated by %s */\n\n\n", this_name);

	fprintf(file, "static const unsigned char reversebits[] = {\n\t");
	for (i = 0; i < 256; i++) {
		reversed = reverse(i, 8);
		fprintf(file, "0x%02x, ", reversed);
		if ((i + 1) % 8 == 0 && i < 255)
			fprintf(file, "\n\t");
	}
	fprintf(file, "\n};\n");
	return 0;
}
