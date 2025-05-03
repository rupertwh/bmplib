/* bmplib - test-read-io.c
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
				return 2;
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


	if (!strcmp(func, "s_read_rgb_pixel")) {
		struct Bmpread br = { 0 };
		struct Bmpinfo ih = { 0 };
		union Pixel    px = { 0 };

		br.ih = &ih;

		struct {
			unsigned char *filedata;
			size_t         datalen;
			int            bitcount;
			uint64_t       mask[4];
			int            shift[4];
			uint32_t       expected[4];
			bool           has_alpha;
			int            result_bitsperchannel;
		} data[] = {
			{ .filedata = (unsigned char[]){0x03,0x02,0x01,0x00},
			  .datalen = 4,
			  .bitcount = 32,
			  .mask = { 0xff0000, 0xff00, 0xff, 0 },
			  .shift = { 16, 8, 0, 0 },
			  .has_alpha = false,
			  .expected = { 1, 2, 3, 255 },
			  .result_bitsperchannel = 8,
			},
			{ .filedata = (unsigned char[]){0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08},
			  .datalen = 8,
			  .bitcount = 64,
			  .mask = { 0xffffULL, 0xffff0000ULL, 0xffff00000000ULL, 0xffff000000000000ULL },
			  .shift = { 0, 16, 32, 48 },
			  .has_alpha = true,
			  .expected = { 0x0201, 0x0403, 0x0605, 0x0807 },
			  .result_bitsperchannel = 16,
			},
			{ .filedata = (unsigned char[]){ 0x12, 0x34 },
			  .datalen = 2,
			  .bitcount = 16,
			  .mask = { 0x0f, 0xf0, 0x0f00, 0xf000 },
			  .shift = { 0, 4, 8, 12 },
			  .has_alpha = true,
			  .expected = { 2, 1, 4, 3 },
			  .result_bitsperchannel = 8,
			},
		};

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			br.file = provide_as_file(data[i].filedata, data[i].datalen);
			if (!br.file)
				return 2;

			br.cmask.mask.red   = data[i].mask[0];
			br.cmask.mask.green = data[i].mask[1];
			br.cmask.mask.blue  = data[i].mask[2];
			br.cmask.mask.alpha = data[i].mask[3];
			br.cmask.shift.red   = data[i].shift[0];
			br.cmask.shift.green = data[i].shift[1];
			br.cmask.shift.blue  = data[i].shift[2];
			br.cmask.shift.alpha = data[i].shift[3];
			br.has_alpha = data[i].has_alpha;
			br.result_bitsperchannel = data[i].result_bitsperchannel;
			ih.bitcount = data[i].bitcount;

			if (!s_read_rgb_pixel(&br, &px)) {
				printf("%s(): EOF or file error (.filedata too short?)\n", func);
				return 2;
			}

			for (int j = 0; j < 4; j++) {
				if (px.value[j] != data[i].expected[j]) {
					printf("%s() failed on dataset %d, val %d:\n", func, i, j);
					printf("expected %d, got %d\n",
						(int)data[i].expected[j],
						(int)px.value[j]);
					return 1;
				}
			}
			fclose(br.file);
			br.file = NULL;
		}
		return 0;
	}

	if (!strcmp(func, "read_u16_le")) {

		struct {
			unsigned char *filedata;
			int            datalen;
			unsigned       expected;
		} data[] = {
			{ .filedata = (unsigned char[]){0x00,0x00},
		          .datalen = 2, .expected = 0 },
			{ .filedata = (unsigned char[]){0x01,0x00},
		          .datalen = 2, .expected = 1 },
			{ .filedata = (unsigned char[]){0xfe,0xff},
		          .datalen = 2, .expected = 65534 },
			{ .filedata = (unsigned char[]){0xff,0xff},
		          .datalen = 2, .expected = 65535 },
		};

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			FILE *file = provide_as_file(data[i].filedata, data[i].datalen);
			if (!file)
				return 2;

			uint16_t result = 0;
			if (!read_u16_le(file, &result)) {
				perror(func);
				return 3;
			}
			if (result != data[i].expected) {
				printf("%s() failed on dataset %d:\n", func, i);
				printf("expected 0x%04x, got 0x%04x\n",
				                    (unsigned)data[i].expected,
				                    (unsigned)result);
				return 1;
			}
		}
		return 0;
	}

	if (!strcmp(func, "read_s16_le")) {

		struct {
			unsigned char *filedata;
			int            datalen;
			int            expected;
		} data[] = {
			{ .filedata = (unsigned char[]){0x00,0x00},
		          .datalen = 2, .expected = 0 },
			{ .filedata = (unsigned char[]){0x01,0x00},
		          .datalen = 2, .expected = 1 },
			{ .filedata = (unsigned char[]){0xff,0xff},
		          .datalen = 2, .expected = -1 },
			{ .filedata = (unsigned char[]){0x00,0x80},
		          .datalen = 2, .expected = -32768 },
			{ .filedata = (unsigned char[]){0x01,0x80},
		          .datalen = 2, .expected = -32767 },
			{ .filedata = (unsigned char[]){0xfe,0x7f},
		          .datalen = 2, .expected = 32766 },
			{ .filedata = (unsigned char[]){0xff,0x7f},
		          .datalen = 2, .expected = 32767 },
		};

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			FILE *file = provide_as_file(data[i].filedata, data[i].datalen);
			if (!file)
				return 2;

			int16_t result = 0;
			if (!read_s16_le(file, &result)) {
				perror(func);
				return 3;
			}
			if (result != data[i].expected) {
				printf("%s() failed on dataset %d:\n", func, i);
				printf("expected %d, got %d\n", (int)data[i].expected,
				                                (int)result);
				return 1;
			}
		}
		return 0;
	}

	if (!strcmp(func, "read_u32_le")) {

		struct {
			unsigned char *filedata;
			int            datalen;
			uint32_t       expected;
		} data[] = {
			{ .filedata = (unsigned char[]){0x00,0x00,0x00,0x00},
		          .datalen = 4, .expected = 0 },
			{ .filedata = (unsigned char[]){0x01,0x00,0x00,0x00},
		          .datalen = 4, .expected = 1 },
			{ .filedata = (unsigned char[]){0xfe,0xff,0xff,0xff},
		          .datalen = 4, .expected = 0xfffffffeUL },
			{ .filedata = (unsigned char[]){0xff,0xff,0xff,0xff},
		          .datalen = 4, .expected = 0xffffffffUL },
			{ .filedata = (unsigned char[]){0x12,0x34,0x56,0x78},
		          .datalen = 4, .expected = 0x78563412UL },
		};

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			FILE *file = provide_as_file(data[i].filedata, data[i].datalen);
			if (!file)
				return 2;

			uint32_t result = 0;
			if (!read_u32_le(file, &result)) {
				perror(func);
				return 3;
			}
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

	if (!strcmp(func, "read_s32_le")) {

		struct {
			unsigned char *filedata;
			int            datalen;
			long           expected;
		} data[] = {
			{ .filedata = (unsigned char[]){0x00,0x00,0x00,0x00},
		          .datalen = 4, .expected = 0 },
			{ .filedata = (unsigned char[]){0x01,0x00,0x00,0x00},
		          .datalen = 4, .expected = 1 },
			{ .filedata = (unsigned char[]){0xff,0xff,0xff,0xff},
		          .datalen = 4, .expected = -1 },
			{ .filedata = (unsigned char[]){0x00,0x00,0x00,0x80},
		          .datalen = 4, .expected = -2147483648L },
			{ .filedata = (unsigned char[]){0x01,0x00,0x00,0x80},
		          .datalen = 4, .expected = -2147483647L },
			{ .filedata = (unsigned char[]){0xfe,0xff,0xff,0x7f},
		          .datalen = 4, .expected = 2147483646L },
			{ .filedata = (unsigned char[]){0xff,0xff,0xff,0x7f},
		          .datalen = 4, .expected = 2147483647L },
			{ .filedata = (unsigned char[]){0x12,0x34,0x56,0x78},
		          .datalen = 4, .expected = 2018915346L },
		};

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			FILE *file = provide_as_file(data[i].filedata, data[i].datalen);
			if (!file)
				return 2;

			int32_t result = 0;
			if (!read_s32_le(file, &result)) {
				perror(func);
				return 3;
			}
			if (result != data[i].expected) {
				printf("%s() failed on dataset %d:\n", func, i);
				printf("expected %ld, got %ld\n", (long)data[i].expected,
				                                  (long)result);
				return 1;
			}
		}
		return 0;
	}

	fprintf(stderr, "Invalid test '%s'\n", func);
	return 2;
}
