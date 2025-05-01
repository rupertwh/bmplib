/* bmplib - test-read-conversions.c
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

	if (!strcmp(func, "s_float_to_s2_13")) {

		struct {
			double   d;
			uint16_t expected;
		} data[] = {
			{ .d = -4.0, .expected = 0x8000 },
			{ .d = -5.0, .expected = 0x8000 },
			{ .d = -1.0, .expected = 0xe000 },
			{ .d =  0.0, .expected = 0x0000 },
			{ .d =  1.0, .expected = 0x2000 },
			{ .d =  3.99987793, .expected = 0x7fff },
			{ .d =  4.0, .expected = 0x7fff },
			{ .d = 20.0, .expected = 0x7fff },
		};

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			uint16_t s2_13 = s_float_to_s2_13(data[i].d);
			if (s2_13 != data[i].expected) {
				printf("%s() failed on data set %d: %f\n", func, i, data[i].d);
				printf("expected 0x%04x, got 0x%04x\n", data[i].expected, s2_13);
				return 1;
			}
		}
		return 0;
	}


	if (!strcmp(func, "s_s2_13_to_float")) {

		struct {
			uint16_t s2_13;
			double   expected;
		} data[] = {
			{ .s2_13 = 0x2000u, .expected =  1.0},
			{ .s2_13 = 0xe000u, .expected = -1.0},
			{ .s2_13 = 0,       .expected =  0.0},
			{ .s2_13 = 0x7fffu, .expected =  3.99987793},
			{ .s2_13 = 0x8000u, .expected = -4.0},
		};

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			double d = s_s2_13_to_float(data[i].s2_13);
			printf("is %.12f, expected %.12f\n", d, data[i].expected);
			if (fabs(d - data[i].expected) > 0.000000001) {
				printf("%s() failed on data set %d: 0x%04x\n", func, i, data[i].s2_13);
				printf("expected %.12f, got %.12f\n", data[i].expected, d);
				return 1;
			}
		}
		return 0;
	}


	if (!strcmp(func, "roundtrip_s2.13-float-s2.13")) {

		for (uint32_t u = 0; u <= 0xffffu; u++) {
			double d = s_s2_13_to_float(u);
			uint16_t u16 = s_float_to_s2_13(d);
			if (u != u16) {
				printf("%s for 0x%04x failed:\n", func, (unsigned)u);
				printf("expected 0x%04x, got 0x%04x\n", (unsigned)u, (unsigned)u16);
				return 1;
			}
		}
		return 0;
	}


	if (!strcmp(func, "s_convert64")) {

		struct {
			uint16_t	val64[4];
			uint16_t	expected[4];
		} data[] = {
			{ .val64 = {0,      0,      0,      0},      .expected = {0,      0,      0,      0} },
			{ .val64 = {0x2000, 0xe000, 0x2000, 0xffff}, .expected = {0xffff, 0,      0xffff, 0} },
			{ .val64 = {0x4000, 0x1000, 0x4000, 0x1000}, .expected = {0xffff, 0x8000, 0xffff, 0x8000} },
		};

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			s_convert64(data[i].val64);
			for (int j = 0; j < 4; j++) {
				if (data[i].val64[j] != data[i].expected[j]) {
					printf("%s() failed on data set %d\n", func, i);
					printf("expected 0x%04x, got 0x%04x\n", (unsigned)data[i].expected[j],
					                                        (unsigned)data[i].val64[j]);
					return 1;
				}
			}
		}
		return 0;
	}


	if (!strcmp(func, "s_srgb_gamma_float")) {

		struct {
			double lin;
			double expected;
		} data[] = {
			{ .lin = 0.0, .expected =  0.0},
			{ .lin = 1.0, .expected =  1.0},
			{ .lin = 0.1, .expected =  0.349190213},
			{ .lin = 0.5, .expected =  0.735356983},
			{ .lin = 0.9, .expected =  0.954687172},
		};

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			double d = s_srgb_gamma_float(data[i].lin);
			printf("is %.12f, expected %.12f\n", d, data[i].expected);
			if (fabs(d - data[i].expected) > 0.000000001) {
				printf("%s() failed on data set %d: %f\n", func, i, data[i].lin);
				printf("expected %.12f, got %.12f\n", data[i].expected, d);
				return 1;
			}
		}
		return 0;
	}


	if (!strcmp(func, "s_int8_to_result_format") && !strcmp(subtest, "float")) {

		struct Bmpread br = { .result_format = BMP_FORMAT_FLOAT, .result_bitsperchannel = 32 };
		struct {
			int    channels;
			int    rgba[4];
			float  expected[4];
		} data[] = {
			{ .channels = 3, .rgba = { 0, 0, 0, 0}, .expected = { 0.0, 0.0, 0.0, 0.0 } },
			{ .channels = 3, .rgba = { 255, 255, 255, 255}, .expected = { 1.0, 1.0, 1.0, 0.0 } },
			{ .channels = 4, .rgba = { 127, 128, 1, 254}, .expected = { 0.4980392, 0.5019608, 0.0039216, 0.9960784 } },
		};
		float floatbuf[4] = { 0 };

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			br.result_channels = data[i].channels;
			s_int8_to_result_format(&br, data[i].rgba, (unsigned char*) floatbuf);
			//printf("is %.12f, expected %.12f\n", d, data[i].expected);
			for (int j = 0; j < 4; j++) {
				if (fabs(floatbuf[j] - data[i].expected[j]) > 0.0000001) {
					printf("%s()/%s - failed on data set %d:\n", func, subtest, i);
					printf("expected %.12f, got %.12f\n", data[i].expected[j], floatbuf[j]);
					return 1;
				}
			}
		}
		return 0;
	}


	if (!strcmp(func, "s_int8_to_result_format") && !strcmp(subtest, "int")) {

		struct Bmpread br = { .result_format = BMP_FORMAT_INT, .result_bitsperchannel = 8 };
		struct {
			int      channels;
			int      rgba[4];
			unsigned expected[4];
		} data[] = {
			{ .channels = 3, .rgba = { 0, 0, 0, 0}, .expected = { 0, 0, 0, 0 } },
			{ .channels = 3, .rgba = { 255, 255, 255, 255}, .expected = { 255, 255, 255, 0 } },
			{ .channels = 4, .rgba = { 127, 128, 1, 254}, .expected = { 127, 128, 1, 254 } },
		};
		uint8_t int8buf[4] = { 0 };

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			br.result_channels = data[i].channels;
			s_int8_to_result_format(&br, data[i].rgba, (unsigned char*) int8buf);
			for (int j = 0; j < 4; j++) {
				if (int8buf[j] != data[i].expected[j]) {
					printf("%s()/%s - failed on data set %d:\n", func, subtest, i);
					printf("expected %u, got %u\n", (unsigned)data[i].expected[j], (unsigned)int8buf[j]);
					return 1;
				}
			}
		}
		return 0;
	}


	if (!strcmp(func, "s_int8_to_result_format") && !strcmp(subtest, "s2.13")) {

		struct Bmpread br = { .result_format = BMP_FORMAT_S2_13, .result_bitsperchannel = 16 };
		struct {
			int      channels;
			int      rgba[4];
			uint16_t expected[4];
		} data[] = {
			{ .channels = 3, .rgba = { 0, 0, 0, 0}, .expected = { 0, 0, 0, 0 } },
			{ .channels = 3, .rgba = { 255, 255, 255, 255}, .expected = { 0x2000, 0x2000, 0x2000, 0 } },
			{ .channels = 4, .rgba = { 127, 128, 1, 254}, .expected = { 0x0ff0, 0x1010, 0x0020, 0x1fe0 } },
		};
		uint16_t s213buf[4] = { 0 };

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			br.result_channels = data[i].channels;
			s_int8_to_result_format(&br, data[i].rgba, (unsigned char*) s213buf);
			for (int j = 0; j < 4; j++) {
				if (s213buf[j] != data[i].expected[j]) {
					printf("%s()/%s - failed on data set %d:\n", func, subtest, i);
					printf("expected %u, got %u\n", (unsigned)data[i].expected[j], (unsigned)s213buf[j]);
					return 1;
				}
			}
		}
		return 0;
	}

	fprintf(stderr, "Invalid test '%s'\n", func);
	return 2;
}
