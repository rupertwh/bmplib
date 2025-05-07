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
	const char *func /*, *subtest =""*/;

	if (argc < 2) {
		fprintf(stderr, "Invalid invocation\n");
		return 2;
	}
	func = argv[1];
	/*if (argc >= 3)
		subtest = argv[2];*/



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

	fprintf(stderr, "Invalid test '%s'\n", func);
	return 2;
}
