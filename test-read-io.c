/* bmplib - test-read_s2_13.c
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

#include "bmp-read-loadimage.c"

#include "test-fileio-pipes.h"

#define ARRAY_LEN(a) ((int)(sizeof (a) / sizeof ((a)[0])))

int main(int argc, const char **argv)
{
	const char *func /*, *subtest =""*/;

	if (argc < 2) {
		fprintf(stderr, "Invalid invocation\n");
		return 2;
	}
	func = argv[1];
	/*if (argc >= 3)
		subtest = argv[2];*/


	if (!strcmp(func, "s_buffer32_fill")) {
		struct Bmpread br = { 0 };
		struct Buffer32 buf32;

		struct {
			unsigned char *filedata;
			size_t         datalen;
			uint32_t       expected_data;
			int            expected_n;
			bool           expected_return;
		} data[] = {
			{ .filedata = (unsigned char[]){0x00,0x00,0x00,0x00},
			  .datalen = 4,
			  .expected_n = 32,
			  .expected_data = 0,
			  .expected_return = true },
			{ .filedata = (unsigned char[]){0x01,0x02,0x03,0x04},
			  .datalen = 4,
			  .expected_n = 32,
			  .expected_data = 0x01020304UL,
			  .expected_return = true },
			{ .filedata = (unsigned char[]){0x01,0x02,0x03},
			  .datalen = 3,
			  .expected_n = 24,
			  .expected_data = 0x01020300UL,
			  .expected_return = true },
			{ .filedata = (unsigned char[]){ 0 },
			  .datalen = 0,
			  .expected_n = 0,
			  .expected_data = 0x0UL,
			  .expected_return = false },
		};

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			br.file = provide_as_file(data[i].filedata, data[i].datalen);
			if (!br.file)
				return 1;
			bool result = s_buffer32_fill(&br, &buf32);
			if (result != data[i].expected_return || buf32.buffer != data[i].expected_data ||
			                                         buf32.n != data[i].expected_n) {
				printf("%s() failed on dataset %d:\n", func, i);
				printf("expected %s/0x%08lx/%d, got %s/0x%08lx/%d\n", data[i].expected_return ? "true" : "false",
				                                                (unsigned long)data[i].expected_data,
				                                                data[i].expected_n,
				                                                result ? "true" : "false",
				                                                (unsigned long)buf32.buffer, buf32.n);
				return 1;
			fclose(br.file);
			}
		}

		return 0;
	}

	if (!strcmp(func, "s_buffer32_bits")) {
		struct Buffer32 buf32 = { 0 };

		struct {
			uint32_t buffer;
			int      n;
			int      request;
			uint32_t expected;
		} data[] = {
			{ .buffer = 0x0f000000UL, .n =  8, .request = 8, .expected = 0x0f },
			{ .buffer = 0x34ffffffUL, .n = 16, .request = 4, .expected = 0x03 },
			{ .buffer = 0x1234ffffUL, .n = 32, .request = 8, .expected = 0x12 },
			{ .buffer = 0x8234ffffUL, .n = 16, .request = 2, .expected = 0x02 },
			{ .buffer = 0x8234ffffUL, .n = 16, .request = 1, .expected = 0x01 },
		};

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			buf32.buffer = data[i].buffer;
			buf32.n      = data[i].n;
			uint32_t result = s_buffer32_bits(&buf32, data[i].request);
			if (result != data[i].expected) {
				printf("%s() failed on dataset %d:\n", func, i);
				printf("expected 0x%08lx, got 0x%08lx\n",
				                    (unsigned long)data[i].expected,
				                    (unsigned long)result);
				return 1;
			}
		}

		return 0;
	}

	fprintf(stderr, "Invalid test '%s'\n", func);

	return 2;
}
