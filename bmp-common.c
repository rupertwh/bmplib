/* bmplib - bmp-common.c
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
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

#include "config.h"
#include "bmplib.h"
#include "logging.h"
#include "bmp-common.h"
#include "bmp-read.h"
#include "bmp-write.h"


struct Bmphandle {
	struct {
		uint32_t magic;
		LOG      log;
	};
};



/********************************************************
 * 	bmp_version
 *******************************************************/

API const char* bmp_version(void)
{
	return LIBRARY_VERSION;
}



/********************************************************
 * 	bmp_errmsg
 *******************************************************/

API const char* bmp_errmsg(BMPHANDLE h)
{
	if (!(h && (h->magic == HMAGIC_READ || h->magic == HMAGIC_WRITE)))
		return "BMPHANDLE is NULL or invalid";
	
	return logmsg(h->log);
}



/********************************************************
 * 	bmp_free
 *******************************************************/

API void bmp_free(BMPHANDLE h)
{
	if (!h)
		return;

	switch (h->magic) {
	case HMAGIC_READ:
		br_free((BMPREAD)(void*)h);
		break;
	case HMAGIC_WRITE:
		bw_free((BMPWRITE)(void*)h);
		break;

	default:
#ifdef DEBUG
		printf("bmp_free() called with invalid handle (0x%04x)\n",
		                   (unsigned int) h->magic);
#endif	
		break;
	}
}



/********************************************************
 * 	cm_check_is_read_handle
 *******************************************************/

int cm_check_is_read_handle(BMPHANDLE h)
{
	BMPREAD rp = (BMPREAD)(void*)h;

	if (rp && rp->magic == HMAGIC_READ)
		return TRUE;
	return FALSE;
}



/********************************************************
 * 	cm_gobble_up
 *******************************************************/

int cm_gobble_up(FILE *file, int count, LOG log)
{
	int i;

	for (i = 0; i < count; i++) {
		if (EOF == getc(file)) {
			if (feof(file))
				logerr(log, "unexpected end of file");
			else
				logsyserr(log, "error reading from file");
			return FALSE;
		}
	}
	return TRUE;
}



/********************************************************
 * 	cm_count_bits
 *
 *  counts the used bits in a number, i.e. the
 *  smallest necessary bitlength to represent the number
 *******************************************************/

int cm_count_bits(unsigned long v)
{
	int bits = 0;

	if (v < 0)
		v = -v;

	while (v) {
		bits++;
		v >>= 1;
	}
	return bits;
}



int cm_all_lessoreq_int(int limit, int n, ...)
{
	va_list ap;
	int i, ret = TRUE;

	if (n < 1)
		return TRUE;

	va_start(ap, n);
	for (i = 0; i < n; i++) {
		if (va_arg(ap, int) > limit) {
			ret = FALSE;
			break;
		}
	}
	va_end(ap);

	return ret;
}


int cm_all_equal_int(int n, ...)
{
	va_list ap;
	int first, i, ret = TRUE;

	if (n < 2)
		return TRUE;

	va_start(ap, n);
	first = va_arg(ap, int);
	for (i = 1; i < n; i++) {
		if (va_arg(ap, int) != first) {
			ret = FALSE;
			break;
		}
	}
	va_end(ap);

	return ret;
}


int cm_all_positive_int(int n, ...)
{
	va_list ap;
	int i, ret = TRUE;

	if (n < 1)
		return TRUE;

	va_start(ap, n);
	for (i = 0; i < n; i++) {
		if (va_arg(ap, int) < 0) {
			ret = FALSE;
			break;
		}
	}
	va_end(ap);

	return ret;
}



int cm_align4padding(unsigned long a)
{
	return cm_align4size(a) - a;
}

int cm_align2padding(unsigned long a)
{
	return cm_align2size(a) - a;
}



/*********************************************************
 *      endianess-agnostic functions to read/write
 *      from little-endian files
 *********************************************************/

int write_u16_le(FILE *file, uint16_t val)
{
	return (EOF != fputc(val & 0xff, file) &&
	        EOF != fputc((val >> 8) & 0xff, file));
}


int write_u32_le(FILE *file, uint32_t val)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (EOF == fputc((val >> (i*8)) & 0xff, file))
			return 0;
	}
	return 1;
}


int read_u16_le(FILE *file, uint16_t *val)
{
	unsigned char buf[2];

	if (2 != fread(buf, 1, 2, file))
		return 0;

	*val = (buf[1] << 8) | buf[0];

	return 1;
}


int read_u32_le(FILE *file, uint32_t *val)
{
	unsigned char buf[4];

	if (4 != fread(buf, 1, 4, file))
		return 0;

	*val = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];

	return 1;
}


int write_s16_le(FILE *file, int16_t val)
{
	return (EOF != fputc(val & 0xff, file) &&
	        EOF != fputc((val >> 8) & 0xff, file));
}


int read_s16_le(FILE *file, int16_t *val)
{
	unsigned char buf[2];

	if (2 != fread(buf, 1, 2, file))
		return 0;

	*val = (((int16_t)(signed char)buf[1]) << 8) | (int16_t) buf[0];

	return 1;
}


int read_s32_le(FILE *file, int32_t *val)
{
	unsigned char buf[4];

	if (4 != fread(buf, 1, 4, file))
		return 0;

	*val = (((int32_t)(signed char)buf[3]) << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];

	return 1;
}


int write_s32_le(FILE *file, int32_t val)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (EOF == fputc((val >> (i*8)) & 0xff, file))
			return 0;
	}
	return 1;
}
