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

#include "bmp-write.c"

#include "test-fileio-pipes.h"

#define ARRAY_LEN(a) ((int)(sizeof (a) / sizeof ((a)[0])))

int main(int argc, const char **argv)
{
	const char *func, *subtest ="";

	if (argc < 2) {
		fprintf(stderr, "Invalid invocation\n");
		return 2;
	}
	func = argv[1];
	if (argc >= 3)
		subtest = argv[2];



	if (!strcmp(func, "write_u32_le")) {
		const int expected_len = 4;

		struct {
			uint32_t       value;
			unsigned char *expected;
		} data[] = {
			{ .expected = (unsigned char[]){0x00,0x00,0x00,0x00}, .value = 0 },
			{ .expected = (unsigned char[]){0x01,0x00,0x00,0x00}, .value = 1 },
			{ .expected = (unsigned char[]){0xfe,0xff,0xff,0xff}, .value = 0xfffffffeUL },
			{ .expected = (unsigned char[]){0xff,0xff,0xff,0xff}, .value = 0xffffffffUL },
			{ .expected = (unsigned char[]){0x12,0x34,0x56,0x78}, .value = 0x78563412UL },
		};

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			struct WritePipe *wp = NULL;
			unsigned char     buf[expected_len];

			FILE *file = open_write_pipe(&wp);
			if (!file)
				return 2;

			if (!write_u32_le(file, data[i].value)) {
				perror(func);
				return 3;
			}
			fflush(file); /* important, otherwise read will get stuck */

			int nbytes = data_from_write_pipe(wp, buf, (int) sizeof buf);
			fclose(file);
			if (nbytes != expected_len) {
				printf("%s() failed on dataset %d (%lu):\n", func, i, (unsigned long)data[i].value);
				printf("expected to read %d bytes, got %d bytes\n", expected_len, nbytes);
				return 1;
			}
			if (memcmp(data[i].expected, buf, expected_len) != 0) {
				printf("%s() failed on dataset %d (%lu):\n", func, i, (unsigned long)data[i].value);
				printf("expected 0x%02x%02x%02x%02x, got 0x%02x%02x%02x%02x\n",
				                    (unsigned)data[i].expected[0], (unsigned)data[i].expected[1],
				                    (unsigned)data[i].expected[2], (unsigned)data[i].expected[3],
				                    (unsigned)buf[0], (unsigned)buf[1], (unsigned)buf[2], (unsigned)buf[3]);
				return 1;
			}
		}
		return 0;
	}


	if (!strcmp(func, "write_s32_le")) {
		const int expected_len = 4;

		struct {
			int32_t        value;
			unsigned char *expected;
		} data[] = {
			{ .expected = (unsigned char[]){0x00,0x00,0x00,0x00}, .value = 0 },
			{ .expected = (unsigned char[]){0x01,0x00,0x00,0x00}, .value = 1 },
			{ .expected = (unsigned char[]){0xff,0xff,0xff,0xff}, .value = -1 },
			{ .expected = (unsigned char[]){0x00,0x00,0x00,0x80}, .value = -2147483648L },
			{ .expected = (unsigned char[]){0x01,0x00,0x00,0x80}, .value = -2147483647L },
			{ .expected = (unsigned char[]){0xfe,0xff,0xff,0x7f}, .value = 2147483646L },
			{ .expected = (unsigned char[]){0xff,0xff,0xff,0x7f}, .value = 2147483647L },
			{ .expected = (unsigned char[]){0x12,0x34,0x56,0x78}, .value = 2018915346L },
		};

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			struct WritePipe *wp = NULL;
			unsigned char     buf[expected_len];

			FILE *file = open_write_pipe(&wp);
			if (!file)
				return 2;

			if (!write_s32_le(file, data[i].value)) {
				perror(func);
				return 3;
			}
			fflush(file); /* important, otherwise read will get stuck */

			int nbytes = data_from_write_pipe(wp, buf, (int) sizeof buf);
			fclose(file);
			if (nbytes != expected_len) {
				printf("%s() failed on dataset %d (%ld):\n", func, i, (long)data[i].value);
				printf("expected to read %d bytes, got %d bytes\n", expected_len, nbytes);
				return 1;
			}
			if (memcmp(data[i].expected, buf, expected_len) != 0) {
				printf("%s() failed on dataset %d (%ld):\n", func, i, (long)data[i].value);
				printf("expected 0x%02x%02x%02x%02x, got 0x%02x%02x%02x%02x\n",
				                    (unsigned)data[i].expected[0], (unsigned)data[i].expected[1],
				                    (unsigned)data[i].expected[2], (unsigned)data[i].expected[3],
				                    (unsigned)buf[0], (unsigned)buf[1], (unsigned)buf[2], (unsigned)buf[3]);
				return 1;
			}
		}
		return 0;
	}


	if (!strcmp(func, "write_u16_le")) {
		const int expected_len = 2;

		struct {
			uint16_t       value;
			unsigned char *expected;
		} data[] = {
			{ .expected = (unsigned char[]){0x00,0x00}, .value = 0 },
			{ .expected = (unsigned char[]){0x01,0x00}, .value = 1 },
			{ .expected = (unsigned char[]){0xfe,0xff}, .value = 65534 },
			{ .expected = (unsigned char[]){0xff,0xff}, .value = 65535 },
		};

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			struct WritePipe *wp = NULL;
			unsigned char     buf[expected_len];

			FILE *file = open_write_pipe(&wp);
			if (!file)
				return 2;

			if (!write_u16_le(file, data[i].value)) {
				perror(func);
				return 3;
			}
			fflush(file); /* important, otherwise read will get stuck */

			int nbytes = data_from_write_pipe(wp, buf, (int) sizeof buf);
			fclose(file);
			if (nbytes != expected_len) {
				printf("%s() failed on dataset %d (%u):\n", func, i, (unsigned)data[i].value);
				printf("expected to read %d bytes, got %d bytes\n", expected_len, nbytes);
				return 1;
			}
			if (memcmp(data[i].expected, buf, expected_len) != 0) {
				printf("%s() failed on dataset %d (%u):\n", func, i, (unsigned)data[i].value);
				printf("expected 0x%02x%02x, got 0x%02x%02x\n",
				                    (unsigned)data[i].expected[0], (unsigned)data[i].expected[1],
				                    (unsigned)buf[0], (unsigned)buf[1]);
				return 1;
			}
		}
		return 0;
	}


	if (!strcmp(func, "write_s16_le")) {
		const int expected_len = 2;

		struct {
			int16_t        value;
			unsigned char *expected;
		} data[] = {
			{ .expected = (unsigned char[]){0x00,0x00}, .value = 0 },
			{ .expected = (unsigned char[]){0x01,0x00}, .value = 1 },
			{ .expected = (unsigned char[]){0x00,0x80}, .value = -32768 },
			{ .expected = (unsigned char[]){0x01,0x80}, .value = -32767 },
			{ .expected = (unsigned char[]){0xfe,0x7f}, .value = 32766 },
			{ .expected = (unsigned char[]){0xff,0x7f}, .value = 32767 },
		};

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			struct WritePipe *wp = NULL;
			unsigned char     buf[expected_len];

			FILE *file = open_write_pipe(&wp);
			if (!file)
				return 2;

			if (!write_s16_le(file, data[i].value)) {
				perror(func);
				return 3;
			}
			fflush(file); /* important, otherwise read will get stuck */

			int nbytes = data_from_write_pipe(wp, buf, (int) sizeof buf);
			fclose(file);
			if (nbytes != expected_len) {
				printf("%s() failed on dataset %d (%d):\n", func, i, (int)data[i].value);
				printf("expected to read %d bytes, got %d bytes\n", expected_len, nbytes);
				return 1;
			}
			if (memcmp(data[i].expected, buf, expected_len) != 0) {
				printf("%s() failed on dataset %d (%d):\n", func, i, (int)data[i].value);
				printf("expected 0x%02x%02x, got 0x%02x%02x\n",
				                    (unsigned)data[i].expected[0], (unsigned)data[i].expected[1],
				                    (unsigned)buf[0], (unsigned)buf[1]);
				return 1;
			}
		}
		return 0;
	}


	if (!strcmp(func, "s_imgrgb_to_outbytes") && !strcmp(subtest, "int")) {
		struct Bmpwrite bw = { 0 };

		struct {
			int                  src_channels;
			bool                 src_has_alpha;
			BMPFORMAT            src_format;
			int                  src_bitsperchannel;
			bool                 out_64bit;
			struct Colormask     masks;
			const unsigned char *imgbytes;
			unsigned long long   expected;
		} data[] = {
			{ .src_channels = 1, .src_has_alpha = false, .src_format = BMP_FORMAT_INT,
			  .src_bitsperchannel = 8, .out_64bit = false,
			  .masks.mask   = { .red = 0xff, .green = 0xff, .blue = 0xff },
			  .masks.shift  = { .red = 0, .green = 8, .blue = 16 },
			  .masks.bits   = { .red = 8, .green = 8, .blue = 8},
			  .masks.maxval = { .red = 255.0, .green = 255.0, .blue = 255.0},
			  .imgbytes = (unsigned char[]){ 0x45 },
			  .expected = 0x00454545ULL,
			},
			{ .src_channels = 3, .src_has_alpha = false, .src_format = BMP_FORMAT_INT,
			  .src_bitsperchannel = 8, .out_64bit = false,
			  .masks.mask   = { .red = 0xff, .green = 0xff, .blue = 0xff },
			  .masks.shift  = { .red = 0, .green = 8, .blue = 16 },
			  .masks.bits   = { .red = 8, .green = 8, .blue = 8},
			  .masks.maxval = { .red = 255.0, .green = 255.0, .blue = 255.0},
			  .imgbytes = (unsigned char[]){ 0x12, 0x34, 0x45 },
			  .expected = 0x00453412ULL,
			},
			{ .src_channels = 4, .src_has_alpha = true, .src_format = BMP_FORMAT_INT,
			  .src_bitsperchannel = 8, .out_64bit = false,
			  .masks.mask   = { .red = 0xff, .green = 0xff, .blue = 0xff, .alpha = 0xff },
			  .masks.shift  = { .red = 0, .green = 8, .blue = 16, .alpha = 24 },
			  .masks.bits   = { .red = 8, .green = 8, .blue = 8, .alpha = 8},
			  .masks.maxval = { .red = 255.0, .green = 255.0, .blue = 255.0, .alpha = 255.0 },
			  .imgbytes = (unsigned char[]){ 0x12, 0x34, 0x45, 0x67 },
			  .expected = 0x67453412ULL,
			},
			{ .src_channels = 4, .src_has_alpha = true, .src_format = BMP_FORMAT_INT,
			  .src_bitsperchannel = 8, .out_64bit = false,
			  .masks.mask   = { .red = 0x3ff, .green = 0x3ff, .blue = 0x3ff, .alpha = 0x1 },
			  .masks.shift  = { .red = 0, .green = 10, .blue = 20, .alpha = 30 },
			  .masks.bits   = { .red = 10, .green = 10, .blue = 10, .alpha = 1 },
			  .masks.maxval = { .red = 1023.0 , .green = 1023.0 , .blue = 1023.0 , .alpha = 1.0 },
			  .imgbytes = (unsigned char[]){ 0x79, 0x11, 0xe6, 0xff },
			  .expected = 0x79b111e5ULL,
			},
			{ .src_channels = 4, .src_has_alpha = true, .src_format = BMP_FORMAT_INT,
			  .src_bitsperchannel = 16, .out_64bit = false,
			  .masks.mask   = { .red = 0xff, .green = 0x1ff, .blue = 0x3ff, .alpha = 0x1f },
			  .masks.shift  = { .red = 0, .green = 8, .blue = 17, .alpha = 27 },
			  .masks.bits   = { .red = 8, .green = 9, .blue = 10, .alpha = 5 },
			  .masks.maxval = { .red = 255.0 , .green = 511.0 , .blue = 1023.0 , .alpha = 31.0 },

			  .imgbytes = (unsigned char[]){ 0x79, 0x00, 0x32, 0x09, 0x45, 0xf5, 0x00, 0x80 },
			  .expected = 0x87a81200ULL,
			},
			{ .src_channels = 4, .src_has_alpha = true, .src_format = BMP_FORMAT_INT,
			  .src_bitsperchannel = 32, .out_64bit = false,
			  .masks.mask   = { .red = 0xff, .green = 0x1ff, .blue = 0x3ff, .alpha = 0x1f },
			  .masks.shift  = { .red = 0, .green = 8, .blue = 17, .alpha = 27 },
			  .masks.bits   = { .red = 8, .green = 9, .blue = 10, .alpha = 5 },
			  .masks.maxval = { .red = 255.0 , .green = 511.0 , .blue = 1023.0 , .alpha = 31.0 },

			  .imgbytes = (unsigned char[]){ 0xf8, 0x15, 0xd3, 0x89, 0xcc, 0x15, 0x6f, 0x55,
			                                 0x5f, 0xd5, 0xfb, 0x1d, 0x19, 0xc8, 0x60, 0x36 },
			  .expected = 0x38f0ab89ULL,
			},
		};


		for (int i = 0; i < ARRAY_LEN(data); i++) {
			bw.source_channels = data[i].src_channels;
			bw.source_has_alpha = data[i].src_has_alpha;
			bw.source_format    = data[i].src_format;
			bw.source_bitsperchannel = data[i].src_bitsperchannel;
			bw.out64bit = data[i].out_64bit;
			memcpy(&bw.cmask, &data[i].masks, sizeof data[i].masks);

			unsigned long long result = s_imgrgb_to_outbytes(&bw, data[i].imgbytes);
			if (result != data[i].expected) {
				printf("%s() - %s failed on dataset %d:\n", func, subtest, i);
				printf("expected 0x%016llx, got 0x%016llx\n",
				                    data[i].expected, result);
				return 1;
			}
		}

		return 0;
	}


	if (!strcmp(func, "s_imgrgb_to_outbytes") && !strcmp(subtest, "float")) {
		struct Bmpwrite bw = { 0 };

		struct {
			int                  src_channels;
			bool                 src_has_alpha;
			BMPFORMAT            src_format;
			int                  src_bitsperchannel;
			bool                 out_64bit;
			struct Colormask     masks;
			const float         *imgvals;
			unsigned long long   expected;
		} data[] = {
			{ .src_channels = 4, .src_has_alpha = true, .src_format = BMP_FORMAT_FLOAT,
			  .src_bitsperchannel = 32, .out_64bit = false,
			  .masks.mask   = { .red = 0xff, .green = 0x1ff, .blue = 0x3ff, .alpha = 0x1f },
			  .masks.shift  = { .red = 0, .green = 8, .blue = 17, .alpha = 27 },
			  .masks.bits   = { .red = 8, .green = 9, .blue = 10, .alpha = 5 },
			  .masks.maxval = { .red = 255.0 , .green = 511.0 , .blue = 1023.0 , .alpha = 31.0 },
			  .imgvals = (float[]){ 0.3, 1.0, 0.78923, 0.6 },
			  .expected = 0x9e4fff4dULL,
			},
			{ .src_channels = 4, .src_has_alpha = true, .src_format = BMP_FORMAT_FLOAT,
			  .src_bitsperchannel = 32, .out_64bit = false,
			  .masks.mask   = { .red = 0xff, .green = 0x1ff, .blue = 0x3ff, .alpha = 0x1f },
			  .masks.shift  = { .red = 0, .green = 8, .blue = 17, .alpha = 27 },
			  .masks.bits   = { .red = 8, .green = 9, .blue = 10, .alpha = 5 },
			  .masks.maxval = { .red = 255.0 , .green = 511.0 , .blue = 1023.0 , .alpha = 31.0 },
			  .imgvals = (float[]){ 0.3, 3.2, 0.78923, -1.5 },
			  .expected = 0x64fff4dULL,

			},
		};

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			bw.source_channels = data[i].src_channels;
			bw.source_has_alpha = data[i].src_has_alpha;
			bw.source_format    = data[i].src_format;
			bw.source_bitsperchannel = data[i].src_bitsperchannel;
			bw.out64bit = data[i].out_64bit;
			memcpy(&bw.cmask, &data[i].masks, sizeof data[i].masks);

			unsigned long long result = s_imgrgb_to_outbytes(&bw, (unsigned char*)data[i].imgvals);
			if (result != data[i].expected) {
				printf("%s() - %s failed on dataset %d:\n", func, subtest, i);
				printf("expected 0x%016llx, got 0x%016llx\n",
				                    data[i].expected, result);
				return 1;
			}
		}

		return 0;
	}


	if (!strcmp(func, "s_imgrgb_to_outbytes") && !strcmp(subtest, "s2.13")) {
		struct Bmpwrite bw = { 0 };

		struct {
			int                  src_channels;
			bool                 src_has_alpha;
			BMPFORMAT            src_format;
			int                  src_bitsperchannel;
			bool                 out_64bit;
			struct Colormask     masks;
			const unsigned char *imgbytes;
			unsigned long long   expected;
		} data[] = {
			{ .src_channels = 4, .src_has_alpha = true, .src_format = BMP_FORMAT_S2_13,
			  .src_bitsperchannel = 16, .out_64bit = false,
			  .masks.mask   = { .red = 0xff, .green = 0x7ff, .blue = 0x3ff, .alpha = 0x7 },
			  .masks.shift  = { .red = 0, .green = 8, .blue = 19, .alpha = 29 },
			  .masks.bits   = { .red = 8, .green = 11, .blue = 10, .alpha = 3 },
			  .masks.maxval = { .red = 255.0 , .green = 2047.0 , .blue = 1023.0 , .alpha = 7.0 },
			  .imgbytes = (unsigned char[]){ 0x81, 0x0e, 0x7d, 0x67, 0x41, 0x19, 0x68, 0xb1 },
			  .expected = 0x193fff74ULL,

			},
		};

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			bw.source_channels = data[i].src_channels;
			bw.source_has_alpha = data[i].src_has_alpha;
			bw.source_format    = data[i].src_format;
			bw.source_bitsperchannel = data[i].src_bitsperchannel;
			bw.out64bit = data[i].out_64bit;
			memcpy(&bw.cmask, &data[i].masks, sizeof data[i].masks);

			unsigned long long result = s_imgrgb_to_outbytes(&bw, (unsigned char*)data[i].imgbytes);
			if (result != data[i].expected) {
				printf("%s() - %s failed on dataset %d:\n", func, subtest, i);
				printf("expected 0x%016llx, got 0x%016llx\n",
				                    data[i].expected, result);
				return 1;
			}
		}

		return 0;
	}


	fprintf(stderr, "Invalid test '%s'\n", func);
	return 2;
}
