/* bmplib - bmp-common.c
 *
 * Copyright (c) 2024, 2025, Rupert Weber.
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
#include <stdbool.h>

#define BMPLIB_LIB

#include "config.h"
#include "bmplib.h"
#include "logging.h"
#include "bmp-common.h"
#include "bmp-read.h"
#include "huffman.h"
#include "bmp-write.h"




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
	if (!(h && (h->common.magic == HMAGIC_READ || h->common.magic == HMAGIC_WRITE)))
		return "BMPHANDLE is NULL or invalid";

	return logmsg(h->common.log);
}



/********************************************************
 * 	bmp_set_number_format
 *******************************************************/

API BMPRESULT bmp_set_number_format(BMPHANDLE h, enum BmpFormat format)
{
	if (!h)
		return BMP_RESULT_ERROR;

	switch (h->common.magic) {
	case HMAGIC_READ:
		return br_set_number_format(&h->read, format);

	case HMAGIC_WRITE:
		return bw_set_number_format(&h->write, format);

	default:
#ifdef DEBUG
		printf("bmp_set_number_format() called with invalid handle (0x%04x)\n",
		                   (unsigned int) h->common.magic);
#endif
		break;
	}
	return BMP_RESULT_ERROR;
}



/********************************************************
 * 	bmp_set_huffman_t4black_value
 *******************************************************/

API BMPRESULT bmp_set_huffman_t4black_value(BMPHANDLE h, int blackidx)
{
	if (!h)
		return BMP_RESULT_ERROR;

	if (!(h->common.magic == HMAGIC_READ || h->common.magic == HMAGIC_WRITE))
		return BMP_RESULT_ERROR;

	h->common.huffman_black_is_zero = !blackidx;

	return BMP_RESULT_OK;
}



/********************************************************
 * 	bmp_free
 *******************************************************/

API void bmp_free(BMPHANDLE h)
{
	if (!h)
		return;

	switch (h->common.magic) {
	case HMAGIC_READ:
		br_free(&h->read);
		break;
	case HMAGIC_WRITE:
		bw_free(&h->write);
		break;

	default:
#ifdef DEBUG
		printf("bmp_free() called with invalid handle (0x%04x)\n",
		                   (unsigned int) h->common.magic);
#endif
		break;
	}
}



/********************************************************
 * 	cm_read_handle
 *******************************************************/

BMPREAD cm_read_handle(BMPHANDLE h)
{
	if (h && h->common.magic == HMAGIC_READ)
		return &h->read;

	return NULL;
}


/********************************************************
 * 	cm_write_handle
 *******************************************************/

BMPWRITE cm_write_handle(BMPHANDLE h)
{
	if (h && h->common.magic == HMAGIC_WRITE)
		return &h->write;

	return NULL;
}


/********************************************************
 * 	cm_gobble_up
 *******************************************************/

bool cm_gobble_up(BMPREAD_R rp, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		if (EOF == getc(rp->file)) {
			if (feof(rp->file)) {
				rp->lasterr = BMP_ERR_TRUNCATED;
				logerr(rp->c.log, "unexpected end of file");
			} else {
				rp->lasterr = BMP_ERR_FILEIO;
				logsyserr(rp->c.log, "error reading from file");
			}
			return false;
		}
	}
	return true;
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

	while (v) {
		bits++;
		v >>= 1;
	}
	return bits;
}



const char* cm_conv64_name(enum Bmpconv64 conv)
{
	switch (conv) {
	case BMP_CONV64_SRGB  : return "BMP_CONV64_SRGB";
        case BMP_CONV64_LINEAR: return "BMP_CONV64_LINEAR";
	case BMP_CONV64_NONE  : return "BMP_CONV64_NONE";}
	return "(invalid)";
}


const char* cm_format_name(enum BmpFormat format)
{
	switch (format) {
	case BMP_FORMAT_INT  : return "BMP_FORMAT_INT";
        case BMP_FORMAT_FLOAT: return "BMP_FORMAT_FLOAT";
        case BMP_FORMAT_S2_13: return "BMP_FORMAT_S2_13";
	}
	return "(invalid)";
}



bool cm_all_lessoreq_int(int limit, int n, ...)
{
	va_list ap;
	int     i;
	bool    ret = true;

	if (n < 1)
		return true;

	va_start(ap, n);
	for (i = 0; i < n; i++) {
		if (va_arg(ap, int) > limit) {
			ret = false;
			break;
		}
	}
	va_end(ap);

	return ret;
}


bool cm_all_equal_int(int n, ...)
{
	va_list ap;
	int     first, i;
	bool    ret = true;

	if (n < 2)
		return true;

	va_start(ap, n);
	first = va_arg(ap, int);
	for (i = 1; i < n; i++) {
		if (va_arg(ap, int) != first) {
			ret = false;
			break;
		}
	}
	va_end(ap);

	return ret;
}


bool cm_all_positive_int(int n, ...)
{
	va_list ap;
	int     i;
	bool    ret = true;

	if (n < 1)
		return true;

	va_start(ap, n);
	for (i = 0; i < n; i++) {
		if (va_arg(ap, int) < 0) {
			ret = false;
			break;
		}
	}
	va_end(ap);

	return ret;
}


bool cm_is_one_of(int n, int candidate, ...)
{
	va_list ap;
	int     i;
	bool    ret = false;

	if (n < 1)
		return true;

	va_start(ap, candidate);
	for (i = 0; i < n; i++) {
		if (va_arg(ap, int) == candidate) {
			ret = true;
			break;
		}
	}
	va_end(ap);

	return ret;
}


int cm_align4padding(unsigned long long a)
{
	return (int) (cm_align4size(a) - a);
}


/*********************************************************
 *      endianess-agnostic functions to read/write
 *      from little-endian files
 *********************************************************/

bool write_u16_le(FILE *file, uint16_t val)
{
	return (EOF != fputc(val & 0xff, file) &&
	        EOF != fputc((val >> 8) & 0xff, file));
}


bool write_u32_le(FILE *file, uint32_t val)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (EOF == fputc((val >> (i*8)) & 0xff, file))
			return false;
	}
	return true;
}


bool read_u16_le(FILE *file, uint16_t *val)
{
	unsigned char buf[2];

	if (2 != fread(buf, 1, 2, file))
		return false;

	*val = ((unsigned)buf[1] << 8) | (unsigned)buf[0];

	return true;
}


bool read_u32_le(FILE *file, uint32_t *val)
{
	unsigned char buf[4];

	if (4 != fread(buf, 1, 4, file))
		return false;

	*val = ((uint32_t)buf[3] << 24) | ((uint32_t)buf[2] << 16) |
	       ((uint32_t)buf[1] << 8) | (uint32_t)buf[0];

	return true;
}


bool write_s16_le(FILE *file, int16_t val)
{
	return write_u16_le(file, (uint16_t)val);
}


bool write_s32_le(FILE *file, int32_t val)
{
	return write_u32_le(file, (uint32_t)val);
}

bool read_s16_le(FILE *file, int16_t *val)
{
	uint16_t u16;

	if (!read_u16_le(file, &u16))
		return false;

	if (u16 >= 0x8000U)
		*val = (int16_t)(u16 - 0x8000U) - 32767 - 1;
	else
		*val = (int16_t)u16;

	return true;
}

bool read_s32_le(FILE *file, int32_t *val)
{
	uint32_t u32;

	if (!read_u32_le(file, &u32))
		return false;

	if (u32 >= 0x80000000UL)
		*val = (int32_t)(u32 - 0x80000000UL) - 0x7fffffffL - 1;
	else
		*val = (int32_t)u32;

	return true;
}



uint32_t u32_from_le(const unsigned char *buf)
{
	return (uint32_t)buf[3] << 24 | (uint32_t)buf[2] << 16 |
	       (uint32_t)buf[1] << 8  | (uint32_t)buf[0];
}

int32_t s32_from_le(const unsigned char *buf)
{
	return (int32_t)u32_from_le(buf);
}

uint16_t u16_from_le(const unsigned char *buf)
{
	return (uint16_t)buf[1] << 8 | (uint16_t)buf[0];
}

int16_t s16_from_le(const unsigned char *buf)
{
	return (int16_t)u16_from_le(buf);
}





/*****************************************************************************
 * 	cm_infoheader_name
 *****************************************************************************/

const char* cm_infoheader_name(enum BmpInfoVer infoversion)
{
	switch (infoversion) {
	case BMPINFO_CORE_OS21 : return "OS21XBITMAPHEADER";
	case BMPINFO_OS22      : return "OS22XBITMAPHEADER";
	case BMPINFO_V3        : return "BITMAPINFOHEADER";
	case BMPINFO_V3_ADOBE1 : return "BITMAPINFOHEADER + RGB mask";
	case BMPINFO_V3_ADOBE2 : return "BITMAPINFOHEADER + RGBA mask";
	case BMPINFO_V4        : return "BITMAPV4HEADER";
	case BMPINFO_V5        : return "BITMAPV5HEADER";
	case BMPINFO_FUTURE    : return "unknown future version";
	default:
		return "invalid infoheader version";
	}
}
