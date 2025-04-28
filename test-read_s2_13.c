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


#define ARRAY_LEN(a) ((int)(sizeof (a) / sizeof ((a)[0])))

int main(int argc, const char **argv)
{
	const char *func;

	if (argc != 2) {
		fprintf(stderr, "Invalid invocation\n");
		return 2;
	}
	func = argv[1];


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
			float expected;
		} data[] = {
			{ .s2_13 = 0x2000u, .expected =  1.0},
			{ .s2_13 = 0xe000u, .expected = -1.0},
			{ .s2_13 = 0,       .expected =  0.0},
			{ .s2_13 = 0x7fffu, .expected =  3.99987793},
			{ .s2_13 = 0x8000u, .expected = -4.0},
		};

		for (int i = 0; i < ARRAY_LEN(data); i++) {
			double d = s_s2_13_to_float(data[i].s2_13);
			if (d != data[i].expected) {
				printf("%s() failed on data set %d: 0x%04x\n", func, i, data[i].s2_13);
				printf("expected %f, got %f\n", data[i].expected, d);
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

	fprintf(stderr, "Invalid test '%s'\n", func);

	return 2;
}
